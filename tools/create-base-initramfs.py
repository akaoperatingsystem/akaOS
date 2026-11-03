#!/usr/bin/env python3
import os, subprocess

ROOT_DIR = "/mnt/c/Users/iniko/Documents/Projects/akaOS"
BUILD_DIR = f"{ROOT_DIR}/build"

raw_dirs = [
    "/bin", "/etc", "/root:0700", "/home", "/home/user:0755",
    "/mnt", "/run", "/run/dbus", "/run/user", "/run/user/0:0700",
    "/tmp", "/lib", "/usr", "/usr/share", "/usr/share/aspell",
    "/usr/local", "/usr/local/bin", "/usr/local/lib",
    "/var", "/var/lib", "/var/lib/pkg", "/var/cache", "/var/cache/pkg",
    "/var/www", "/var/www/cgi-bin"
]

raw_files = [
    f"/usr/share/aspell/en.dict:{ROOT_DIR}/userspace/share/aspell/en.dict:0644",
    f"/usr/share/aspell/en_US.dict:{ROOT_DIR}/userspace/share/aspell/en_US.dict:0644",
    f"/bin/hello:{BUILD_DIR}/HELLO.ELF:exec",
    f"/bin/hello-pie:{BUILD_DIR}/HELLO_PIE.ELF:exec",
    f"/bin/hello-pi:{BUILD_DIR}/HELLO_PIE.ELF:exec",
    f"/bin/cat:{BUILD_DIR}/CAT.ELF:exec",
    f"/bin/ls:{BUILD_DIR}/LS.ELF:exec",
    f"/bin/grep:{BUILD_DIR}/GREP.ELF:exec",
    f"/bin/head:{BUILD_DIR}/HEAD.ELF:exec",
    f"/bin/tail:{BUILD_DIR}/TAIL.ELF:exec",
    f"/bin/wc:{BUILD_DIR}/WC.ELF:exec",
    f"/bin/uname:{BUILD_DIR}/UNAME.ELF:exec",
    f"/bin/akafetch:{BUILD_DIR}/AKAFETCH.ELF:exec",
    f"/bin/whoami:{BUILD_DIR}/WHOAMI.ELF:exec",
    f"/bin/touch:{BUILD_DIR}/TOUCH.ELF:exec",
    f"/bin/mkdir:{BUILD_DIR}/MKDIR.ELF:exec",
    f"/bin/rm:{BUILD_DIR}/RM.ELF:exec",
    f"/bin/cp:{BUILD_DIR}/CP.ELF:exec",
    f"/bin/mv:{BUILD_DIR}/MV.ELF:exec",
    f"/bin/chmod:{BUILD_DIR}/CHMOD.ELF:exec",
    f"/bin/chown:{BUILD_DIR}/CHOWN.ELF:exec",
    f"/bin/ted:{BUILD_DIR}/TED.ELF:exec",
    f"/bin/pwd:{BUILD_DIR}/PWD.ELF:exec",
    f"/bin/echo:{BUILD_DIR}/ECHO.ELF:exec",
    f"/bin/clear:{BUILD_DIR}/CLEAR.ELF:exec",
    f"/bin/sync:{BUILD_DIR}/SYNC.ELF:exec",
    f"/bin/init:{BUILD_DIR}/INIT.ELF:exec",
    f"/bin/login:{BUILD_DIR}/LOGIN.ELF:4755",
    f"/bin/su:{BUILD_DIR}/SU.ELF:4755",
    f"/bin/passwd:{BUILD_DIR}/PASSWD.ELF:4755",
    f"/bin/sudo:{BUILD_DIR}/SUDO.ELF:4755",
    f"/bin/reboot:{BUILD_DIR}/REBOOT.ELF:exec",
    f"/bin/poweroff:{BUILD_DIR}/POWEROFF.ELF:exec",
    f"/bin/lumen:{BUILD_DIR}/LUMEN.ELF:exec",
    f"/bin/palette:{BUILD_DIR}/PALETTE.ELF:exec",
    f"/bin/aurora:{BUILD_DIR}/AURORA.ELF:exec",
    f"/bin/terminal:{BUILD_DIR}/TERMINAL.ELF:exec",
    f"/bin/notify:{BUILD_DIR}/NOTIFY.ELF:exec",
    f"/bin/beep:{BUILD_DIR}/BEEP.ELF:exec",
    f"/bin/ping:{BUILD_DIR}/PING.ELF:exec",
    f"/bin/netstat:{BUILD_DIR}/NETSTAT.ELF:exec",
    f"/bin/tty:{BUILD_DIR}/TTY.ELF:exec",
    f"/bin/stty:{BUILD_DIR}/STTY.ELF:exec",
    f"/bin/ps:{BUILD_DIR}/PS.ELF:exec",
    f"/bin/akash:{BUILD_DIR}/AKASH.ELF:exec",
    f"/bin/sh:{BUILD_DIR}/AKASH.ELF:exec",
    f"/bin/nslookup:{BUILD_DIR}/NSLOOKUP.ELF:exec",
    f"/bin/curl:{BUILD_DIR}/CURL.ELF:exec",
    f"/bin/pkg:{BUILD_DIR}/PKG.ELF:exec",
    f"/bin/wget:{BUILD_DIR}/WGET.ELF:exec",
    f"/bin/httpd:{BUILD_DIR}/HTTPD.ELF:exec",
    f"/bin/akaos-config:{BUILD_DIR}/AKAOS_CONFIG.ELF:exec",
    f"/bin/sysmon:{BUILD_DIR}/SYSMON.ELF:exec",
    f"/bin/ip:{BUILD_DIR}/IP.ELF:exec",
    f"/bin/mount:{BUILD_DIR}/MOUNT.ELF:exec",
    f"/bin/umount:{BUILD_DIR}/UMOUNT.ELF:exec",
    f"/bin/telnetd:{BUILD_DIR}/TELNETD.ELF:exec",
    f"/bin/tftpd:{BUILD_DIR}/TFTPD.ELF:exec",
    f"/bin/tftp:{BUILD_DIR}/TFTP.ELF:exec",
    f"/bin/desktop:{BUILD_DIR}/DESKTOP.ELF:exec",
    f"/bin/startlxde:{BUILD_DIR}/DESKTOP_COMPAT.ELF:exec",
    f"/bin/openbox:{BUILD_DIR}/DESKTOP_COMPAT.ELF:exec",
    f"/bin/pcmanfm:{BUILD_DIR}/DESKTOP_COMPAT.ELF:exec",
    f"/bin/startlxqt:{BUILD_DIR}/DESKTOP_COMPAT.ELF:exec",
    f"/bin/lxqt-panel:{BUILD_DIR}/DESKTOP_COMPAT.ELF:exec",
    f"/bin/pcmanfm-qt:{BUILD_DIR}/DESKTOP_COMPAT.ELF:exec",
    f"/bin/gnome-shell:{BUILD_DIR}/DESKTOP_COMPAT.ELF:exec",
    f"/bin/mutter:{BUILD_DIR}/DESKTOP_COMPAT.ELF:exec",
    f"/bin/gnome-session:{BUILD_DIR}/DESKTOP_COMPAT.ELF:exec",
    f"/bin/plasma:{BUILD_DIR}/DESKTOP_COMPAT.ELF:exec",
    f"/bin/kwin:{BUILD_DIR}/DESKTOP_COMPAT.ELF:exec",
    f"/bin/startplasma:{BUILD_DIR}/DESKTOP_COMPAT.ELF:exec",
    f"/bin/notepad:{BUILD_DIR}/NOTEPAD.ELF:exec",
    f"/bin/taskmgr:{BUILD_DIR}/TASKMGR.ELF:exec",
    f"/bin/webviewer:{BUILD_DIR}/WEBVIEWER.ELF:exec",
    f"/bin/status_cgi:{BUILD_DIR}/STATUS_CGI.ELF:exec",
    f"/bin/less:{BUILD_DIR}/LESS.ELF:exec",
    f"/bin/more:{BUILD_DIR}/MORE.ELF:exec",
    f"/bin/find:{BUILD_DIR}/FIND.ELF:exec",
    f"/bin/sort:{BUILD_DIR}/SORT.ELF:exec",
    f"/bin/uniq:{BUILD_DIR}/UNIQ.ELF:exec",
    f"/bin/xargs:{BUILD_DIR}/XARGS.ELF:exec",
    f"/bin/test:{BUILD_DIR}/TEST.ELF:exec",
    f"/bin/[:{BUILD_DIR}/TEST.ELF:exec",
    f"/bin/sshd:{BUILD_DIR}/SSHD.ELF:exec",
    f"/bin/ssh:{BUILD_DIR}/SSH.ELF:exec",
    f"/bin/sleep:{BUILD_DIR}/SLEEP.ELF:exec",
    f"/bin/which:{BUILD_DIR}/WHICH.ELF:exec",
    f"/bin/env:{BUILD_DIR}/ENV.ELF:exec",
    f"/bin/cut:{BUILD_DIR}/CUT.ELF:exec",
    f"/bin/tr:{BUILD_DIR}/TR.ELF:exec",
    f"/bin/df:{BUILD_DIR}/DF.ELF:exec",
    f"/bin/du:{BUILD_DIR}/DU.ELF:exec",
    f"/bin/kill:{BUILD_DIR}/KILL.ELF:exec",
    f"/bin/pkill:{BUILD_DIR}/PKILL.ELF:exec",
    f"/bin/uptime:{BUILD_DIR}/UPTIME.ELF:exec",
    f"/bin/free:{BUILD_DIR}/FREE.ELF:exec",
    f"/bin/watch:{BUILD_DIR}/WATCH.ELF:exec",
    f"/bin/sed:{BUILD_DIR}/SED.ELF:exec",
    f"/bin/diff:{BUILD_DIR}/DIFF.ELF:exec",
    f"/bin/cmp:{BUILD_DIR}/CMP.ELF:exec",
    f"/bin/patch:{BUILD_DIR}/PATCH.ELF:exec",
    f"/bin/tar:{BUILD_DIR}/TAR.ELF:exec",
    f"/bin/gzip:{BUILD_DIR}/GZIP.ELF:exec",
    f"/bin/gunzip:{BUILD_DIR}/GUNZIP.ELF:exec",
    f"/bin/route:{BUILD_DIR}/ROUTE.ELF:exec",
    f"/bin/traceroute:{BUILD_DIR}/TRACEROUTE.ELF:exec",
    f"/bin/dig:{BUILD_DIR}/DIG.ELF:exec",
    f"/bin/arp:{BUILD_DIR}/ARP.ELF:exec",
    f"/bin/lscpu:{BUILD_DIR}/LSCPU.ELF:exec",
    f"/bin/lspci:{BUILD_DIR}/LSPCI.ELF:exec",
    f"/bin/gpuinfo:{BUILD_DIR}/GPUINFO.ELF:exec",
    f"/bin/sndinfo:{BUILD_DIR}/SNDINFO.ELF:exec",
    f"/bin/acpi:{BUILD_DIR}/ACPI.ELF:exec",
    f"/bin/xsdisplay:{BUILD_DIR}/XSDISPLAY.ELF:exec",
    f"/bin/dbus-daemon:{BUILD_DIR}/DBUS_DAEMON.ELF:exec",
    f"/bin/dltest:{BUILD_DIR}/DLTEST.ELF:exec",
    f"/bin/display-protocols:{BUILD_DIR}/DISPLAY_PROTOCOLS.ELF:exec",
    f"/bin/xserver:{BUILD_DIR}/XSERVER.ELF:exec",
    f"/bin/x11-server:{BUILD_DIR}/XSERVER.ELF:exec",
    f"/bin/x11-client:{BUILD_DIR}/X11_CLIENT.ELF:exec",
    f"/bin/x11-wm-client:{BUILD_DIR}/X11_WM_CLIENT.ELF:exec",
    f"/lib/ld-akaos.so:{BUILD_DIR}/LD_AKAOS.ELF:exec",
    f"/lib/libc.so.6:{BUILD_DIR}/libc.so.6:0444",
    f"/lib/libm.so.6:{BUILD_DIR}/libm.so.6:0444",
    f"/lib/libpthread.so:{BUILD_DIR}/libpthread.so:0444",
    f"/lib/libunwind.so.1:{BUILD_DIR}/libunwind.so.1:0444",
    f"/lib/libc++abi.so.1:{BUILD_DIR}/libc++abi.so.1:0444",
    f"/lib/libc++.so.1:{BUILD_DIR}/libc++.so.1:0444",
    f"/etc/passwd:{ROOT_DIR}/userspace/etc/passwd:0644",
    f"/etc/shadow:{ROOT_DIR}/userspace/etc/shadow:0600",
    f"/etc/group:{ROOT_DIR}/userspace/etc/group:0644",
    f"/etc/sudoers:{ROOT_DIR}/userspace/etc/sudoers:0440",
    f"/etc/hosts:{ROOT_DIR}/userspace/etc/hosts:0644",
    f"/etc/resolv.conf:{ROOT_DIR}/userspace/etc/resolv.conf:0644",
    f"/etc/os-release:{ROOT_DIR}/userspace/etc/os-release:0644",
    f"/etc/hostname:{ROOT_DIR}/userspace/etc/hostname:0644",
    f"/etc/pkg.conf:{ROOT_DIR}/userspace/etc/pkg.conf:0644",
    f"/etc/dbus-services.conf:{ROOT_DIR}/userspace/etc/dbus-services.conf:0644",
    f"/etc/fstab:{ROOT_DIR}/userspace/etc/fstab:0644",
    f"/etc/shells:{ROOT_DIR}/userspace/etc/shells:0644",
    f"/etc/profile:{ROOT_DIR}/userspace/etc/profile:0644",
    f"/var/www/cgi-bin/status:{BUILD_DIR}/STATUS_CGI.ELF:exec",
]

if os.path.exists(f"{ROOT_DIR}/userspace/var/www/index.html"):
    raw_files.append(f"/var/www/index.html:{ROOT_DIR}/userspace/var/www/index.html:0644")

raw_data = [
    "/etc/motd:akaOS XSCore initramfs\n",
    "/etc/akaos-desktop-compat:shim:0444"
]

args = ["python3", f"{ROOT_DIR}/tools/mkinitramfs.py", f"{BUILD_DIR}/initramfs.img", "--exclude-long-vfs-names"]
for d in raw_dirs:
    args += ["--dir", d]

valid_files = 0
for f in raw_files:
    parts = f.split(':')
    source = parts[1]
    if os.path.exists(source):
        args += ["--file", f]
        valid_files += 1

for d in raw_data:
    args += ["--data", d]

print(f"Creating base initramfs with {len(raw_dirs)} dirs, {valid_files} files, {len(raw_data)} data...")
subprocess.check_call(args)
print("Base initramfs created successfully with /etc/motd present!")
