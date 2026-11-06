# akaOS Software Bundles & Library Architecture

This document describes the software bundle profiles available in `build.sh` and `run.sh` (`--software`), what binaries and libraries each profile packages, and the distinction between core base libraries and heavy GUI/desktop toolkits.

---

## 1. Overview of Library Tiers

Software in akaOS relies on two distinct tiers of shared libraries:

1. **Core Base Libraries (`/lib`):**
   - Essential C/C++ runtime: `libc.so.6` (freestanding akaOS libc), `libm.so.6` (math), `ld-akaos.so` (ELF dynamic linker), `libpthread.so.0`.
   - Small footprint (~2 MB total).
   - Included in every functional bundle (`all`, `desktop`, `dev`, `net`, `manual`, `minimal`).
2. **Heavy GUI & Desktop Toolkits (`/usr/local/lib`):**
   - Graphics & GUI runtimes: GTK+ 3.24 (`libgtk-3.so.0`), GTK 4.22 (`libgtk-4.so.1`), Qt 6.12 (`libQt6Core.so.6`, `libQt6Gui.so.6`, `libQt6Widgets.so.6`), Cairo (`libcairo.so.2`), Pango (`libpango-1.0.so`), HarfBuzz, FreeType, Fontconfig.
   - X11 Protocol libraries: `libX11.so.6`, `libXext.so.6`, `libXrender.so.1`, `libXinerama.so.1`, `libXrandr.so.2`, `libXfixes.so.3`, `libXcomposite.so.1`, `libXdamage.so.1`, `libICE.so.6`, `libSM.so.6`.
   - Desktop integration: `libglib-2.0.so`, `libgio-2.0.so`, `libgobject-2.0.so`, `libdbus-1.so.3`, `libcanberra.so`, `libfm-gtk3.so`, `libwnck-3.so`, `libxkbcommon.so`.
   - Large footprint (~50 MB - 150 MB+).
   - Only included in bundles that require graphical display support (`all`, `desktop`, `libs`, or when GUI apps are checked in `manual`).

---

## 2. Software Bundle Matrix

| Bundle Name | Primary Target | Includes Core Libs? (`libc`, `libm`) | Includes Heavy GUI Libs? (GTK, Qt, Cairo, X11) | Packaged Software Highlights | Approximate Disk Size |
| :--- | :--- | :---: | :---: | :--- | :--- |
| **`all`** | Full Operating System | **YES** | **YES** | All 124+ tools, GUI apps, WMs, daemons, diagnostics | ~150 - 300 MB |
| **`apps`** | All Apps Without Libs | **YES** (core) | **NO** | All 124+ CLI utilities & native apps without heavy GUI toolkits | ~50 - 80 MB |
| **`desktop`** | Daily Workstation | **YES** | **YES** | Desktops (XFCE, LXDE, Openbox), GUI apps, terminal | ~100 - 180 MB |
| **`libs`** | Shared Library Runtime | **YES** | **YES** | All 30+ shared libraries & toolkits for dev testing | ~90 - 150 MB |
| **`dev`** | Hardware & Development | **YES** | **NO** | `pkg`, `akafetch`, `lscpu`, `lspci`, `gpuinfo`, `sysmon` | ~35 - 50 MB |
| **`net`** | Server & Network Appliance | **YES** | **NO** | `httpd`, `sshd`, `telnetd`, `wget`, `curl`, `ip`, `route` | ~40 - 60 MB |
| **`manual`** | Custom User Selection | **YES** | **Dynamic** | User-selected from 82-package interactive checklist | Dynamic |
| **`minimal`** | Minimal Base Shell | **YES** (minimal) | **NO** | `akash` shell, `login`, `init`, basic POSIX tools | ~20 - 30 MB |
| **`none`** | Kernel Bootstrap Only | **NO** | **NO** | Barebones XSCore kernel & minimal PID 1 only | < 15 MB |

---

## 3. Bundle Details & Use Cases

### `all` (Full Suite)
- **When to choose:** You want a full-featured desktop OS with every utility, window manager, and server daemon ready out-of-the-box.
- **Includes:** All desktop environments (XFCE 4.18, LXDE, Openbox 3.6.1, Aurora), all GUI utilities (`terminal`, `notepad`, `taskmgr`, `webviewer`), all servers (`httpd`, `sshd`, `telnetd`, `tftpd`), all diagnostic tools, and all shared libraries.

### `apps` (All Apps Without Heavy Upstream GUI Libs)
- **When to choose:** You want all native console tools, shell utilities, networking servers, compression suites, and developer diagnostics, but without compiling or packaging heavy upstream GUI toolkits (Wayland, GTK 3/4, Qt 6, Cairo, X11).
- **Includes:** Core C/C++ runtime (`libc.so.6`, `libm.so.6`, `ld-akaos.so`), all 124+ tools in `/bin`, network servers (`httpd`, `sshd`, `telnetd`, `tftpd`), and developer tools.
- **Omits:** Heavy upstream GUI toolkits (`libgtk-3`, `libgtk-4`, `libQt6*`, `libcairo`, `libX11`, `libpango`).

### `desktop` (Desktop Essentials)
- **When to choose:** You want a graphical desktop workstation without running background server services.
- **Includes:** The selected desktop environment (XFCE, LXDE, or Openbox), the X11 display server, compositor, GUI apps, and all required GUI libraries (GTK3/4, Cairo, X11). Server daemons are omitted.

### `dev` (Developer & Diagnostics)
- **When to choose:** You want a fast console environment with hardware diagnostics, benchmarking tools, and the package manager without compiling or packaging heavy GUI toolkits.
- **Includes:** `pkg` (package manager), `akafetch` (hardware visualizer), `lscpu`, `lspci`, `gpuinfo`, `sndinfo`, `acpi`, `sysmon`, and core Unix tools.
- **Omits:** Wayland, GTK 3/4, Qt 6, and Cairo libraries (builds in seconds).

### `net` (Network & Servers)
- **When to choose:** Building a headless network appliance, router, micro web server, or firewall.
- **Includes:** `httpd`, `sshd`, `telnetd`, `tftpd`, `wget`, `curl`, `ip`, `route`, `ping`, `traceroute`, `netstat`.
- **Omits:** All GUI and display libraries.

### `manual` (Custom Package Checklist)
- **When to choose:** You want granular control over individual packages.
- **Operation:** Opens an interactive 82-package checklist in the TUI (or accepts `--packages "pkg,akafetch,wget"` on the CLI). Only selected binaries and their strictly required dependencies are bundled.

### `minimal` (Base Shell)
- **When to choose:** Embedded systems, minimal rescue environments, or fastest possible boot testing.
- **Includes:** `akash` shell, `login`, `init`, and essential file commands (`ls`, `cat`, `cp`, `rm`, `mkdir`, `ps`).
