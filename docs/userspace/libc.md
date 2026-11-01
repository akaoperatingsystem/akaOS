# akaOS Native C Library (libc) Architecture

akaOS provides an original, freestanding C library and dynamic runtime designed from scratch to be independent of glibc or musl.

## Key Subsystems
- **Standard I/O (`stdio`)**: Formatted printing, buffered file operations, `popen`, `getline`.
- **POSIX System Calls**: File descriptors, pipes, directories (`opendir`, `readdir`), socket layer, signal handling.
- **Threading & Synchronization**: POSIX Threads (`pthread`), mutexes, condition variables, semaphores.
- **Dynamic Linking (`ld-akaos`)**: Shared object resolution (`dlopen`, `dlsym`), ELF relocations, thread-local storage (TLS).
