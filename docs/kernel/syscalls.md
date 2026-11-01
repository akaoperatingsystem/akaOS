# XSCore Kernel Syscall Architecture & ABI

The XSCore kernel provides a freestanding, 64-bit and 32-bit native system call interface independent of Linux. All syscalls follow architecture-specific register-passing conventions.

## Syscall Calling Conventions

- **x86-64**: Syscall number in `RAX`, arguments in `RDI`, `RSI`, `RDX`, `R10`, `R8`, `R9`. Return value in `RAX`.
- **x86-32**: Interrupt vector `0x80`, syscall number in `EAX`, arguments in `EBX`, `ECX`, `EDX`, `ESI`, `EDI`. Return value in `EAX`.
- **AArch64**: Instruction `svc #0`, syscall number in `X8`, arguments in `X0-X5`. Return value in `X0`.
- **ARM32**: Instruction `svc #0`, syscall number in `R7`, arguments in `R0-R4`. Return value in `R0`.
- **RISC-V (64 & 32)**: Instruction `ecall`, syscall number in `a7`, arguments in `a0-a5`. Return value in `a0`.

## Core Subsystems Supported
1. **Process & Thread Management**: `fork`, `execve`, `waitpid`, `exit`, `getpid`, `setuid`, `setgid`.
2. **Virtual Memory & Paging**: `mmap`, `munmap`, `mprotect`, demand paging, copy-on-write (COW).
3. **VFS & Storage**: `open`, `read`, `write`, `close`, `lseek`, `stat`, `dup2`, `pipe`, `poll`.
4. **Networking**: `socket`, `bind`, `connect`, `listen`, `accept`, `sendto`, `recvfrom`.
5. **Display & Input**: `/dev/fb0` memory mapping and `/dev/input/event0` evdev event stream.
