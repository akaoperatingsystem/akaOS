#!/usr/bin/env python3
"""
tools/aka-makepkg.py - akaOS Package Creator & Repository Index Generator

Creates .akp package archives (POSIX ustar tar format) with .PKGINFO metadata,
package hooks, and SHA-256 checksums, and generates repository index (packages.db.txt).

Usage:
  Build a package:
    ./tools/aka-makepkg.py build --name nano --version 8.1-1 --arch x86_64 \
         --desc "GNU nano command-line text editor" \
         --root ./pkg-root/ --out ./packages/ [--depends "libc,ncurses"] \
         [--postinst ./postinst.sh] [--prerm ./prerm.sh]

  Generate or update repository index:
    ./tools/aka-makepkg.py index --repo-dir ./packages/ --arch x86_64 --out ./packages.db.txt
"""

import argparse
import hashlib
import io
import os
import sys
import tarfile

def compute_sha256(file_path):
    h = hashlib.sha256()
    with open(file_path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def sanitize_tar_path(path):
    # Normalize slashes and strip leading slashes
    parts = path.replace("\\", "/").strip("/").split("/")
    cleaned = []
    for p in parts:
        if p in ("", "."):
            continue
        if p == "..":
            if cleaned:
                cleaned.pop()
        else:
            cleaned.append(p)
    return "/".join(cleaned)

def build_package(name, version, arch, desc, root_dir, out_dir, depends="", postinst=None, prerm=None):
    os.makedirs(out_dir, exist_ok=True)
    pkg_filename = f"{name}-{version}-{arch}.akp"
    pkg_path = os.path.join(out_dir, pkg_filename)

    print(f"[aka-makepkg] Building {pkg_filename} from {root_dir}...")

    pkginfo_content = f"""Package: {name}
Version: {version}
Architecture: {arch}
Description: {desc}
Dependencies: {depends}
"""

    with tarfile.open(pkg_path, "w", format=tarfile.USTAR_FORMAT) as tar:
        # 1. Add .PKGINFO metadata file at the archive root
        info_bytes = pkginfo_content.encode("utf-8")
        ti = tarfile.TarInfo(name=".PKGINFO")
        ti.size = len(info_bytes)
        ti.mode = 0o644
        ti.uname = "root"
        ti.gname = "root"
        tar.addfile(ti, io.BytesIO(info_bytes))

        # 2. Add optional post-install hook
        if postinst and os.path.isfile(postinst):
            with open(postinst, "rb") as f:
                p_data = f.read()
            ti = tarfile.TarInfo(name="CONTROL/postinst")
            ti.size = len(p_data)
            ti.mode = 0o755
            ti.uname = "root"
            ti.gname = "root"
            tar.addfile(ti, io.BytesIO(p_data))

        # 3. Add optional pre-remove hook
        if prerm and os.path.isfile(prerm):
            with open(prerm, "rb") as f:
                r_data = f.read()
            ti = tarfile.TarInfo(name="CONTROL/prerm")
            ti.size = len(r_data)
            ti.mode = 0o755
            ti.uname = "root"
            ti.gname = "root"
            tar.addfile(ti, io.BytesIO(r_data))

        # 4. Add payload files from root_dir with clean, sanitized relative paths
        for root, dirs, files in os.walk(root_dir):
            for d in sorted(dirs):
                full_path = os.path.join(root, d)
                rel_path = sanitize_tar_path(os.path.relpath(full_path, root_dir))
                if not rel_path:
                    continue
                ti = tarfile.TarInfo(name=rel_path + "/")
                ti.type = tarfile.DIRTYPE
                ti.mode = 0o755
                ti.uname = "root"
                ti.gname = "root"
                tar.addfile(ti)

            for f in sorted(files):
                full_path = os.path.join(root, f)
                rel_path = sanitize_tar_path(os.path.relpath(full_path, root_dir))
                if not rel_path:
                    continue
                with open(full_path, "rb") as fp:
                    data = fp.read()
                st = os.stat(full_path)
                ti = tarfile.TarInfo(name=rel_path)
                ti.size = len(data)
                # Keep executable bit if present on host
                ti.mode = 0o755 if (st.st_mode & 0o111) else 0o644
                ti.uname = "root"
                ti.gname = "root"
                tar.addfile(ti, io.BytesIO(data))

    pkg_size = os.path.getsize(pkg_path)
    sha = compute_sha256(pkg_path)
    print(f"[aka-makepkg] Package created: {pkg_path}")
    print(f"             Size: {pkg_size} bytes, SHA256: {sha}")
    return pkg_path

def generate_index(repo_dir, arch, out_file):
    print(f"[aka-makepkg] Scanning {repo_dir} for {arch} packages...")
    entries = []

    if not os.path.exists(repo_dir):
        print(f"Error: repo directory {repo_dir} does not exist", file=sys.stderr)
        return False

    for f in sorted(os.listdir(repo_dir)):
        if f.endswith(f"-{arch}.akp") or f.endswith(".akp"):
            pkg_path = os.path.join(repo_dir, f)
            try:
                with tarfile.open(pkg_path, "r") as tar:
                    pkginfo = None
                    try:
                        fobj = tar.extractfile(".PKGINFO")
                        if fobj:
                            pkginfo = fobj.read().decode("utf-8")
                    except KeyError:
                        pass

                    if not pkginfo:
                        print(f"Warning: {f} missing .PKGINFO, skipping")
                        continue

                    lines = pkginfo.strip().splitlines()
                    data = {}
                    for line in lines:
                        if ":" in line:
                            k, v = line.split(":", 1)
                            data[k.strip().lower()] = v.strip()

                    size = os.path.getsize(pkg_path)
                    sha = compute_sha256(pkg_path)
                    entry = f"""Package: {data.get('package', f)}
Version: {data.get('version', '1.0')}
Architecture: {data.get('architecture', arch)}
Size: {size}
SHA256: {sha}
Filename: packages/{f}
Description: {data.get('description', '')}
Dependencies: {data.get('dependencies', '')}
"""
                    entries.append(entry)
            except Exception as e:
                print(f"Error reading {f}: {e}", file=sys.stderr)

    with open(out_file, "w") as out:
        out.write("# akaOS Repository Index\n# Generated by aka-makepkg\n\n")
        for e in entries:
            out.write(e + "\n")

    print(f"[aka-makepkg] Wrote index with {len(entries)} package(s) to {out_file}")
    return True

def main():
    parser = argparse.ArgumentParser(description="akaOS Package Management Tool")
    subparsers = parser.add_subparsers(dest="command")

    build_p = subparsers.add_parser("build", help="Build a .akp package")
    build_p.add_argument("--name", required=True, help="Package name")
    build_p.add_argument("--version", required=True, help="Package version (e.g. 1.0.0-1)")
    build_p.add_argument("--arch", default="x86_64", help="Architecture")
    build_p.add_argument("--desc", default="", help="Description")
    build_p.add_argument("--depends", default="", help="Comma-separated dependencies")
    build_p.add_argument("--postinst", default=None, help="Path to post-installation script")
    build_p.add_argument("--prerm", default=None, help="Path to pre-removal script")
    build_p.add_argument("--root", required=True, help="Root directory containing payload")
    build_p.add_argument("--out", default="./packages", help="Output directory")

    idx_p = subparsers.add_parser("index", help="Generate repository packages.db.txt index")
    idx_p.add_argument("--repo-dir", required=True, help="Directory containing .akp packages")
    idx_p.add_argument("--arch", default="x86_64", help="Architecture")
    idx_p.add_argument("--out", required=True, help="Output packages.db.txt path")

    args = parser.parse_args()
    if args.command == "build":
        build_package(args.name, args.version, args.arch, args.desc, args.root, args.out,
                      args.depends, args.postinst, args.prerm)
    elif args.command == "index":
        generate_index(args.repo_dir, args.arch, args.out)
    else:
        parser.print_help()

if __name__ == "__main__":
    main()
