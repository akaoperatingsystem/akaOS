#!/usr/bin/env python3
"""
tools/test-pkg.py - Comprehensive End-to-End Test Suite for akaOS pkg Manager

Spins up a lightweight local mock HTTP mirror server, generates test packages
with dependencies and lifecycle hooks, and executes pkg commands:
  - pkg update
  - pkg search
  - pkg info
  - pkg install (with recursive dependency resolution)
  - pkg list
  - pkg upgrade (version comparison & upgrade)
  - pkg remove (cleanup and hook execution)
  - checksum verification (tampered package detection)
"""

import hashlib
import http.server
import os
import shutil
import socketserver
import subprocess
import sys
import threading
import time

PORT = 18888
HOST = "127.0.0.1"
WORKSPACE = "/tmp/akaos-pkg-test"

def run_cmd(cmd, env=None):
    res = subprocess.run(cmd, shell=True, capture_output=True, text=True, env=env)
    return res

class QuietHTTPHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, format, *args):
        pass # Suppress server access log

def start_http_server(directory):
    class CustomHandler(QuietHTTPHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=directory, **kwargs)

    socketserver.TCPServer.allow_reuse_address = True
    httpd = socketserver.TCPServer((HOST, PORT), CustomHandler)
    server_thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    server_thread.start()
    return httpd

def main():
    print("==================================================")
    print("  akaOS Package Manager (pkg) Comprehensive Test  ")
    print("==================================================")

    # 1. Prepare sandboxed workspace
    if os.path.exists(WORKSPACE):
        shutil.rmtree(WORKSPACE)
    os.makedirs(WORKSPACE)

    mirror_root = os.path.join(WORKSPACE, "mirror", "akaos", "x86_64")
    repo_packages_dir = os.path.join(mirror_root, "packages")
    os.makedirs(repo_packages_dir, exist_ok=True)

    test_root = os.path.join(WORKSPACE, "target_root")
    os.makedirs(test_root, exist_ok=True)

    db_dir = os.path.join(WORKSPACE, "db")
    cache_dir = os.path.join(WORKSPACE, "cache")
    os.makedirs(db_dir, exist_ok=True)
    os.makedirs(cache_dir, exist_ok=True)

    conf_path = os.path.join(WORKSPACE, "pkg.conf")
    with open(conf_path, "w") as f:
        f.write(f"""mirror=http://{HOST}:{PORT}/akaos
arch=x86_64
cachedir={cache_dir}
dbdir={db_dir}
""")

    # 2. Build test packages using tools/aka-makepkg.py
    makepkg = sys.executable + " " + os.path.abspath("tools/aka-makepkg.py")

    # Package A: "libshared" (v1.0-1) - a dependency package
    lib_payload = os.path.join(WORKSPACE, "build_lib")
    os.makedirs(os.path.join(lib_payload, "usr", "lib"), exist_ok=True)
    with open(os.path.join(lib_payload, "usr", "lib", "libshared.so"), "w") as f:
        f.write("/* dummy shared library 1.0 */")
    res = run_cmd(f"{makepkg} build --name libshared --version 1.0-1 --arch x86_64 --desc 'Shared library for test' --root {lib_payload} --out {repo_packages_dir}")
    assert res.returncode == 0, f"Failed to build libshared: {res.stderr}"

    # Package B: "myapp" (v1.0-1) - depends on libshared, has postinst and prerm hooks
    app_payload = os.path.join(WORKSPACE, "build_app")
    os.makedirs(os.path.join(app_payload, "usr", "bin"), exist_ok=True)
    with open(os.path.join(app_payload, "usr", "bin", "myapp"), "w") as f:
        f.write("#!/bin/sh\necho myapp running\n")
    os.chmod(os.path.join(app_payload, "usr", "bin", "myapp"), 0o755)

    postinst_script = os.path.join(WORKSPACE, "myapp-postinst.sh")
    with open(postinst_script, "w") as f:
        f.write(f"#!/bin/sh\necho 'myapp postinst hook executed' > {WORKSPACE}/postinst_ran.txt\n")
    os.chmod(postinst_script, 0o755)

    prerm_script = os.path.join(WORKSPACE, "myapp-prerm.sh")
    with open(prerm_script, "w") as f:
        f.write(f"#!/bin/sh\necho 'myapp prerm hook executed' > {WORKSPACE}/prerm_ran.txt\n")
    os.chmod(prerm_script, 0o755)

    res = run_cmd(f"{makepkg} build --name myapp --version 1.0-1 --arch x86_64 --desc 'Test application with dependencies' --depends libshared --postinst {postinst_script} --prerm {prerm_script} --root {app_payload} --out {repo_packages_dir}")
    assert res.returncode == 0, f"Failed to build myapp: {res.stderr}"

    # Generate repository index
    index_path = os.path.join(mirror_root, "packages.db.txt")
    res = run_cmd(f"{makepkg} index --repo-dir {repo_packages_dir} --arch x86_64 --out {index_path}")
    assert res.returncode == 0, f"Failed to generate index: {res.stderr}"

    def trust_index():
        # Provision locally; the HTTP server is never the source of this pin.
        with open(index_path, "rb") as f:
            digest = hashlib.sha256(f.read()).hexdigest()
        with open(conf_path, "a") as f:
            f.write(f"metadata_sha256={digest}\n")

    # 3. Start mock HTTP mirror
    httpd = start_http_server(os.path.join(WORKSPACE, "mirror"))
    time.sleep(0.5)

    # Compile pkg for host testing
    pkg_bin = os.path.join(WORKSPACE, "pkg")
    res = run_cmd(f"gcc -Wall -Wextra -Werror userspace/bin/pkg/main.c -o {pkg_bin}")
    assert res.returncode == 0, f"Failed to compile pkg: {res.stderr}"

    env = os.environ.copy()
    env["PKG_CONF"] = conf_path
    env["PKG_ROOT"] = test_root

    # Missing trust configuration must fail without publishing metadata.
    res = run_cmd(f"{pkg_bin} update", env=env)
    assert res.returncode != 0
    assert not os.path.exists(os.path.join(db_dir, "packages.db"))
    for bad_pin in ("a" * 63, "g" * 64, "a" * 65):
        with open(conf_path, "a") as f:
            f.write(f"metadata_sha256={bad_pin}\n")
        res = run_cmd(f"{pkg_bin} update", env=env)
        assert res.returncode != 0
        assert not os.path.exists(os.path.join(db_dir, "packages.db"))
    trust_index()

    # TEST 1: pkg update
    print("\n[TEST 1] Testing 'pkg update'...")
    res = run_cmd(f"{pkg_bin} update", env=env)
    print(res.stdout.strip())
    assert res.returncode == 0, f"pkg update failed: {res.stderr}"
    assert os.path.isfile(os.path.join(db_dir, "packages.db")), "packages.db not created"
    print(" -> PASS: Repository metadata updated successfully")

    with open(index_path, "rb") as f:
        trusted_index = f.read()
    database = os.path.join(db_dir, "packages.db")
    # Replace the archive and its matching digest together, as an on-path
    # attacker could do. Only the independently configured pin stops this.
    archive = os.path.join(repo_packages_dir, "myapp-1.0-1-x86_64.akp")
    with open(archive, "rb") as f:
        trusted_archive = f.read()
    malicious_archive = trusted_archive + b"attacker replacement"
    with open(archive, "wb") as f:
        f.write(malicious_archive)
    forged_index = trusted_index.replace(
        hashlib.sha256(trusted_archive).hexdigest().encode(),
        hashlib.sha256(malicious_archive).hexdigest().encode(),
    )
    assert forged_index != trusted_index
    with open(index_path, "wb") as f:
        f.write(forged_index)
    res = run_cmd(f"{pkg_bin} update", env=env)
    assert res.returncode != 0 and "authentication failed" in res.stderr
    with open(database, "rb") as f:
        assert f.read() == trusted_index
    shutil.copyfile(index_path, database)
    res = run_cmd(f"{pkg_bin} install myapp", env=env)
    assert res.returncode != 0 and "authentication failed" in res.stderr
    res = run_cmd(f"{pkg_bin} upgrade", env=env)
    assert res.returncode != 0 and "authentication failed" in res.stderr
    with open(archive, "wb") as f:
        f.write(trusted_archive)
    assert not os.path.exists(os.path.join(WORKSPACE, "postinst_ran.txt"))
    with open(index_path, "wb") as f:
        f.write(trusted_index)
    with open(database, "wb") as f:
        f.write(trusted_index)

    for bad_digest in ("", "a" * 63, "g" * 64):
        lines = trusted_index.decode().splitlines()
        invalid = "\n".join(
            "SHA256: " + bad_digest if line.startswith("SHA256:") else line
            for line in lines
        ).encode()
        with open(index_path, "wb") as f:
            f.write(invalid)
        trust_index()
        res = run_cmd(f"{pkg_bin} update", env=env)
        assert res.returncode == 0
        res = run_cmd(f"{pkg_bin} install myapp", env=env)
        assert res.returncode != 0 and "missing or invalid SHA-256" in res.stderr
        assert not os.path.exists(os.path.join(test_root, "usr", "bin", "myapp"))
        assert not os.path.exists(os.path.join(WORKSPACE, "postinst_ran.txt"))
    with open(index_path, "wb") as f:
        f.write(trusted_index)
    trust_index()
    res = run_cmd(f"{pkg_bin} update", env=env)
    assert res.returncode == 0

    # TEST 2: pkg search & pkg info
    print("\n[TEST 2] Testing 'pkg search' and 'pkg info'...")
    res = run_cmd(f"{pkg_bin} search myapp", env=env)
    assert res.returncode == 0 and "myapp" in res.stdout, "pkg search failed"
    print(" -> search output:\n" + res.stdout.strip())

    res = run_cmd(f"{pkg_bin} info myapp", env=env)
    assert res.returncode == 0 and "libshared" in res.stdout, "pkg info failed"
    print(" -> info output:\n" + res.stdout.strip())
    print(" -> PASS: Package search and info verified")

    # TEST 3: pkg install myapp (verify dependency libshared installed automatically)
    print("\n[TEST 3] Testing 'pkg install myapp' (with automatic dependency resolution)...")
    res = run_cmd(f"{pkg_bin} install myapp", env=env)
    print(res.stdout.strip())
    assert res.returncode == 0, f"pkg install myapp failed: {res.stderr}"

    # Verify both libshared and myapp are extracted in test_root
    assert os.path.isfile(os.path.join(test_root, "usr", "lib", "libshared.so")), "libshared.so was not extracted!"
    assert os.path.isfile(os.path.join(test_root, "usr", "bin", "myapp")), "myapp was not extracted!"
    assert os.path.isfile(os.path.join(WORKSPACE, "postinst_ran.txt")), "postinst hook did not execute!"
    print(" -> PASS: myapp and its dependency libshared installed; postinst hook executed successfully")

    # TEST 4: pkg list
    print("\n[TEST 4] Testing 'pkg list'...")
    res = run_cmd(f"{pkg_bin} list", env=env)
    print(res.stdout.strip())
    assert res.returncode == 0 and "myapp" in res.stdout and "libshared" in res.stdout
    print(" -> PASS: Installed package database lists both packages")

    # TEST 5: pkg upgrade
    print("\n[TEST 5] Testing 'pkg upgrade'...")
    # Build v2.0-1 of myapp and update repo index
    with open(os.path.join(app_payload, "usr", "bin", "myapp"), "w") as f:
        f.write("#!/bin/sh\necho myapp v2.0 running\n")
    res = run_cmd(f"{makepkg} build --name myapp --version 2.0-1 --arch x86_64 --desc 'Test app v2' --depends libshared --root {app_payload} --out {repo_packages_dir}")
    assert res.returncode == 0
    res = run_cmd(f"{makepkg} index --repo-dir {repo_packages_dir} --arch x86_64 --out {index_path}")
    assert res.returncode == 0

    trust_index()

    # Update metadata
    res = run_cmd(f"{pkg_bin} update", env=env)
    assert res.returncode == 0

    # Run upgrade
    res = run_cmd(f"{pkg_bin} upgrade", env=env)
    print(res.stdout.strip())
    assert res.returncode == 0 and "Upgrading myapp" in res.stdout

    # Verify new version in myapp binary
    with open(os.path.join(test_root, "usr", "bin", "myapp"), "r") as f:
        content = f.read()
    assert "v2.0" in content, "myapp file was not upgraded to v2.0!"
    print(" -> PASS: Upgrade correctly detected new version and updated payload")

    # TEST 6: SHA-256 Checksum Tampering Protection
    print("\n[TEST 6] Testing SHA-256 Checksum Verification...")
    # Tamper with an archive on the mirror
    tamper_file = os.path.join(repo_packages_dir, "myapp-2.0-1-x86_64.akp")
    # Clear local cache first
    shutil.rmtree(cache_dir)
    os.makedirs(cache_dir)
    with open(tamper_file, "a") as f:
        f.write("TAMPERED_BYTES")
    
    # Remove installed db record or run remove first so install proceeds
    run_cmd(f"{pkg_bin} remove myapp", env=env)
    # Try installing tampered package - must reject!
    res = run_cmd(f"{pkg_bin} install myapp", env=env)
    assert "SHA-256 mismatch" in res.stderr or "SHA-256 mismatch" in res.stdout or res.returncode != 0
    print(" -> Tampered package correctly caught:\n" + (res.stderr or res.stdout).strip())
    print(" -> PASS: Package integrity verification validated")

    # Restore untampered package, update index, update pkg, and install for TEST 7
    res = run_cmd(f"{makepkg} build --name myapp --version 2.0-1 --arch x86_64 --desc 'Test app v2' --depends libshared --postinst {postinst_script} --prerm {prerm_script} --root {app_payload} --out {repo_packages_dir}")
    assert res.returncode == 0
    res = run_cmd(f"{makepkg} index --repo-dir {repo_packages_dir} --arch x86_64 --out {index_path}")
    assert res.returncode == 0
    trust_index()
    res = run_cmd(f"{pkg_bin} update", env=env)
    assert res.returncode == 0
    res = run_cmd(f"{pkg_bin} install myapp", env=env)
    assert res.returncode == 0, f"install failed: {res.stdout} {res.stderr}"

    # TEST 7: pkg remove
    print("\n[TEST 7] Testing 'pkg remove myapp'...")
    res = run_cmd(f"{pkg_bin} remove myapp", env=env)
    print(res.stdout.strip())
    assert res.returncode == 0
    assert not os.path.exists(os.path.join(test_root, "usr", "bin", "myapp")), "myapp was not deleted!"
    assert os.path.isfile(os.path.join(WORKSPACE, "prerm_ran.txt")), "prerm hook did not execute!"
    print(" -> PASS: Package uninstalled, files removed, and prerm hook executed")

    httpd.shutdown()
    print("\n==================================================")
    print("  ALL PACKAGE MANAGER TESTS PASSED SUCCESSFULLY!  ")
    print("==================================================")
    return 0

if __name__ == "__main__":
    sys.exit(main())
