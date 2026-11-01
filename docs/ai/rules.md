# AI Agent Rules & Engineering Directives for akaOS

All AI agents, coding assistants (DeepSeek Harness, Claude, Gemini, Cursor, Copilot, etc.), and human contributors utilizing AI tooling on the **akaOS** codebase **MUST** strictly adhere to the rules outlined in this document.

---

## 1. Mandatory MIT License Compliance

- **Universal MIT Licensing**: Every single line of code, script, header, and implementation created or modified for akaOS and XSCore must be licensed under the **[MIT License](../../LICENSE)**.
- **Copyright Attribution**: Retain standard attribution:
  ```c
  /*
   * Copyright (c) 2026 akaOS Authors
   * SPDX-License-Identifier: MIT
   */
  ```
- **Zero Incompatible Code Ingestion**: Do **not** import, copy, or paste proprietary code, code under viral or incompatible copyleft licenses that cannot be combined under MIT terms, or unvetted snippets.

---

## 2. Upstream Desktop Purity Rule (Zero Patching)

- **Unmodified Upstream Desktop Software**: All desktop environments (GNOME, KDE Plasma, XFCE, MATE, Cinnamon, LXQt, LXDE) and window managers (Openbox, i3, KWin, Mutter, Xfwm4, Marco) must build and execute from **completely unmodified upstream source code**.
- **No Upstream Patching**: Never patch upstream desktop source code, never modify their branding, and never apply custom themes to upstream desktop codebases.
- **Compatibility Lives in akaOS**: If a desktop environment or window manager requires missing functionality, that compatibility must be implemented strictly inside akaOS's own subsystems:
  - Inside the **XSCore** kernel (syscalls, device drivers, signals, IPC).
  - Inside the **akaOS libc & C runtime** (POSIX interfaces, math, threading, dynamic loader).
  - Inside the **native X11 server (`xserver`)** or **Wayland bridge**.
  - Inside **D-Bus** and system services.

---

## 3. Independent Dual Versioning

akaOS (the operating system) and XSCore (the kernel) are independently versioned inside [`sdk/include/akaos/version.h`](../../sdk/include/akaos/version.h):
- **`XSCORE_VERSION` (Kernel)**: Increment whenever kernel code, hardware drivers, memory management, or syscall ABIs are modified (`xscore/`, `boot/`).
- **`AKAOS_VERSION` (Operating System)**: Increment whenever userspace applications, desktop components, window managers, libc, or tools are modified (`userspace/`, `tools/`).
- Always use semantic versioning (`MAJOR.MINOR.PATCH`). Never use date-based timestamps or codenames.
- **Badge Synchronization**: Always synchronize the version badges in **both [`README.md`](../../README.md)** and **[`PROGRESS.md`](../../PROGRESS.md)** whenever [`sdk/include/akaos/version.h`](../../sdk/include/akaos/version.h) is modified.

---

## 4. Real-Time Git Synchronization (Zero-Tolerance for Uncommitted Work)

- **Instant Push Rule**: AI agents must **never** perform file modifications in long loops while ignoring Git. Immediately after editing, creating, or finishing any file:
  ```powershell
  git add .
  git commit -m "<concise descriptive message with updated versions>"
  git push origin main
  ```
- **Windows Host Git**: Run Git operations from the host PowerShell (`pwsh`) to utilize the authenticated Windows Credential Manager. Do not push from inside WSL without configured credentials.
- **Strict `.gitignore` Enforcement**: Never track or push build output (`/build/`), test results (`/.test-results/`), internal scratch memory (`/AI/`), or logs (`*.log`).

---

## 5. Persistent AI Memory (`AI/` Directory)

- Before starting any task, read and parse all files in [`AI/`](../../AI/):
  - `AI/ARCHITECTURE.md`, `AI/CHANGELOG.md`, `AI/CONTEXT.md`, `AI/CURRENT_STATE.md`, `AI/DECISIONS.md`, `AI/DEVELOPMENT.md`, `AI/HANDOFF.md`, and `AI/TODO.md`.
- After every milestone or architectural change, update `AI/HANDOFF.md`, `AI/CHANGELOG.md`, and `AI/TODO.md` to preserve context for subsequent continuation rounds.

---

## 6. Execution Hygiene & Verification

- **Non-Interactive Commands**: Always use non-interactive, bounded commands with explicit timeouts. Avoid hanging in interactive prompts.
- **Automated Verification**: Validate all changes through QEMU regression scripts (e.g. `./test.sh`, `./tools/test-upstream-wm.sh`, `./tools/build-arch-smoke.sh --all`) before concluding work.
