# Contributor & AI Agent Guidelines

Welcome to **akaOS**! To ensure the project remains high quality, legal, transparent, and architecturally sound, all contributors — **both human developers and contributors using AI coding assistants** (such as Cursor, Copilot, DeepSeek, Claude, ChatGPT, etc.) — must strictly follow these rules before submitting code, documentation, or pull requests.

---

## 1. Universal BSD 3-Clause License Compliance (Strict)

- **Everything is BSD 3-Clause Licensed**: Every line of code, header file, assembly file, driver, user utility, script, and documentation contributed to akaOS must be released under the **[BSD 3-Clause License](../../LICENSE)**.
- **Header Notice**: Where appropriate, include the standard SPDX BSD 3-Clause license header:
  ```c
  /*
   * Copyright (c) 2026 akaOS Authors
   * SPDX-License-Identifier: BSD-3-Clause
   */
  ```
- **Zero Proprietary or Incompatible Code**: Never copy, paste, or import code with GPL/AGPL viral copyleft terms into the core kernel/libc, proprietary code, decompiled binaries, or uncredited snippets from other projects. All code in akaOS and XSCore must be clean and freely forkable for everyone, including commercial companies.

---

## 2. Upstream Desktop Purity (Zero-Patching Rule)

- **Completely Unmodified Upstream Software**: All desktop environments (GNOME, KDE Plasma, XFCE, MATE, Cinnamon, LXQt, LXDE) and window managers (Openbox, i3, KWin, Mutter, Xfwm4, Marco) must run as completely unmodified upstream software.
- **No Patching Upstream Desktops**: Never patch upstream desktop source code, never modify their branding, and never apply custom themes to upstream desktop codebases.
- **Compatibility Belongs in akaOS**: If a desktop environment or window manager lacks functionality:
  - Implement the required POSIX APIs in **libc** (`userspace/libc/`).
  - Implement the required X11 protocols or extensions in the **native display server** (`userspace/bin/xserver/`).
  - Implement missing syscalls, socket options, or driver interfaces in the **XSCore kernel** (`xscore/`).

---

## 3. Human Developer Standards

- **Clean Architecture Separation**:
  - `boot/`: UEFI loader and bootloader protocols.
  - `xscore/`: Kernel HAL, memory managers, process scheduling, VFS, device drivers.
  - `userspace/`: Native libc, dynamic linker (`ld-akaos`), core utilities, and userland applications.
  - `sdk/`: Shared kernel-to-userspace ABI and boot headers.
  - `docs/`: Public technical documentation.
- **Portability First**: akaOS supports 6 CPU architectures (**x86-64**, **x86-32**, **AArch64**, **ARM32**, **RISC-V64**, **RISC-V32**). Do not introduce architecture-specific assumptions into common kernel or libc code.
- **No Reliance on Linux**: akaOS is an original Unix-like operating system powered by the XSCore kernel. Do not introduce runtime dependencies on Linux or the Linux kernel.

---

## 4. AI Tool & Coding Assistant Directives

If you are using AI coding assistants (such as Cursor, Copilot, DeepSeek, Claude, ChatGPT, etc.) to generate or assist with code:

1. **Verify License Cleanliness**: Ensure the AI model does not generate verbatim snippets from copyleft or proprietary codebases. The resulting code must be 100% compatible with the BSD 3-Clause License.
2. **Review Before Submitting**: AI-generated code must be thoroughly reviewed by a human contributor. Do not submit hallucinations, dead code, or untested stubs.
3. **No Breaking Regressions**: Test all AI-generated code against automated QEMU regression scripts:
   ```sh
   ./test.sh
   ./tools/build-arch-smoke.sh --all
   ```
4. **Follow Project Style**: Match existing formatting, naming conventions, and freestanding C coding practices used throughout XSCore and akaOS libc.

---

## 5. Versioning Directives

akaOS and the XSCore kernel are versioned independently in [`sdk/include/akaos/version.h`](../../sdk/include/akaos/version.h):
- **`AKAOS_VERSION` (Operating System)**: Bump when adding or updating userspace tools, desktop environments, or libc APIs.
- **`XSCORE_VERSION` (Kernel)**: Bump when adding or modifying kernel code, device drivers, memory management, or syscall ABIs.
- Always use semantic versioning (`MAJOR.MINOR.PATCH`). Never use date-based timestamps or codenames (e.g. no "Alpha" or "Beta").
