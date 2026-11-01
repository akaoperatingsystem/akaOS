# akaOS Native Display Server & Desktop Infrastructure

akaOS implements a native X11 protocol server (`xserver`) and compositor (`lumen`) allowing unmodified X11 applications and desktop environments to execute natively on bare metal or QEMU.

## Architecture

```text
+-------------------------------------------------------------+
|    Desktop Environments: GNOME / KDE / XFCE / MATE / LXDE   |
+-------------------------------------------------------------+
         |                                           |
         v (X11 Protocol: :0)                       v (Wayland Protocols)
+-------------------------------+           +-------------------------+
|     akaOS X11 Server (xserver)|           |   Wayland Bridge Layer  |
+-------------------------------+           +-------------------------+
         |                                           |
         +--------------------+----------------------+
                              v
             +----------------------------------+
             |    Lumen Compositor Engine       |
             +----------------------------------+
                              |
             +----------------v-----------------+
             |   XSCore Kernel Display HAL      |
             |       (/dev/fb0, /dev/input)     |
             +----------------------------------+
```

## Supported X11 Extensions
- **SHAPE**: Window boundary shaping for irregular window frames and widgets.
- **X-Resource (X-RES)**: Client resource monitoring and allocation tracking for desktop panels.
- **BIG-REQUESTS**: Extended packet payloads for image transfers and large protocol requests.
- **XC-MISC**: XID recycling and allocation ranges.
