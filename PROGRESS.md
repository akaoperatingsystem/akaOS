# akaOS Development Progress

[![akaOS Version](https://img.shields.io/badge/akaOS-v0.1.46-blue.svg)](sdk/include/akaos/version.h)
[![XSCore Version](https://img.shields.io/badge/XSCore-v0.1.20-green.svg)](sdk/include/akaos/version.h)

This document tracks the live implementation progress, hardware readiness, subsystem maturity, and desktop environment compatibility for **akaOS** and the **XSCore** kernel.

---

## Supported CPU Architectures

| Architecture | Port Status & Kernel Target | Readiness | Progress Bar |
| :--- | :--- | :--- | :--- |
| **x86-64 (AMD64)** | Full Image Boot-Tested (UEFI GOP & BIOS Multiboot2, SMP, PCI, VirtIO) | 98% | `[██████████]` 98% |
| **x86-32 (i386)** | Full Image Boot-Tested (UEFI FAT32 & BIOS ATA/IDE Persistence) | 92% | `[█████████░]` 92% |
| **AArch64 (ARM 64-bit)** | Native Kernel & Userspace Boot-Tested (MMU, Exceptions, VirtIO-MMIO) | 88% | `[████████░░]` 88% |
| **ARM32 (ARMv7-A)** | Native Kernel & Userspace Boot-Tested (MMU, Direct Kernel QEMU `virt`) | 85% | `[████████░░]` 85% |
| **RISC-V64 (RV64GC)** | Native Kernel & Userspace Boot-Tested (Sv39 Paging, VirtIO-MMIO) | 85% | `[████████░░]` 85% |
| **RISC-V32 (RV32GC)** | Native Kernel & Userspace Boot-Tested (Sv32 Paging, Direct Kernel QEMU `virt`) | 82% | `[████████░░]` 82% |

---

## System Subsystems & Hardware Drivers

| Target Subsystem | Scope / Implementation | Readiness | Progress Bar |
| :--- | :--- | :--- | :--- |
| **Memory Management** | 4-Level Paging, 131K Refcount Slots, 2048-Page Cache, Dynamic Stack Growth, 32MB Heap | 100% | `[██████████]` 100% |
| **Process & Scheduling** | Preemptive Multitasking, Local APIC Timer, POSIX Fork/Execve, Stack COW Fault Isolation | 94% | `[█████████░]` 94% |
| **GPU & Display Drivers** | Framebuffer (`/dev/fb0`), Lumen, X11 Server (64MB Backing, XYBitmap/PutImage, SHAPE, X-Res), Mode Query | 94% | `[█████████░]` 94% |
| **Storage & akaFS Overlay** | VirtIO-Block, IDE/ATA PIO Fallback, Persistent Flash Overlay VFS | 85% | `[████████░░]` 85% |
| **Native libc & Linker** | Freestanding C Runtime, Dynamic Linker (`ld-akaos`), POSIX pthread_atfork Canonical Guards | 90% | `[█████████░]` 90% |
| **Networking Stack** | VirtIO-Net, IPv4/ARP/UDP/TCP Sockets, DNS Resolver, HTTP Server | 80% | `[████████░░]` 80% |
| **Audio Subsystem** | AC97 / Intel HDA Sound Driver Foundation, `/dev/audio` Device Stream | 70% | `[███████░░░]` 70% |

---

## Desktop Environments & Window Managers

All desktop environments and window managers run as **completely unmodified upstream releases**; all required compatibility is implemented directly within akaOS subsystems.

| Desktop / Window Manager | Upstream Version | Readiness | Progress Bar |
| :--- | :--- | :--- | :--- |
| **Openbox WM** | Unmodified Upstream 3.6.1 | 100% | `[██████████]` 100% |
| **LXDE (Openbox + LXPanel + PCManFM)** | Unmodified Upstream | 100% | `[██████████]` 100% |
| **LXQt Desktop & Session** | Unmodified Upstream | 65% | `[██████░░░░]` 65% |
| **XFCE Desktop (Xfwm4 + xfce4-panel + xfce4-session)** | Unmodified Upstream 4.18 | 100% | `[██████████]` 100% |
| **MATE Desktop & Marco** | Unmodified Upstream (Marco 1.26.2, libcanberra 0.30, libogg 1.3, libvorbis 1.3) | 70% | `[███████░░░]` 70% |
| **KDE Plasma & KWin** | Unmodified Upstream | 25% | `[██░░░░░░░░]` 25% |
| **GNOME Desktop & Mutter** | Unmodified Upstream | 20% | `[██░░░░░░░░]` 20% |
| **Cinnamon Desktop & Muffin** | Unmodified Upstream | 20% | `[██░░░░░░░░]` 20% |
| **Overall System Completion** | Full Native Operating System | 75% | `[████████░░]` 75% |


---

## GPU Hardware Drivers & Target Support Matrix

This matrix tracks the hardware driver support and acceleration milestones across NVIDIA, AMD/ATI, and Intel graphics processors inside the **XSCore** kernel driver framework and display subsystem.

### GPU Subsystem Summary

| GPU Family / Vendor | Architecture Range | Target Readiness | Progress Bar |
| :--- | :--- | :--- | :--- |
| **Generic & Virtual GPUs** | UEFI GOP, VESA, Bochs-VGA, VirtIO-GPU, QEMU Standard VGA | 95% | `[█████████░]` 95% |
| **NVIDIA Corporation** | NV1 through Blackwell / Rubin (Consumer, Workstation, Data Center) | 42% | `[████░░░░░░]` 42% |
| **ATI Technologies & AMD** | Mach64 through RDNA 4 / CDNA 4 (Radeon, APU, FirePro, Instinct) | 45% | `[████░░░░░░]` 45% |
| **Intel Corporation** | i740 through Arc Alchemist / Battlemage / Celestial (GMA, HD, Iris, Xe) | 50% | `[█████░░░░░]` 50% |

---

### 1. NVIDIA Corporation GPU Drivers

#### 1.1 Pre-GeForce & Consumer Desktop / Mobile GeForce Series

* **Pre-GeForce Era (NV1 – NV5)**: `[████████░░]` 80%
  * NVIDIA NV1 (STG-2000)
  * NVIDIA RIVA 128 (NV3), NVIDIA RIVA 128ZX
  * NVIDIA RIVA TNT (NV4)
  * NVIDIA RIVA TNT2, RIVA TNT2 M64, RIVA TNT2 Vanta, RIVA TNT2 Pro, RIVA TNT2 Ultra
* **GeForce 256 Series (NV10)**: `[████████░░]` 80%
  * NVIDIA GeForce 256 SDR, NVIDIA GeForce 256 DDR
* **GeForce 2 Series (NV11 / NV15 / NV16)**: `[████████░░]` 80%
  * NVIDIA GeForce2 MX, GeForce2 MX 100, GeForce2 MX 200, GeForce2 MX 400
  * NVIDIA GeForce2 GTS, GeForce2 Pro, GeForce2 Ti, GeForce2 Ultra
  * **Mobile:** NVIDIA GeForce2 Go, GeForce2 Go 100, GeForce2 Go 200
* **GeForce 3 Series (NV20)**: `[███████░░░]` 75%
  * NVIDIA GeForce3, GeForce3 Ti 200, GeForce3 Ti 500
* **GeForce 4 Series (NV17 / NV18 / NV25 / NV28)**: `[███████░░░]` 75%
  * NVIDIA GeForce4 MX 420, GeForce4 MX 440, GeForce4 MX 440-SE, GeForce4 MX 440 8x, GeForce4 MX 460, GeForce4 MX 4000, GeForce PCX 4300
  * NVIDIA GeForce4 Ti 4200, GeForce4 Ti 4200 8x, GeForce4 Ti 4400, GeForce4 Ti 4600, GeForce4 Ti 4800, GeForce4 Ti 4800 SE
  * **Mobile:** NVIDIA GeForce4 410 Go, 420 Go, 440 Go, 460 Go, 488 Go, GeForce4 4200 Go
* **GeForce FX (5) Series (NV30 – NV38)**: `[██████░░░░]` 65%
  * NVIDIA GeForce FX 5100, GeForce FX 5200, GeForce FX 5200 LE, GeForce FX 5200 Ultra, GeForce PCX 5300
  * NVIDIA GeForce FX 5500, GeForce FX 5600, GeForce FX 5600 XT, GeForce FX 5600 Ultra, GeForce FX 5600 Ultra Rev.2
  * NVIDIA GeForce FX 5700, GeForce FX 5700 LE, GeForce FX 5700 VE, GeForce FX 5700 Ultra, GeForce FX 5700 Ultra GDDR3, GeForce PCX 5750
  * NVIDIA GeForce FX 5800, GeForce FX 5800 Ultra
  * NVIDIA GeForce FX 5900, GeForce FX 5900 ZT, GeForce FX 5900 XT, GeForce FX 5900 Ultra, GeForce PCX 5900
  * NVIDIA GeForce FX 5950 Ultra, GeForce PCX 5950
  * **Mobile:** NVIDIA GeForce FX Go 5100, Go 5200, Go 5300, Go 5600, Go 5650, Go 5700
* **GeForce 6 Series (NV40 – NV48)**: `[██████░░░░]` 60%
  * NVIDIA GeForce 6200, GeForce 6200 TurboCache, GeForce 6200 SE TurboCache, GeForce 6200 LE, GeForce 6200 A-LE
  * NVIDIA GeForce 6500, GeForce 6600, GeForce 6600 LE, GeForce 6600 VE, GeForce 6600 DDR2, GeForce 6600 GT
  * NVIDIA GeForce 6700 XL, GeForce 6800, GeForce 6800 LE, GeForce 6800 XT, GeForce 6800 XE, GeForce 6800 GTO, GeForce 6800 GS, GeForce 6800 GT, GeForce 6800 Ultra, GeForce 6800 Ultra Extreme
  * **Mobile:** NVIDIA GeForce Go 6200, Go 6250, Go 6400, Go 6600, Go 6600 TE, Go 6600 GT, Go 6800, Go 6800 Ultra
* **GeForce 7 Series (G70 – G73)**: `[██████░░░░]` 60%
  * NVIDIA GeForce 7100 GS, GeForce 7200 GS, GeForce 7300 SE, GeForce 7300 LE, GeForce 7300 GS, GeForce 7300 GT, GeForce 7500 LE
  * NVIDIA GeForce 7600 GS, GeForce 7600 GT, GeForce 7650 GS, GeForce 7800 GS, GeForce 7800 GT, GeForce 7800 GTX, GeForce 7800 GTX 512
  * NVIDIA GeForce 7900 GS, GeForce 7900 GT, GeForce 7900 GTO, GeForce 7900 GTX, GeForce 7900 GX2, GeForce 7950 GT, GeForce 7950 GX2
  * **Mobile:** NVIDIA GeForce Go 7200, Go 7300, Go 7400, Go 7600, Go 7600 GT, Go 7700, Go 7800, Go 7800 GTX, Go 7900 GS, Go 7900 GTX, Go 7950 GTX
* **GeForce 8 Series (Tesla Architecture: G80 – G98)**: `[█████░░░░░]` 55%
  * NVIDIA GeForce 8300 GS, GeForce 8400 GS, GeForce 8400 GS Rev.2, GeForce 8400 GS Rev.3, GeForce 8500 GT
  * NVIDIA GeForce 8600 GS, GeForce 8600 GT, GeForce 8600 GTS
  * NVIDIA GeForce 8800 GS, GeForce 8800 GTS (320MB / 640MB), GeForce 8800 GTS 512 (G92), GeForce 8800 GT, GeForce 8800 GTX, GeForce 8800 Ultra
  * **Mobile:** NVIDIA GeForce 8200M G, 8400M G, 8400M GS, 8400M GT, 8600M GS, 8600M GT, 8700M GT, 8800M GTS, 8800M GTX
* **GeForce 9 Series (Tesla Architecture: G92 – G98)**: `[█████░░░░░]` 55%
  * NVIDIA GeForce 9300 GE, GeForce 9300 GS, GeForce 9300 SE, GeForce 9400 GT, GeForce 9500 GS, GeForce 9500 GT
  * NVIDIA GeForce 9600 GSO, GeForce 9600 GSO 512, GeForce 9600 GT, GeForce 9600 GT Green Edition
  * NVIDIA GeForce 9800 GT, GeForce 9800 GTX, GeForce 9800 GTX+, GeForce 9800 GX2
  * **Mobile:** NVIDIA GeForce 9100M G, 9200M GS, 9300M G, 9300M GS, 9400M G, 9500M G, 9500M GS, 9600M GS, 9600M GT, 9650M GS, 9650M GT, 9700M GT, 9700M GTS, 9800M GS, 9800M GT, 9800M GTS, 9800M GTX
* **GeForce 100 / 200 / 300 Series (Tesla Refresh & GT200 / GT21x)**: `[█████░░░░░]` 50%
  * **100 Series (OEM):** NVIDIA GeForce G 100, GeForce GT 120, GeForce GT 130, GeForce GT 140, GeForce GTS 150
  * **100M Series:** NVIDIA GeForce G 102M, G 103M, G 105M, G 110M, GT 120M, GT 130M, GTS 150M, GTS 160M
  * **200 Series:** NVIDIA GeForce 205, GeForce 210, GeForce GT 220, GeForce GT 230, GeForce GT 240, GeForce GTS 240, GeForce GTS 250, GeForce GTX 260, GeForce GTX 260 Core 216, GeForce GTX 275, GeForce GTX 280, GeForce GTX 285, GeForce GTX 295
  * **200M Series:** NVIDIA GeForce G210M, GT 220M, GT 230M, GT 240M, GTS 250M, GTS 260M, GTX 260M, GTX 280M, GTX 285M
  * **300 Series (OEM):** NVIDIA GeForce 310, GeForce 315, GeForce GT 320, GeForce GT 330, GeForce GT 340
  * **300M Series:** NVIDIA GeForce 305M, 310M, 315M, GT 320M, GT 325M, GT 330M, GT 335M, GTS 350M, GTS 360M
* **GeForce 400 / 500 Series (Fermi Architecture: GF100 – GF119)**: `[████░░░░░░]` 45%
  * **400 Series:** NVIDIA GeForce 405, GeForce GT 420, GeForce GT 430, GeForce GT 440, GeForce GTS 450, GeForce GTX 460 SE, GeForce GTX 460 (768MB / 1GB), GeForce GTX 460 v2, GeForce GTX 465, GeForce GTX 470, GeForce GTX 480
  * **400M Series:** NVIDIA GeForce GT 415M, GT 420M, GT 425M, GT 435M, GT 445M, GTX 460M, GTX 470M, GTX 480M, GTX 485M
  * **500 Series:** NVIDIA GeForce 510, GeForce GT 520, GeForce GT 530, GeForce GT 545, GeForce GTX 550 Ti, GeForce GTX 555, GeForce GTX 560 SE, GeForce GTX 560, GeForce GTX 560 Ti, GeForce GTX 560 Ti 448 Cores, GeForce GTX 570, GeForce GTX 580, GeForce GTX 590
  * **500M Series:** NVIDIA GeForce GT 520M, GT 520MX, GT 525M, GT 540M, GT 550M, GT 555M, GTX 560M, GTX 570M, GTX 580M
* **GeForce 600 / 700 / 800M Series (Kepler & Early Maxwell)**: `[████░░░░░░]` 45%
  * **600 Series:** NVIDIA GeForce 605, GeForce GT 610, GeForce GT 620, GeForce GT 625, GeForce GT 630, GeForce GT 635, GeForce GT 640, GeForce GT 645, GeForce GTX 645, GeForce GTX 650, GeForce GTX 650 Ti, GeForce GTX 650 Ti Boost, GeForce GTX 660, GeForce GTX 660 Ti, GeForce GTX 670, GeForce GTX 680, GeForce GTX 690
  * **600M Series:** NVIDIA GeForce 610M, GT 620M, GT 625M, GT 630M, GT 635M, GT 640M, GT 640M LE, GT 645M, GT 650M, GTX 660M, GTX 670M, GTX 670MX, GTX 675M, GTX 675MX, GTX 680M, GTX 680MX
  * **700 Series:** NVIDIA GeForce GT 705, GeForce GT 710, GeForce GT 720, GeForce GT 730, GeForce GT 740, GeForce GTX 745, GeForce GTX 750, GeForce GTX 750 Ti, GeForce GTX 760, GeForce GTX 760 192-bit, GeForce GTX 760 Ti, GeForce GTX 770, GeForce GTX 780, GeForce GTX 780 Ti, NVIDIA GeForce GTX TITAN, GeForce GTX TITAN Black, GeForce GTX TITAN Z
  * **700M Series:** NVIDIA GeForce 705M, 710M, GT 720M, GT 730M, GT 735M, GT 740M, GT 745M, GT 750M, GT 755M, GTX 760M, GTX 765M, GTX 770M, GTX 780M
  * **800M Series:** NVIDIA GeForce 800M, 810M, 820M, 825M, 830M, 840M, 845M, GTX 850M, GTX 860M, GTX 870M, GTX 880M
* **GeForce 900 Series (Maxwell Architecture: GM107 – GM200)**: `[████░░░░░░]` 40%
  * **Desktop:** NVIDIA GeForce GT 945A, GeForce GTX 950, GeForce GTX 960, GeForce GTX 970, GeForce GTX 980, GeForce GTX 980 Ti, NVIDIA GeForce GTX TITAN X
  * **Mobile:** NVIDIA GeForce 910M, 920M, 920MX, 930M, 930MX, 940M, 940MX, 945M, GTX 950M, GTX 960M, GTX 965M, GTX 970M, GTX 980M, GeForce GTX 980 (Notebook)
* **GeForce 10 Series (Pascal Architecture: GP102 – GP108)**: `[████░░░░░░]` 40%
  * **Desktop:** NVIDIA GeForce GT 1010, GeForce GT 1030 (GDDR5 / DDR4), GeForce GTX 1050 (2GB / 3GB), GeForce GTX 1050 Ti, GeForce GTX 1060 (3GB / 5GB / 6GB / GDDR5X), GeForce GTX 1070, GeForce GTX 1070 Ti, GeForce GTX 1080, GeForce GTX 1080 Ti, NVIDIA TITAN X (Pascal), NVIDIA TITAN Xp
  * **Mobile:** NVIDIA GeForce MX110, GeForce MX130, GeForce MX150, GeForce GTX 1050 Mobile, GTX 1050 Ti Mobile, GTX 1060 Mobile, GTX 1070 Mobile, GTX 1080 Mobile
* **GeForce 16 & 20 Series (Turing Architecture & Volta TITAN)**: `[████░░░░░░]` 38%
  * **Volta:** NVIDIA TITAN V, NVIDIA TITAN V CEO Edition
  * **16 Series:** NVIDIA GeForce GTX 1630, GeForce GTX 1650 (GDDR5 / GDDR6 / TU106 / TU116), GeForce GTX 1650 SUPER, GeForce GTX 1650 Ti (Mobile), GeForce GTX 1660, GeForce GTX 1660 SUPER, GeForce GTX 1660 Ti
  * **MX Series:** NVIDIA GeForce MX230, MX250, MX330, MX350, MX450, MX550, MX570, MX570 A
  * **20 Series:** NVIDIA GeForce RTX 2050 (Mobile), GeForce RTX 2060 (6GB / 12GB), GeForce RTX 2060 SUPER, GeForce RTX 2070, GeForce RTX 2070 SUPER, GeForce RTX 2080, GeForce RTX 2080 SUPER, GeForce RTX 2080 Ti, NVIDIA TITAN RTX
* **GeForce RTX 30 Series (Ampere Architecture: GA102 – GA107)**: `[███░░░░░░░]` 35%
  * **Desktop:** NVIDIA GeForce RTX 3050 (6GB / 8GB), GeForce RTX 3060 (8GB / 12GB), GeForce RTX 3060 Ti, GeForce RTX 3070, GeForce RTX 3070 Ti, GeForce RTX 3080 (10GB / 12GB), GeForce RTX 3080 Ti, GeForce RTX 3090, GeForce RTX 3090 Ti
  * **Laptop:** NVIDIA GeForce RTX 3050, 3050 Ti, 3060, 3070, 3070 Ti, 3080 (8GB / 16GB), 3080 Ti Laptop GPU
* **GeForce RTX 40 Series (Ada Lovelace: AD102 – AD107)**: `[███░░░░░░░]` 30%
  * **Desktop:** NVIDIA GeForce RTX 4060, GeForce RTX 4060 Ti (8GB / 16GB), GeForce RTX 4070 (GDDR6X / GDDR6), GeForce RTX 4070 SUPER, GeForce RTX 4070 Ti, GeForce RTX 4070 Ti SUPER, GeForce RTX 4080, GeForce RTX 4080 SUPER, GeForce RTX 4090 D, GeForce RTX 4090
  * **Laptop:** NVIDIA GeForce RTX 4050, 4060, 4070, 4080, 4090 Laptop GPU
* **GeForce RTX 50 Series (Blackwell Architecture: GB202 – GB207)**: `[██░░░░░░░░]` 25%
  * **Desktop:** NVIDIA GeForce RTX 5050, GeForce RTX 5060, GeForce RTX 5060 Ti, GeForce RTX 5070, GeForce RTX 5070 Ti, GeForce RTX 5080, GeForce RTX 5090 D, GeForce RTX 5090
  * **Laptop:** NVIDIA GeForce RTX 5050, 5060, 5070, 5070 Ti, 5080, 5090 Laptop GPU

#### 1.2 NVIDIA Integrated GPUs & SoCs
* **nForce Chipset iGPUs**: `[███████░░░]` 70%
  * NVIDIA nForce 220/415/420 (GeForce2 MX IGP), nForce2 IGP (GeForce4 MX IGP), GeForce 6100, GeForce 6150, GeForce 6150 SE, GeForce 6150 LE, GeForce 7025, GeForce 7050, GeForce 7100, GeForce 7150, GeForce 8100, GeForce 8200, GeForce 8300, GeForce 9100M G, GeForce 9300, GeForce 9400, GeForce 9400M (ION), NVIDIA ION, NVIDIA ION 2
* **Tegra & Jetson SoCs**: `[████░░░░░░]` 40%
  * NVIDIA Tegra APX 2500, Tegra 600, Tegra 2, Tegra 3, Tegra 4, Tegra K1 (Kepler), Tegra X1 / X1+ (Maxwell), Tegra X2 (Pascal), Tegra Xavier (Volta), Tegra Orin (Ampere), Tegra T239, Jetson Thor (Blackwell)
* **ARM PC & Workstation APU SoCs**: `[██░░░░░░░░]` 20%
  * NVIDIA GB10 Grace Blackwell Superchip (DGX Spark iGPU), NVIDIA N1 / N1X AI PC SoC (Blackwell iGPU)

#### 1.3 NVIDIA Workstation GPUs (Quadro & RTX Professional)
* **Legacy Quadro & FX Series**: `[███████░░░]` 70%
  * Quadro, Quadro2 MXR/EX/Pro, Quadro DCC, Quadro4 200/400 NVS, Quadro4 XGL series
  * Quadro FX 330, 350, 370, 380, 500, 540, 550, 560, 570, 580, 600, 700, 1000, 1100, 1300, 1400, 1500, 1700, 1800, 2000, 3000, 3400, 3500, 3700, 3800, 4000, 4500, 4600, 4800, 5500, 5600, 5800
* **Quadro NVS Multi-Display**: `[██████░░░░]` 65%
  * NVS 50, 100, 200, 280, 285, 290, 295, 300, 310, 315, 400, 420, 440, 450, 510, 810
* **Quadro Fermi / Kepler / Maxwell / Pascal**: `[█████░░░░░]` 50%
  * Quadro 400, 600, 2000, 4000, 5000, 6000, 7000
  * Quadro K420, K600, K620, K1200, K2000, K2200, K4000, K4200, K5000, K5200, K6000
  * Quadro M2000, M4000, M5000, M6000
  * Quadro P400, P600, P620, P1000, P2000, P2200, P4000, P5000, P6000, GP100
* **Quadro Volta & Turing (RTX Series)**: `[████░░░░░░]` 40%
  * Quadro GV100, Quadro T400, T600, T1000, Quadro RTX 3000, RTX 4000, RTX 5000, RTX 6000, RTX 8000
* **NVIDIA RTX Ampere / Ada / Blackwell Pro**: `[███░░░░░░░]` 30%
  * RTX A400, A1000, A2000, A4000, A4500, A5000, A5500, A6000
  * RTX 2000 Ada, RTX 4000 Ada, RTX 4000 SFF Ada, RTX 4500 Ada, RTX 5000 Ada, RTX 5880 Ada, RTX 6000 Ada
  * RTX PRO 2000, 4000, 4000 SFF, 4500, 5000, 6000 Blackwell Workstation & Server Edition

#### 1.4 NVIDIA Server, Data Center & AI GPUs
* **Tesla Legacy & Fermi / Kepler / Maxwell**: `[█████░░░░░]` 50%
  * Tesla C870, D870, S870, C1060, S1070, M1060, C2050, C2070, C2075, M2050, M2070, M2090, K10, K20, K20X, K40, K80, M4, M10, M40, M60
* **Pascal / Volta / Turing Data Center**: `[████░░░░░░]` 40%
  * Tesla P4, P6, P40, P100 (PCIe / SXM2), Tesla V100 (16GB / 32GB / V100S), Tesla T4, T40
* **CMP Cryptocurrency Processors**: `[████░░░░░░]` 40%
  * P106-90, P106-100, P104-100, P102-100, CMP 30HX, 40HX, 50HX, 70HX, 90HX, 170HX
* **Ampere & Ada Data Center & AI**: `[███░░░░░░░]` 30%
  * A2, A10, A10G, A16, A30, A40, A100 (40GB / 80GB), A800, AX800, L2, L4, L20, L40, L40S
* **Hopper / Blackwell / Rubin AI Superchips**: `[██░░░░░░░░]` 20%
  * H100 (PCIe / SXM5 / NVL), H800, H20, H200, GH200 Grace Hopper Superchip
  * B100, B200, B300 (Blackwell Ultra), GB200 Grace Blackwell Superchip, GB200 NVL72, GB300 NVL72
  * R100, R200, Vera Rubin Superchip, Vera Rubin NVL144 / NVL576

---

### 2. ATI Technologies & AMD GPU Drivers

#### 2.1 Pre-Radeon & Consumer Desktop / Mobile Radeon Series
* **Pre-Radeon Era (Mach & Rage)**: `[████████░░]` 85%
  * ATI Mach8, Mach32, Mach64, 3D Rage, 3D Rage II, 3D Rage Pro, Rage LT Pro, Rage XL, Rage 128, Rage 128 Pro, Rage Fury MAXX
* **Radeon R100 / R200 / R300 / R400 / R500**: `[███████░░░]` 75%
  * Radeon DDR, SDR, LE, VE, 7000, 7200, 7500
  * Radeon 8500, 8500 LE, 8500DV, 9000, 9000 Pro, 9100, 9200, 9250
  * Radeon 9500, 9550, 9600, 9700, 9800 (SE / Pro / XT)
  * Radeon X300, X550, X600, X700, X800, X850 (Pro / XT / PE), X1050
  * Radeon X1300, X1550, X1600, X1650, X1800, X1900, X1950 (GT / Pro / XT / XTX)
* **TeraScale 1, 2, 3 (HD 2000 – HD 6000)**: `[██████░░░░]` 60%
  * HD 2350, HD 2400, HD 2600, HD 2900 (Pro / XT)
  * HD 3450, HD 3650, HD 3850, HD 3870, HD 3870 X2
  * HD 4350, HD 4550, HD 4650, HD 4670, HD 4770, HD 4850, HD 4870, HD 4890, HD 4870 X2
  * HD 5450, HD 5570, HD 5670, HD 5750, HD 5770, HD 5850, HD 5870, HD 5970
  * HD 6450, HD 6570, HD 6670, HD 6770, HD 6790, HD 6850, HD 6870, HD 6950, HD 6970, HD 6990
* **GCN 1 – GCN 5 (HD 7000 – HD 8000 & Rx 200 – Rx 500 / Vega / VII)**: `[█████░░░░░]` 50%
  * HD 7750, HD 7770, HD 7850, HD 7870, HD 7950, HD 7970, HD 7990
  * R5 230, R7 240, R7 250, R7 260X, R9 270X, R9 280X, R9 290X, R9 295X2
  * R7 360, R7 370, R9 380, R9 390, R9 390X, R9 Nano, R9 Fury, R9 Fury X, Pro Duo
  * RX 460, RX 470, RX 480
  * RX 550, RX 560, RX 570, RX 580, RX 590
  * RX Vega 56, RX Vega 64, Vega Liquid, Radeon VII (7nm Vega 20)
* **RDNA 1 & RDNA 2 (RX 5000 & RX 6000)**: `[████░░░░░░]` 40%
  * RX 5300, RX 5500, RX 5500 XT, RX 5600 XT, RX 5700, RX 5700 XT
  * RX 6400, RX 6500 XT, RX 6600, RX 6600 XT, RX 6650 XT, RX 6700 XT, RX 6750 XT, RX 6800, RX 6800 XT, RX 6900 XT, RX 6950 XT
* **RDNA 3 & RDNA 4 (RX 7000 & RX 9000)**: `[███░░░░░░░]` 30%
  * RX 7600, RX 7600 XT, RX 7700 XT, RX 7800 XT, RX 7900 GRE, RX 7900 XT, RX 7900 XTX
  * RX 9060, RX 9060 XT, RX 9070 GRE, RX 9070, RX 9070 XT

#### 2.2 ATI & AMD Integrated GPUs & APUs
* **Chipset IGPs & Early Fusion APUs**: `[██████░░░░]` 65%
  * Radeon Xpress 200/1100/1150/1250, Radeon 2100, HD 3100/3200/3300/4200/4250/4290
  * HD 6250, 6310, 6410D, 6530D, 6550D, 7480D, 7560D, 7660D, 8370D, 8670D
* **GCN & Vega APUs (Ryzen 2000G – 5000G)**: `[█████░░░░░]` 50%
  * Radeon R2/R3/R4/R5/R6/R7 Integrated Graphics
  * Radeon Vega 3, Vega 6, Vega 7, Vega 8, Vega 9, Vega 10, RX Vega 11
* **RDNA 2 / 3 / 3.5 APUs (Ryzen 6000 – Ryzen AI 300 / Strix Halo)**: `[███░░░░░░░]` 35%
  * Radeon 610M, 660M, 680M, Radeon 740M, 760M, 780M
  * Radeon 840M, 860M, 880M, 890M, Radeon 8040S, 8050S, 8060S (40CU Strix Halo)

#### 2.3 ATI & AMD Workstation GPUs
* **FireGL & FireMV**: `[███████░░░]` 75%
  * FireGL 8700, 8800, T2-128, X1, X2, X3, Z1, V3100 – V8650; FireMV 2200 – 2450
* **FirePro V & W Series**: `[█████░░░░░]` 55%
  * FirePro V3700 – V9800, FirePro W2100 – W9100, FirePro D300/D500/D700
* **Radeon Pro WX, Vega & RDNA (W5000 – W7000 & AI PRO R9700)**: `[████░░░░░░]` 40%
  * Radeon Pro WX 2100 – WX 9100, Radeon Vega Frontier Edition, Radeon Pro SSG
  * Radeon Pro W5500/W5700, W6400/W6600/W6800/W6900X, W7500/W7600/W7700/W7800/W7900, Radeon AI PRO R9700

#### 2.4 ATI & AMD Server, Cloud & AI Data Center GPUs
* **FireStream & FirePro S-Series**: `[█████░░░░░]` 50%
  * FireStream 9170 – 9370, FirePro S4000X – S10000, Radeon Sky 500/700/900
* **Radeon Instinct & AMD Instinct CDNA 1 / 2 / 3 / 4**: `[███░░░░░░░]` 30%
  * Instinct MI6, MI8, MI25, MI50, MI60
  * Instinct MI100 (CDNA 1), MI210, MI250, MI250X (CDNA 2)
  * Instinct MI300A, MI300X, MI325X (CDNA 3)
  * Instinct MI350X, MI355X (CDNA 4)

---

### 3. Intel Corporation GPU Drivers

#### 3.1 Early Discrete GPUs & Xeon Phi
* **Discrete & MIC Accelerators**: `[██████░░░░]` 60%
  * Intel740 (i740), Intel752 (i752)
  * Intel Xeon Phi (Knights Corner, Knights Landing, Knights Mill)
  * Intel Iris Xe MAX Graphics (DG1 Mobile), Intel Iris Xe Graphics (DG1 Desktop OEM)

#### 3.2 Intel Integrated GPUs (iGPUs)
* **Intel Extreme Graphics & GMA Series**: `[███████░░░]` 75%
  * Intel 810 / 815 Graphics, Extreme Graphics 1 & 2
  * GMA 900, GMA 950, GMA 3000, GMA 3100, GMA X3000, GMA X3100, GMA X3500, GMA 4500, GMA X4500HD, GMA 3150, GMA 500, GMA 600, GMA 3600
* **Intel HD / Iris / Iris Pro (Gen5 – Gen9)**: `[██████░░░░]` 65%
  * Gen5 (Ironlake), Gen6 (Sandy Bridge HD 2000/3000), Gen7 (Ivy Bridge HD 2500/4000)
  * Gen7.5 (Haswell HD 4400/4600, Iris 5100, Iris Pro 5200)
  * Gen8 (Broadwell HD 5500/6000, Iris 6100, Iris Pro 6200)
  * Gen9 (Skylake HD 520/530, Iris 540/550, Iris Pro 580)
* **Intel UHD / Iris Plus / Iris Xe (Gen9.5 – Gen12.2)**: `[█████░░░░░]` 55%
  * Gen9.5 (Kaby Lake / Coffee Lake / Comet Lake UHD 620/630, Iris Plus 640/650)
  * Gen11 (Ice Lake UHD G1, Iris Plus G4/G7)
  * Gen12 (Tiger Lake / Alder Lake / Raptor Lake UHD 730/770, Iris Xe G7 80EU/96EU)
* **Intel Arc Integrated Graphics (Xe-LPG, Xe2-LPG, Xe3)**: `[███░░░░░░░]` 35%
  * Meteor Lake & Arrow Lake (Arc 7/8 Xe-cores, Arc 130T/140T)
  * Lunar Lake (Xe2 Battlemage Arc 130V/140V)
  * Panther Lake (Xe3 Celestial Arc B390 iGPU)

#### 3.3 Intel Arc Consumer Discrete GPUs
* **Arc A-Series (Alchemist / Xe-HPG)**: `[████░░░░░░]` 45%
  * **Desktop:** Intel Arc A310, Arc A380, Arc A580, Arc A750, Arc A770 (8GB / 16GB)
  * **Mobile:** Intel Arc A350M, A370M, A530M, A550M, A570M, A730M, A770M
* **Arc B-Series (Battlemage / Xe2-HPG)**: `[███░░░░░░░]` 35%
  * **Desktop:** Intel Arc B570 (10GB), Intel Arc B580 (12GB)

#### 3.4 Intel Workstation, Server & AI Data Center GPUs
* **Workstation Arc Pro Series**: `[████░░░░░░]` 40%
  * Arc Pro A30M, A40, A50, A60, A60M (Alchemist Pro)
  * Arc Pro B50 (16GB), Arc Pro B60 (24GB) (Battlemage Pro)
* **Server & Cloud Visual GPUs**: `[████░░░░░░]` 40%
  * Server GPU SG1 (4x DG1)
  * Data Center GPU Flex 140, Flex 170, Flex 170V (Arctic Sound-M)
* **HPC & AI Data Center Accelerators**: `[██░░░░░░░░]` 25%
  * Data Center GPU Max 1100, 1350, 1550 (Ponte Vecchio Xe-HPC)
  * Habana Labs AI: Goya HL-100, Greco HL-1000, Gaudi (HL-205), Gaudi 2 (HL-225), Gaudi 3 (128GB HBM2e)
  * Crescent Island Data Center GPU (Xe3P)


---

# Comprehensive Global CPU & SoC Model Directory

This directory registers all certified, recognized, and targeted microprocessors, mobile application processors, server silicon, and edge SoCs across the **XSCore** CPU detection, topology enumeration, and hardware HAL layers.

### CPU Architecture & Vendor Summary

| Vendor / Architecture Family | Silicon Range | Hardware Readiness | Progress Bar |
| :--- | :--- | :--- | :--- |
| **Intel Processors** | Core Ultra, Core 1st–14th Gen, Xeon, Atom, NetBurst, Itanium | 78% | `[████████░░]` 78% |
| **AMD Processors** | Zen 1–Zen 5, Threadripper, EPYC, Bulldozer, K10, K8, K7 | 80% | `[████████░░]` 80% |
| **Qualcomm Snapdragon** | Snapdragon X Elite/Plus, 8-Series, 7/6/4/2-Series, Wear, XR | 65% | `[███████░░░]` 65% |
| **MediaTek Processors** | Dimensity 9000/8000/7000/6000, Helio G/P/X, Kompanio, Genio | 62% | `[██████░░░░]` 62% |
| **Rockchip Processors** | RK3588, RK3576, RK3399, RK3568, RK3326 Handheld, RV1126 AI | 75% | `[████████░░]` 75% |
| **Allwinner Technology** | A-Series, H-Series, T-Series Automotive, D1 RISC-V, V-Series | 72% | `[███████░░░]` 72% |
| **Broadcom Processors** | Raspberry Pi (BCM2712, BCM2711, BCM2837), Wi-Fi 7/6E Routers | 85% | `[█████████░]` 85% |
| **NVIDIA Superchips & SoCs** | Grace CPU (72-Core Neoverse V2), DRIVE Thor/Orin, Tegra X1/K1 | 68% | `[███████░░░]` 68% |
| **ARM Architecture Core IP** | Cortex-X1–X925, Cortex-A53–A725, Neoverse V/N, Cortex-R/M | 82% | `[████████░░]` 82% |
| **Apple Silicon** | M-Series (M1–M4 Max/Ultra), A-Series (A7–A18 Pro) | 55% | `[██████░░░░]` 55% |
| **Samsung Exynos** | Exynos 2400 (RDNA3), 2200, 2100, 990, 850, Auto V920 | 58% | `[██████░░░░]` 58% |
| **Unisoc Processors** | 5G Tanggula (T820, T770, T760), 4G Tiger (T619, T616, T606) | 55% | `[██████░░░░]` 55% |
| **RISC-V & Hyperscale Cloud** | SiFive, T-Head XuanTie, StarFive, AmpereOne, Graviton, Loongson | 70% | `[███████░░░]` 70% |

---

## 1. Intel Processors

### Core Ultra Series (Series 1 & Series 2)
* **Arrow Lake-S / 200S Series (Desktop LGA 1851):** `[███████░░░]` 75%
  * Core Ultra 9 285K, Core Ultra 9 285KF, Core Ultra 9 285, Core Ultra 9 285T, Core Ultra 7 265K, Core Ultra 7 265KF, Core Ultra 7 265, Core Ultra 7 265F, Core Ultra 7 265T, Core Ultra 5 245K, Core Ultra 5 245KF, Core Ultra 5 245, Core Ultra 5 235, Core Ultra 5 225, Core Ultra 5 225F.
* **Lunar Lake / 200V Series (Mobile Thin & Light):** `[███████░░░]` 70%
  * Core Ultra 9 288V, Core Ultra 7 268V, Core Ultra 7 266V, Core Ultra 7 258V, Core Ultra 7 256V, Core Ultra 5 238V, Core Ultra 5 236V, Core Ultra 5 228V, Core Ultra 5 226V.
* **Meteor Lake / Core Ultra Series 1 (Mobile H/U-Series):** `[████████░░]` 80%
  * Core Ultra 9 185H, Core Ultra 7 165H, Core Ultra 7 155H, Core Ultra 7 165U, Core Ultra 7 155U, Core Ultra 5 135H, Core Ultra 5 125H, Core Ultra 5 135U, Core Ultra 5 125U, Core Ultra 5 115U.

### Core Desktop Generations (14th Gen down to 1st Gen)
* **14th Gen (Raptor Lake Refresh - LGA 1700):** `[█████████░]` 90%
  * **Core i9:** i9-14900KS, i9-14900K, i9-14900KF, i9-14900, i9-14900F, i9-14900T.
  * **Core i7:** i7-14700K, i7-14700KF, i7-14700, i7-14700F, i7-14700T.
  * **Core i5:** i5-14600K, i5-14600KF, i5-14600, i5-14500, i5-14500T, i5-14400, i5-14400F, i5-14400T.
  * **Core i3 & Intel Processor:** i3-14100, i3-14100F, i3-14100T, Intel Processor 300, Intel Processor 300T.
* **13th Gen (Raptor Lake - LGA 1700):** `[█████████░]` 90%
  * **Core i9:** i9-13900KS, i9-13900K, i9-13900KF, i9-13900, i9-13900F, i9-13900T.
  * **Core i7:** i7-13700K, i7-13700KF, i7-13700, i7-13700F, i7-13700T.
  * **Core i5:** i5-13600K, i5-13600KF, i5-13600, i5-13500, i5-13500T, i5-13400, i5-13400F, i5-13400T.
  * **Core i3:** i3-13100, i3-13100F, i3-13100T.
* **12th Gen (Alder Lake - LGA 1700):** `[█████████░]` 90%
  * **Core i9:** i9-12900KS, i9-12900K, i9-12900KF, i9-12900, i9-12900F, i9-12900T.
  * **Core i7:** i7-12700K, i7-12700KF, i7-12700, i7-12700F, i7-12700T.
  * **Core i5:** i5-12600K, i5-12600KF, i5-12600, i5-12500, i5-12500T, i5-12400, i5-12400F, i5-12400T.
  * **Core i3 & Pentium/Celeron:** i3-12300, i3-12100, i3-12100F, i3-12100T, Pentium Gold G7400, Celeron G6900.
* **11th Gen (Rocket Lake - LGA 1200):** `[█████████░]` 92%
  * Core i9-11900K, i9-11900KF, i9-11900, i9-11900F, i9-11900T; Core i7-11700K, i7-11700KF, i7-11700, i7-11700F, i7-11700T; Core i5-11600K, i5-11600KF, i5-11600, i5-11500, i5-11400, i5-11400F, i5-11400T.
* **10th Gen (Comet Lake - LGA 1200):** `[█████████░]` 95%
  * Core i9-10900K, i9-10900KF, i9-10900, i9-10900F, i9-10850K; Core i7-10700K, i7-10700KF, i7-10700, i7-10700F; Core i5-10600K, i5-10600, i5-10500, i5-10400, i5-10400F; Core i3-10320, i3-10300, i3-10105, i3-10100, i3-10100F.
* **9th Gen (Coffee Lake Refresh - LGA 1151v2):** `[█████████░]` 95%
  * Core i9-9900KS, i9-9900K, i9-9900KF, i9-9900; Core i7-9700K, i7-9700KF, i7-9700, i7-9700F; Core i5-9600K, i5-9600KF, i5-9500, i5-9400, i5-9400F; Core i3-9350KF, i3-9100, i3-9100F.
* **8th Gen (Coffee Lake - LGA 1151v2):** `[█████████░]` 95%
  * Core i7-8700K, i7-8700; Core i5-8600K, i5-8600, i5-8500, i5-8400; Core i3-8350K, i3-8300, i3-8100.
* **7th Gen (Kaby Lake - LGA 1151v1):** `[██████████]` 98%
  * Core i7-7700K, i7-7700, i7-7700T; Core i5-7600K, i5-7600, i5-7500, i5-7400; Core i3-7350K, i3-7300, i3-7100; Pentium G4560, G4600, G4620.
* **6th Gen (Skylake - LGA 1151v1):** `[██████████]` 98%
  * Core i7-6700K, i7-6700; Core i5-6600K, i5-6600, i5-6500, i5-6400; Core i3-6320, i3-6300, i3-6100; Pentium G4400.
* **5th Gen (Broadwell - LGA 1150):** `[██████████]` 98%
  * Core i7-5775C, Core i5-5675C.
* **4th Gen (Haswell & Devil's Canyon - LGA 1150):** `[██████████]` 98%
  * Core i7-4790K, i7-4790, i7-4770K, i7-4770, i7-4771; Core i5-4690K, i5-4690, i5-4670K, i5-4670, i5-4590, i5-4460; Core i3-4370, i3-4170, i3-4130; Pentium G3258.
* **3rd Gen (Ivy Bridge - LGA 1155):** `[██████████]` 98%
  * Core i7-3770K, i7-3770, i7-3770S, i7-3770T; Core i5-3570K, i5-3570, i5-3470, i5-3330; Core i3-3240, i3-3220.
* **2nd Gen (Sandy Bridge - LGA 1155):** `[██████████]` 98%
  * Core i7-2700K, i7-2600K, i7-2600; Core i5-2550K, i5-2500K, i5-2500, i5-2400, i5-2300; Core i3-2120, i3-2100.
* **1st Gen (Nehalem & Westmere - LGA 1366 / LGA 1156):** `[██████████]` 98%
  * Core i7-990X Extreme, i7-980X, i7-975 Extreme, i7-960, i7-950, i7-930, i7-920; Core i7-880, i7-870, i7-860; Core i5-760, i5-750, i5-680, i5-650; Core i3-560, i3-540, i3-530.

### Intel High-End Desktop (HEDT) & Core X-Series
* **LGA 2066 (Cascade Lake-X & Skylake-X):** `[█████████░]` 90%
  * Core i9-10980XE, i9-10940X, i9-10920X, i9-10900X, i9-9980XE, i9-9960X, i9-9940X, i9-9920X, i9-9900X, i9-9820X, i7-9800X, i9-7980XE, i9-7960X, i9-7940X, i9-7920X, i9-7900X, i7-7820X, i7-7800X.
* **LGA 2011-v3 (Broadwell-E & Haswell-E):** `[██████████]` 95%
  * Core i7-6950X, i7-6900K, i7-6850K, i7-6800K, i7-5960X, i7-5930K, i7-5820K.
* **LGA 2011 (Ivy Bridge-E & Sandy Bridge-E):** `[██████████]` 95%
  * Core i7-4960X, i7-4930K, i7-4820K, i7-3970X, i7-3960X, i7-3930K, i7-3820.

### Intel Xeon Enterprise & Server
* **Xeon 6 (Sierra Forest & Granite Rapids):** `[██████░░░░]` 65%
  * Xeon 6780E, 6766E, 6756E, 6740E, 6731E, 6710E; Xeon 6980P, 6979P, 6972P, 6960P.
* **5th Gen Xeon Scalable (Emerald Rapids):** `[███████░░░]` 75%
  * Xeon Platinum 8592+, 8580, 8570, 8568Y+; Xeon Gold 6554S, 6548Y+, 6542Y, 6530, 5520+; Xeon Silver 4516Y+, 4514Y, 4510, 4509Y.
* **4th Gen Xeon Scalable (Sapphire Rapids):** `[████████░░]` 80%
  * Xeon Platinum 8490H, 8480+, 8470, 8468, 8460Y+; Xeon Gold 6458Q, 6454S, 6448Y, 6442Y, 5420+; Xeon Silver 4416+, 4410Y; Xeon Max 9480, 9468.
* **Legacy Xeon E5 / E7 (v1 through v4):** `[██████████]` 95%
  * Xeon E5-2699 v4, E5-2697 v4, E5-2680 v4, E5-2650 v4; E5-2699 v3, E5-2678 v3, E5-2690 v3; E5-2697 v2, E5-2680 v2, E5-2670 v2; E5-2690, E5-2687W, E5-2670, E5-2660; Xeon E7-8890 v4, E7-8880 v3.

### Intel Embedded, Atom, & Low-Power SoCs
* **Alder Lake-N:** `[████████░░]` 85%
  * Processor N100, N95, N97, N200, N300, N305, Processor N50, Core i3-N305, Core i3-N300.
* **Tremont & Elkhart Lake:** `[████████░░]` 85%
  * Atom x6425E, x6413E, x6211E, Celeron J6412, J6413, N6210, Pentium J6426.
* **Denverton & Snow Ridge (Atom Server):** `[████████░░]` 80%
  * Atom C3958, C3858, C3758R, C3558R, C3338R, P5962B, P5342.
* **Gemini Lake & Apollo Lake:** `[█████████░]` 90%
  * Celeron J4125, J4105, J4005, N4100, N4020, N4000; Pentium Silver J5040, N5030, N5000; Celeron J3455, N3450.
* **Classic Netbook & Tablet Atoms:** `[██████████]` 95%
  * Atom x5-Z8350, x5-Z8500, x7-Z8700, Z3735F, Z3740, D525, D2550, D2700, N570, N455, N450, N280, N270, Atom 330, Atom 230.

### Intel Historical & Landmark Microprocessors
* **Core 2 Family:** `[██████████]` 98%
  * Core 2 Quad Q9650, Q9550, Q6600, Q6700; Core 2 Extreme QX9770, QX9650, QX6850, X6800; Core 2 Duo E8600, E8500, E8400, E7500, E6600, E6300.
* **NetBurst & Earlier:** `[██████████]` 98%
  * Pentium 4 Extreme Edition 3.73 GHz, Pentium 4 540, 630, 660, 670; Pentium D 960, 950, 820, 805; Pentium III Tualatin-S 1.4 GHz, Coppermine 1000 MHz, Katmai 500 MHz; Pentium II Deschutes 450 MHz, Klamath 266 MHz; Pentium Pro; Pentium OverDrive; Pentium MMX; i486DX4-100, i486DX2-66, i486DX-33; i386DX, i386SX; 80286; 8086, 8088; 8085; 8008; 4004.
* **Itanium (IA-64):** `[████░░░░░░]` 40%
  * Itanium 9760, 9750, 9740, 9720 (Kittson); 9560, 9550, 9540 (Poulson); 9350, 9340 (Tukwila); 9150N (Montvale); 9050 (Montecito).

---

## 2. AMD Processors

### Zen 5 Generation (Ryzen 9000 Desktop & Ryzen AI 300 Mobile)
* **Desktop (Socket AM5):** `[████████░░]` 80%
  * Ryzen 9 9950X3D, Ryzen 9 9950X, Ryzen 9 9900X3D, Ryzen 9 9900X, Ryzen 7 9800X3D, Ryzen 7 9700X, Ryzen 5 9600X.
* **Mobile / Strix Point & Strix Halo:** `[███████░░░]` 75%
  * Ryzen AI Max+ 395, Ryzen AI Max 390, Ryzen AI Max 385, Ryzen AI 9 HX 375, Ryzen AI 9 HX 370, Ryzen AI 9 365, Ryzen AI 7 PRO 360.

### Zen 4 Generation (Ryzen 7000 Desktop & 8000G APUs)
* **Desktop (Socket AM5):** `[█████████░]` 90%
  * Ryzen 9 7950X3D, Ryzen 9 7950X, Ryzen 9 7900X3D, Ryzen 9 7900X, Ryzen 9 7900, Ryzen 7 7800X3D, Ryzen 7 7700X, Ryzen 7 7700, Ryzen 5 7600X, Ryzen 5 7600, Ryzen 5 7500F.
* **Desktop APUs (8000G Series - Phoenix):** `[████████░░]` 85%
  * Ryzen 7 8700G, Ryzen 7 8700F, Ryzen 5 8600G, Ryzen 5 8400F, Ryzen 5 8500G, Ryzen 3 8300G.
* **Mobile (Dragon Range & Phoenix / Hawk Point):** `[████████░░]` 85%
  * Ryzen 9 7945HX3D, Ryzen 9 7945HX, Ryzen 9 7845HX, Ryzen 7 7745HX; Ryzen 9 8945HS, Ryzen 7 8845HS, Ryzen 5 8645HS; Ryzen 9 7940HS, Ryzen 7 7840HS, Ryzen 7 7840U, Ryzen 5 7640HS, Ryzen 5 7540U; Ryzen Z1 Extreme, Ryzen Z1.

### Zen 3 Generation (Ryzen 5000 Desktop & APUs)
* **Vermeer Desktop (Socket AM4):** `[█████████░]` 95%
  * Ryzen 9 5950X, Ryzen 9 5900XT, Ryzen 9 5900X, Ryzen 7 5800X3D, Ryzen 7 5800XT, Ryzen 7 5800X, Ryzen 7 5700X3D, Ryzen 7 5700X, Ryzen 7 5700, Ryzen 5 5600X3D, Ryzen 5 5600X, Ryzen 5 5600, Ryzen 5 5500X3D, Ryzen 5 5500.
* **Cezanne Desktop APUs & Refreshes:** `[█████████░]` 95%
  * Ryzen 7 5700G, Ryzen 7 5700GE, Ryzen 5 5600G, Ryzen 5 5600GE, Ryzen 5 5600GT, Ryzen 5 5500GT, Ryzen 3 5300G.
* **Mobile (Cezanne & Lucienne):** `[█████████░]` 90%
  * Ryzen 9 5980HX, Ryzen 9 5900HX, Ryzen 7 5800H, Ryzen 7 5800U, Ryzen 5 5600H, Ryzen 5 5600U, Ryzen 7 5700U, Ryzen 5 5500U, Ryzen 3 5300U.

### Zen 2 Generation (Ryzen 3000 Desktop & 4000 Series)
* **Matisse Desktop (Socket AM4):** `[██████████]` 98%
  * Ryzen 9 3950X, Ryzen 9 3900XT, Ryzen 9 3900X, Ryzen 7 3800XT, Ryzen 7 3800X, Ryzen 7 3700X, Ryzen 5 3600XT, Ryzen 5 3600X, Ryzen 5 3600, Ryzen 5 3500X, Ryzen 5 3500, Ryzen 3 3300X, Ryzen 3 3100.
* **Renoir Desktop APUs & CPUs:** `[██████████]` 95%
  * Ryzen 7 4700G, Ryzen 5 4600G, Ryzen 3 4300G, Ryzen 5 4500, Ryzen 3 4100.
* **Mobile (Renoir):** `[█████████░]` 92%
  * Ryzen 9 4900H, Ryzen 7 4800H, Ryzen 7 4800U, Ryzen 5 4600H, Ryzen 5 4500U, Ryzen 3 4300U.

### Zen+ & Zen 1 Generations (Ryzen 2000 & 1000)
* **Pinnacle Ridge / Zen+ (Socket AM4):** `[██████████]` 98%
  * Ryzen 7 2700X, Ryzen 7 2700, Ryzen 5 2600X, Ryzen 5 2600, Ryzen 5 2600E, Ryzen 3 2300X; Raven Ridge APUs: Ryzen 5 2400G, Ryzen 3 2200G.
* **Summit Ridge / Zen 1 (Socket AM4):** `[██████████]` 98%
  * Ryzen 7 1800X, Ryzen 7 1700X, Ryzen 7 1700, Ryzen 5 1600X, Ryzen 5 1600, Ryzen 5 1500X, Ryzen 5 1400, Ryzen 3 1300X, Ryzen 3 1200.

### AMD Ryzen Threadripper & Threadripper PRO
* **Zen 4 (7000 Series - sTR5):** `[████████░░]` 85%
  * Threadripper PRO 7995WX (96-core), 7985WX, 7975WX, 7965WX, 7955WX, 7945WX; HEDT: Threadripper 7980X (64-core), 7970X, 7960X.
* **Zen 3 (5000WX Series - sWRX8):** `[█████████░]` 90%
  * Threadripper PRO 5995WX, 5975WX, 5965WX, 5955WX, 5945WX.
* **Zen 2 (3000 Series - sTRX4 / sWRX8):** `[█████████░]` 92%
  * Threadripper 3990X, 3970X, 3960X; Threadripper PRO 3995WX, 3975WX, 3955WX, 3945WX.
* **Zen+ & Zen 1 (2000 & 1000 Series - TR4):** `[██████████]` 95%
  * Threadripper 2990WX, 2970WX, 2950X, 2920X; Threadripper 1950X, 1920X, 1900X.

### AMD EPYC Server Lineup
* **EPYC 9005 Series (Turin - Zen 5 / Zen 5c):** `[███████░░░]` 75%
  * EPYC 9755 (128C), 9655 (96C), 9575F, 9555, 9475F, 9355, 9845 (160C Zen 5c), 9825 (144C Zen 5c), 9965 (192C Zen 5c).
* **EPYC 9004 Series (Genoa, Bergamo, Genoa-X - Zen 4):** `[████████░░]` 85%
  * EPYC 9754 (128C Zen 4c), 9684X (3D V-Cache), 9654 (96C), 9554, 9454, 9354, 9254, 9124, 8534P (Siena).
* **EPYC 7003 Series (Milan & Milan-X - Zen 3):** `[█████████░]` 90%
  * EPYC 7773X, 7763, 7713, 7573X, 7543, 7443, 7343, 72F3.
* **EPYC 7002 & 7001 Series (Rome & Naples):** `[██████████]` 95%
  * EPYC 7H12, 7742, 7702, 7642, 7542, 7402, 7302, 7282; EPYC 7601, 7551, 7401, 7351, 7251.

### Legacy AMD Architectures
* **Bulldozer / Piledriver / Steamroller / Excavator:** `[██████████]` 95%
  * FX-9590, FX-9370, FX-8370, FX-8350, FX-8320E, FX-8300, FX-8150, FX-8120, FX-6350, FX-6300, FX-4350; A10-7890K, A10-7870K, A10-7850K, A10-6800K, A10-5800K, A8-7600, A8-6600K, A6-6400K, A4-6300.
* **K10 / Stars (Phenom II & Athlon II):** `[██████████]` 98%
  * Phenom II X6 1100T BE, 1090T, 1075T, 1055T; Phenom II X4 980, 965 BE, 955 BE; Phenom II X2 555 BE; Athlon II X4 645; Athlon II X3 450; Athlon II X2 250.
* **K8 / Hammer (Athlon 64 & Opteron):** `[██████████]` 98%
  * Athlon 64 FX-62, FX-60, FX-57, FX-55; Athlon 64 X2 6400+, 6000+, 5600+, 5200+, 4800+, 4200+, 3800+; Athlon 64 4000+, 3700+, 3500+, 3200+; Opteron 248, 252, 285, 885.
* **K7 / K6 / K5 & Historical:** `[██████████]` 98%
  * Athlon XP 3200+ (Barton), 2800+, 2500+ Barton; Athlon Thunderbird 1.4 GHz; Duron 1.8 GHz; K6-III+ 500, K6-2 500; K5 PR166; Am5x86-P75; Am486DX4-100, Am386DX-40.

---

## 3. Qualcomm Snapdragon Processors

### Laptop & Windows on ARM Compute Platforms
* **Snapdragon X Series:** `[██████░░░░]` 65%
  * Snapdragon X Elite (X1E-84-100, X1E-80-100, X1E-78-100); Snapdragon X Plus (X1P-64-100, X1P-46-100, X1P-42-100).
* **Legacy Compute SoCs:** `[███████░░░]` 70%
  * Snapdragon 8cx Gen 3, Snapdragon 8cx Gen 2, Snapdragon 8cx, Snapdragon 850, Snapdragon 7c+ Gen 3, 7c Gen 2, 7c.

### Snapdragon 8-Series Flagship Mobile Platforms
* **Oryon Mobile Architecture:** `[██████░░░░]` 60%
  * Snapdragon 8 Elite (SM8750-AB).
* **Kryo / Cortex-X Mobile Architecture:** `[███████░░░]` 75%
  * Snapdragon 8 Gen 3, 8s Gen 3, 8 Gen 2, 8+ Gen 1, 8 Gen 1, 888+, 888, 870, 865+, 865, 855+, 855, 845, 835, 821, 820, 810, 808, 805, 801, 800, S4 Pro.

### Snapdragon 7, 6, 4 & 2 Series
* **7-Series:** `[███████░░░]` 70%
  * Snapdragon 7+ Gen 3, 7 Gen 3, 7+ Gen 2, 7s Gen 2, 7 Gen 1, 780G, 778G+, 778G, 768G, 765G, 750G, 732G, 730G, 720G, 712, 710.
* **6-Series:** `[███████░░░]` 70%
  * Snapdragon 6 Gen 3, 6s Gen 3, 6 Gen 1, 695, 680, 678, 675, 670, 665, 662, 660, 652, 650, 636, 625.
* **4 & 2 Series:** `[███████░░░]` 70%
  * Snapdragon 4s Gen 2, 4 Gen 2, 4 Gen 1, 480+, 480, 460, 450, 439, 435, 425, 410, 400; Snapdragon 215, 212, 210.
* **Automotive, XR & Wearables:** `[█████░░░░░]` 55%
  * Snapdragon Ride Flex (SA8775P), Cockpit SA8295P/SA8155P; XR2+ Gen 2, XR2 Gen 2, AR2 Gen 1; W5+ Gen 1, Wear 4100+.

---

## 4. MediaTek Processors

### Dimensity Flagship & Mainstream Lineup
* **Dimensity 9000 Lineup:** `[██████░░░░]` 65%
  * Dimensity 9400, Dimensity 9300+, Dimensity 9300 (All-Big-Core), Dimensity 9200+, Dimensity 9200, Dimensity 9000+, Dimensity 9000.
* **Dimensity 8000 Lineup:** `[██████░░░░]` 65%
  * Dimensity 8350, Dimensity 8300-Ultra, Dimensity 8300, Dimensity 8250, Dimensity 8200-Ultra, Dimensity 8200, Dimensity 8100-Max, Dimensity 8100, Dimensity 8050, Dimensity 8000.
* **Dimensity 7000 & 6000 Lineup:** `[██████░░░░]` 65%
  * Dimensity 7350 Pro, 7300, 7200-Ultra, 7050, 7020; Dimensity 6300, 6100+, 6080, 6020; Dimensity 1300, 1200, 1100, 1080, 920, 900, 800, 700.

### Helio Gaming, Kompanio & Automotive
* **Helio G, P & X Series:** `[███████░░░]` 70%
  * Helio G100, G99, G96, G95, G90T, G88, G85, G80, G70, G36; Helio P95, P90, P70, P65, P60, P35, P22; Helio X30, X25, X20, X10.
* **Kompanio & Genio:** `[██████░░░░]` 60%
  * Kompanio 1300T, 1200 (MT8195T), 900T, 838, 520, 500; Genio 1200 (MT8395), Genio 700, 510, 350.
* **Dimensity Auto & Smart TV:** `[█████░░░░░]` 50%
  * Dimensity Auto Cockpit C-X1, C-Y1; Pentonic 2000, 1000, 700.

---

## 5. Rockchip Processors

### High-End & AI Flagship SoCs
* **RK3588 Platform:** `[████████░░]` 80%
  * RK3588 (Quad A76 + Quad A55, 6 TOPS NPU), RK3588S, RK3588M, RK3588J.
* **RK3576 Platform:** `[███████░░░]` 75%
  * RK3576 (Quad A72 + Quad A53, 6 TOPS NPU, low power 8nm).
* **RK3399 Platform:** `[█████████░]` 88%
  * RK3399Pro (Integrated NPU), RK3399 (Dual A72 + Quad A53), OP1.
* **RK3288 Platform:** `[█████████░]` 88%
  * RK3288 (Quad Cortex-A17 up to 1.8GHz, Mali-T760 MP4), RK3288W.

### Mainstream Embedded, SBC & Handheld SoCs
* **RK356x Series:** `[████████░░]` 80%
  * RK3568 (Quad A55, 1 TOPS NPU, dual PCIe/SATA), RK3568B2, RK3566 (Quad A55).
* **RK3528 / RK3562:** `[███████░░░]` 75%
  * RK3528 (Quad A53, 4K 60fps AV1), RK3562 (Quad A53, 1.2 TOPS NPU).
* **Retro Gaming & Vision:** `[████████░░]` 80%
  * RK3326 (Quad A35), PX30, RK3308, RV1126 (2.0 TOPS NPU), RV1109, RV1106 (Cortex-A7 + RISC-V MCU), RV1103, RK1808.

---

## 6. Allwinner Technology Processors

### Application, OTT & Industrial SoCs
* **A-Series (Tablets & SBCs):** `[████████░░]` 80%
  * Allwinner A527, A523, A133 Pro, A133, A100, A64 (PinePhone), A83T, A80 Octa, A33, A20, A10.
* **H-Series (Home Entertainment):** `[████████░░]` 80%
  * Allwinner H728, H700, H618, H616, H6, H5, H3, H2+.
* **T-Series (Automotive & Industrial):** `[███████░░░]` 75%
  * Allwinner T527, T507-H, T507, T113-S3, T113-I (-40°C to 105°C), T3.
* **RISC-V & Edge Camera (D & V Series):** `[████████░░]` 80%
  * Allwinner D1 (XuanTie C906 64-bit RISC-V), D1s / F133; Allwinner V853, V851s, V831, V3s; F1C100s, F1C200s.

---

## 7. Broadcom Processors

### Raspberry Pi & Gateway Network Processors
* **Raspberry Pi Series:** `[█████████░]` 92%
  * Broadcom BCM2712 (Quad Cortex-A76 @ 2.4 GHz - Pi 5), BCM2711 (Quad A72 - Pi 4/400/CM4), BCM2837B0/A0 (Quad A53 - Pi 3), BCM2836 (Quad A7 - Pi 2), BCM2835 (ARM11 - Pi 1 / Pi Zero).
* **Wi-Fi 7 / 6E Gateways:** `[████████░░]` 80%
  * Broadcom BCM4916 (Quad ARMv8 @ 2.6 GHz), BCM4912, BCM4908, BCM47094, BCM4709, BCM4708.
* **Broadband & Cable:** `[███████░░░]` 75%
  * BCM68580 (PON Gateway), BCM63178, BCM63168; BCM3390 (DOCSIS 3.1), BCM7445 (Brahma-B15 server ARM core).

---

## 8. NVIDIA Application Processors & Superchips

* **Data Center & Compute:** `[███████░░░]` 70%
  * NVIDIA Grace CPU (72 ARM Neoverse V2 cores, SVE2, NVLink-C2C), GH200 Grace Hopper, GB200 Grace Blackwell.
* **DRIVE Automotive & Robotics:** `[███████░░░]` 70%
  * NVIDIA DRIVE Thor (2,000 TFLOPS), DRIVE Orin / Jetson AGX Orin (T234, 12x Cortex-A78AE), DRIVE Xavier / Jetson AGX Xavier (T194, 8x Carmel cores).
* **Tegra Mobile/Embedded SoCs:** `[████████░░]` 82%
  * Tegra X2 (Parker - Denver 2 + A57), Tegra X1+ (Nintendo Switch OLED), Tegra X1 (Switch, Shield TV), Tegra K1 (64-bit Denver / 32-bit A15), Tegra 4, Tegra 3, Tegra 2.

---

## 9. ARM Architecture Core Designs (Licensed IP)

* **Cortex-X Flagship Cores:** `[███████░░░]` 75%
  * Cortex-X925 (Armv9-A 3nm), Cortex-X4, Cortex-X3, Cortex-X2, Cortex-X1.
* **Cortex-A Performance Cores:** `[█████████░]` 88%
  * Cortex-A725, Cortex-A720, Cortex-A715, Cortex-A710, Cortex-A78, Cortex-A77, Cortex-A76, Cortex-A75, Cortex-A73, Cortex-A72, Cortex-A57, Cortex-A15, Cortex-A9, Cortex-A8.
* **Cortex-A Efficiency Cores:** `[█████████░]` 90%
  * Cortex-A520, Cortex-A510, Cortex-A55, Cortex-A53, Cortex-A35, Cortex-A32, Cortex-A7.
* **Neoverse Infrastructure:** `[████████░░]` 80%
  * Neoverse V3, V3AE, V2, V1 (Heavy Compute); Neoverse N3, N2, N1, E1 (Scale-Out).
* **Cortex-R & Cortex-M:** `[████████░░]` 80%
  * Cortex-R82, R52+, R52, R8, R7, R5; Cortex-M85, M55, M33, M23, M7, M4, M3, M0+.
* **Classic ARM:** `[██████████]` 95%
  * ARM1176JZF-S, ARM926EJ-S, ARM7TDMI.

---

## 10. Apple Silicon Processors

* **M-Series (Mac & iPad Pro):** `[█████░░░░░]` 55%
  * Apple M4 Max, M4 Pro, M4; Apple M3 Max, M3 Pro, M3; Apple M2 Ultra, M2 Max, M2 Pro, M2; Apple M1 Ultra, M1 Max, M1 Pro, M1.
* **A-Series (iPhone & iPad):** `[█████░░░░░]` 55%
  * Apple A18 Pro, A18, A17 Pro (Ray Tracing), A16 Bionic, A15 Bionic, A14 Bionic, A13 Bionic, A12 Bionic, A11 Bionic, A10 Fusion, A9, A8, A7 (Cyclone 64-bit); Apple A6, A5, A4, S5L8900.

---

## 11. Samsung Exynos Processors

* **Flagship Mobile:** `[██████░░░░]` 60%
  * Exynos 2400 (Deca-core + Xclipse 940 RDNA3 GPU), Exynos 2200 (Xclipse 920 RDNA2 GPU), Exynos 2100, Exynos 990, Exynos 9825, Exynos 9820 (Mongoose M4), Exynos 9810, Exynos 8895, Exynos 8890, Exynos 7420 (Galaxy S6), Exynos 5433, 4412, 3110 (Hummingbird).
* **Mid-Range & Automotive:** `[██████░░░░]` 60%
  * Exynos 1480 (Xclipse 530), Exynos 1380, Exynos 1280, Exynos 1080, Exynos 850; Exynos Auto V920 (10x Cortex-A78AE), Exynos Auto V9; Exynos W930, W920.

---

## 12. Unisoc Processors

* **5G Tanggula Platforms:** `[██████░░░░]` 55%
  * Unisoc T820 (Cortex-A76 @ 2.7GHz + Mali-G57 MC4), Unisoc T770, Unisoc T760, Unisoc T750, Unisoc T740.
* **4G LTE Smartphone Platforms:** `[██████░░░░]` 60%
  * Unisoc T619, T616, T612, T610, T606 (2x A75 + 6x A55); Unisoc T310; SC9863A (8x A55), SC9832E, SC7731E.

---

## 13. RISC-V, Cloud Infrastructure & Alternative Silicon

* **RISC-V High-Performance:** `[████████░░]` 82%
  * **SiFive:** Performance P870-D, P670, P550, P470; Intelligence X280; Essential U74-MC, U74, U54, E76; Freedom U740, Horse Creek P550.
  * **Alibaba T-Head (XuanTie):** XuanTie C920, C910 (RVB-ICE / Lichee Pi 4A), C908, C906, E907, E902, TH1520 SoC.
  * **StarFive Technology:** Dubhe, JH7110 (Quad U74 in VisionFive 2), JH7100.
  * **SpacemiT:** Key Stone K1 (Octa-core 64-bit RISC-V with 2.0 TOPS NPU, Banana Pi BPI-F3).
* **Cloud Native Hyperscale Server:** `[███████░░░]` 75%
  * **Ampere Computing:** AmpereOne A192-32X (192 custom ARM cores), AmpereOne A160, Ampere Altra Max M128-30 (128 cores), Ampere Altra Q80-33.
  * **AWS:** Graviton4 (96 Neoverse V2 cores), Graviton3E, Graviton3, Graviton2, Graviton1.
  * **Google & Microsoft:** Google Axion CPU (Neoverse V2), Microsoft Azure Cobalt 100 (128 Neoverse N2 cores).
* **Alternative Architectures:** `[███████░░░]` 70%
  * **Loongson (LoongArch):** Loongson 3A6000, 3B6000, 3C6000 (16-core server), 3A5000, 3C5000, 3D5000 (32-core dual-die).
  * **Zhaoxin (x86-64):** KaiXian KX-7000 (8-core x86 @ 3.7GHz), KX-6000G, KX-6000; KaiSheng KH-40000 (32 cores).
  * **Phytium (ARMv8):** Tengyun S5000C (64-core), Tengrui D2000 (8-core desktop), FT-2000/4.
  * **Huawei HiSilicon:** Kunpeng 920 (up to 64 TaiShan v110 ARM cores); Kirin 9010, Kirin 9000s (Maleoon 910 GPU), Kirin 9000, Kirin 990 5G, Kirin 980.


---

# The Complete Global Laptop & Mobile CPU Directory

This directory details all supported, detected, and targeted mobile, ultraportable, laptop, handheld, and high-performance gaming laptop microprocessors across **akaOS** and the **XSCore** architecture layer.

### Laptop & Mobile CPU Architecture Summary

| Vendor / Platform | Mobile Series & Segment | Target Readiness | Progress Bar |
| :--- | :--- | :--- | :--- |
| **Intel Mobile Core** | Lunar Lake, Arrow Lake-H/HX, Meteor Lake, Raptor Lake Mobile | 82% | `[████████░░]` 82% |
| **Intel Classic Mobile** | Tiger Lake, Ice Lake, Coffee Lake, Sandy/Ivy Bridge, Core 2 Duo, Pentium M | 95% | `[█████████░]` 95% |
| **AMD Ryzen Mobile** | Strix Halo (Ryzen AI Max+), Strix Point, Hawk Point, Phoenix, Dragon Range | 85% | `[████████░░]` 85% |
| **AMD Classic Mobile** | Rembrandt (6000), Cezanne (5000), Renoir (4000), APUs (A10/A8), Turion 64 | 92% | `[█████████░]` 92% |
| **Qualcomm Compute** | Snapdragon X Elite / X Plus (Oryon), 8cx Gen 3, 7c Series | 68% | `[███████░░░]` 68% |
| **Apple Silicon Mac** | Apple M4 Max/Pro/M4, M3, M2, M1 Family (MacBook Pro & Air) | 60% | `[██████░░░░]` 60% |
| **MediaTek Kompanio** | Kompanio 1300T/1200/900T/838/528/520/500 Chromebooks & Laptops | 65% | `[██████░░░░]` 65% |
| **Rockchip & Open Laptops** | RK3588S/RK3588 (Nova Laptop), RK3399/OP1 (Pinebook Pro), Allwinner A64 | 82% | `[████████░░]` 82% |

---

## 1. Intel Laptop Processors (Mobile Core, Core Ultra, & Celeron/Pentium M)

### Core Ultra Series 2 & Series 1 (Meteor Lake, Lunar Lake, & Arrow Lake Mobile)
* **Lunar Lake (Core Ultra 200V Series - 17W to 30W On-Package Memory):** `[███████░░░]` 75%
  * Core Ultra 9 288V (32GB LPDDR5X)
  * Core Ultra 7 268V (32GB), Core Ultra 7 266V (16GB), Core Ultra 7 258V (32GB), Core Ultra 7 256V (16GB)
  * Core Ultra 5 238V (32GB), Core Ultra 5 236V (16GB), Core Ultra 5 228V (32GB), Core Ultra 5 226V (16GB)
* **Arrow Lake-H & Arrow Lake-HX (Core Ultra 200H / 200HX Series - High Performance):** `[███████░░░]` 75%
  * Core Ultra 9 285HX, Core Ultra 9 275HX
  * Core Ultra 7 265HX, Core Ultra 7 255HX
  * Core Ultra 5 245HX, Core Ultra 5 235HX
  * Core Ultra 9 285H, Core Ultra 7 265H, Core Ultra 7 255H, Core Ultra 5 235H, Core Ultra 5 225H
* **Meteor Lake (Core Ultra Series 1 - 28W H-Series & 15W/9W U-Series):** `[████████░░]` 82%
  * **H-Series (28W Base / 64W–115W Turbo):** Core Ultra 9 185H, Core Ultra 7 165H, Core Ultra 7 155H, Core Ultra 5 135H, Core Ultra 5 125H
  * **U-Series (15W Base / 57W Turbo):** Core Ultra 7 165U, Core Ultra 7 155U, Core Ultra 5 135U, Core Ultra 5 125U
  * **Ultra-Low Power (9W Base / 28W Turbo):** Core Ultra 7 164U, Core Ultra 5 134U

### 14th & 13th Gen Raptor Lake Mobile (HX, H, P, U Series)
* **14th Gen Raptor Lake-HX Refresh (55W Base / Enthusiast Mobile Workstation & Gaming):** `[█████████░]` 90%
  * Core i9-14900HX
  * Core i7-14700HX, Core i7-14650HX
  * Core i5-14500HX, Core i5-14450HX
* **13th Gen Raptor Lake-HX (55W Base / 157W Turbo Desktop Silicon in BGA):** `[█████████░]` 90%
  * Core i9-13980HX, Core i9-13950HX, Core i9-13900HX
  * Core i7-13850HX, Core i7-13700HX, Core i7-13650HX
  * Core i5-13600HX, Core i5-13500HX, Core i5-13450HX
* **13th Gen Raptor Lake-H (45W Performance Gaming & Creator):** `[█████████░]` 90%
  * Core i9-13900HK (Unlocked), Core i9-13900H
  * Core i7-13800H, Core i7-13700H, Core i7-13620H
  * Core i5-13600H, Core i5-13500H, Core i5-13420H
* **13th Gen Raptor Lake-P (28W Thin & Light Performance):** `[█████████░]` 92%
  * Core i7-1370P, Core i7-1360P, Core i5-1350P, Core i5-1340P
* **13th Gen Raptor Lake-U & Series 1 U-Refresh (15W Ultra-Portable):** `[█████████░]` 92%
  * Core 7 150U, Core 5 120U, Core 3 100U
  * Core i7-1365U, Core i7-1355U, Core i5-1345U, Core i5-1335U, Core i5-1334U, Core i3-1315U, Core i3-1305U, Intel Processor U300

### 12th Gen Alder Lake Mobile (HX, H, P, U Series)
* **Alder Lake-HX (55W Enthusiast BGA):** `[█████████░]` 90%
  * Core i9-12950HX, Core i9-12900HX, Core i7-12850HX, Core i7-12800HX, Core i7-12650HX, Core i5-12600HX, Core i5-12450HX.
* **Alder Lake-H (45W Gaming & Workstation):** `[█████████░]` 90%
  * Core i9-12900HK, Core i9-12900H, Core i7-12800H, Core i7-12700H, Core i7-12650H, Core i5-12600H, Core i5-12500H, Core i5-12450H.
* **Alder Lake-P (28W Premium Thin & Light):** `[█████████░]` 92%
  * Core i7-1280P, Core i7-1270P, Core i7-1260P, Core i5-1250P, Core i5-1240P, Core i3-1220P.
* **Alder Lake-U (15W & 9W Base):** `[█████████░]` 92%
  * Core i7-1265U, Core i7-1255U, Core i7-1260U, Core i7-1250U, Core i5-1245U, Core i5-1235U, Core i5-1240U, Core i5-1230U, Core i3-1215U, Core i3-1210U, Pentium 8505, Celeron 7305.

### 11th Gen Tiger Lake Mobile (H45, H35, UP3, UP4)
* **Tiger Lake-H45 & H35:** `[█████████░]` 95%
  * Core i9-11980HK, Core i9-11950H, Core i9-11900H, Core i7-11850H, Core i7-11800H, Core i5-11500H, Core i5-11400H, Core i5-11260H.
  * Core i7-11390H, Core i7-11375H SE, Core i7-11370H, Core i5-11320H, Core i5-11300H.
* **Tiger Lake-UP3 & UP4:** `[█████████░]` 95%
  * Core i7-1195G7, Core i7-1185G7, Core i7-1165G7, Core i5-1155G7, Core i5-1145G7, Core i5-1135G7, Core i3-1125G4, Core i3-1115G4.
  * Core i7-1180G7, Core i7-1160G7, Core i5-1140G7, Core i5-1130G7, Core i3-1120G4, Core i3-1110G4.

### 10th Gen Mobile (Ice Lake 10nm & Comet Lake 14nm)
* **Comet Lake-H & U:** `[██████████]` 96%
  * Core i9-10980HK, Core i7-10875H, Core i7-10870H, Core i7-10850H, Core i7-10750H, Core i5-10500H, Core i5-10400H, Core i5-10300H, Core i5-10200H.
  * Core i7-10810U, Core i7-10710U (6-core), Core i7-10610U, Core i7-10510U, Core i5-10310U, Core i5-10210U, Core i3-10110U.
* **Ice Lake-U & Y:** `[█████████░]` 95%
  * Core i7-1068NG7, Core i7-1065G7, Core i5-1038NG7, Core i5-1035G7, Core i5-1035G4, Core i5-1035G1, Core i3-1005G1.
  * Core i7-1060G7, Core i5-1030G7, Core i5-1030G4, Core i3-1000G4, Core i3-1000G1.

### 9th & 8th Gen Coffee Lake / Kaby Lake Mobile
* **9th Gen Coffee Lake-H Refresh:** `[██████████]` 98%
  * Core i9-9980HK, Core i9-9880H, Core i7-9850H, Core i7-9750H, Core i7-9750HF, Core i5-9400H, Core i5-9300H, Core i5-9300HF.
* **8th Gen Coffee Lake-H & Kaby Lake-G:** `[██████████]` 98%
  * Core i9-8950HK, Core i7-8850H, Core i7-8750H, Core i5-8400H, Core i5-8300H.
  * Core i7-8809G, Core i7-8709G, Core i7-8706G, Core i7-8705G, Core i5-8305G (Vega M on package).
* **8th Gen Whiskey Lake-U, Kaby Lake-R & Coffee Lake-U:** `[██████████]` 98%
  * Core i7-8665U, Core i7-8565U, Core i5-8365U, Core i5-8265U, Core i3-8145U; Core i7-8650U, Core i7-8550U, Core i5-8350U, Core i5-8250U, Core i3-8130U.
  * Core i7-8559U, Core i5-8269U, Core i5-8259U, Core i3-8109U.

### 7th Gen down to 1st Gen Mobile Core i
* **7th Gen Kaby Lake Mobile:** `[██████████]` 98%
  * H-Series: Core i7-7920HQ, i7-7820HK, i7-7820HQ, i7-7700HQ, i5-7440HQ, i5-7300HQ.
  * U/Y-Series: Core i7-7600U, i7-7500U, i5-7300U, i5-7200U, i3-7100U; Core i7-7Y75, i5-7Y54, m3-7Y30.
* **6th Gen Skylake Mobile:** `[██████████]` 98%
  * H-Series: Core i7-6920HQ, i7-6820HK, i7-6820HQ, i7-6700HQ, i5-6300HQ.
  * U-Series: Core i7-6600U, i7-6500U, i5-6300U, i5-6200U, i3-6100U; Core m7-6Y75, m5-6Y54, m3-6Y30.
* **5th Gen Broadwell Mobile:** `[██████████]` 98%
  * H-Series: Core i7-5950HQ, i7-5850HQ, i7-5750HQ, i7-5700HQ, i5-5350H.
  * U-Series: Core i7-5650U, i7-5600U, i7-5557U, i7-5500U, i5-5350U, i5-5300U, i5-5200U, i3-5010U; Core M-5Y71, M-5Y10.
* **4th Gen Haswell Mobile:** `[██████████]` 98%
  * Extreme & Quad: Core i7-4940MX Extreme, i7-4930MX, i7-4910MQ, i7-4900MQ, i7-4810MQ, i7-4800MQ, i7-4710MQ, i7-4700MQ, i7-4770HQ, i7-4720HQ, i7-4710HQ.
  * Dual-Core M/H: Core i7-4610M, i7-4600M, i5-4340M, i5-4300M, i5-4210M, i5-4200M, i3-4100M.
  * U/Y-Series: Core i7-4650U, i7-4500U, i5-4300U, i5-4200U, i3-4030U, i3-4010U; i7-4610Y, i5-4210Y.
* **3rd Gen Ivy Bridge Mobile:** `[██████████]` 98%
  * Core i7-3940XM Extreme, i7-3920XM, i7-3840QM, i7-3820QM, i7-3740QM, i7-3720QM, i7-3630QM, i7-3610QM.
  * Core i7-3540M, i7-3520M, i5-3380M, i5-3360M, i5-3320M, i5-3230M, i5-3210M, i3-3120M, i3-3110M.
  * ULV: Core i7-3687U, i7-3667U, i7-3537U, i7-3517U, i5-3437U, i5-3427U, i5-3337U, i5-3317U, i3-3227U, i3-3217U.
* **2nd Gen Sandy Bridge Mobile:** `[██████████]` 98%
  * Core i7-2960XM Extreme, i7-2920XM, i7-2860QM, i7-2820QM, i7-2760QM, i7-2720QM, i7-2670QM, i7-2630QM.
  * Core i7-2640M, i7-2620M, i5-2540M, i5-2520M, i5-2450M, i5-2430M, i5-2410M, i3-2370M, i3-2350M, i3-2330M.
  * ULV: Core i7-2677M, i7-2637M, i5-2557M, i5-2467M, i3-2367M.
* **1st Gen Mobile (Clarksfield & Arrandale):** `[██████████]` 98%
  * Core i7-940XM Extreme, i7-920XM, i7-840QM, i7-820QM, i7-740QM, i7-720QM.
  * Core i7-640M, i7-620M, i5-580M, i5-560M, i5-540M, i5-520M, i5-480M, i5-460M, i5-430M, i3-390M, i3-380M, i3-370M, i3-350M, i3-330M.
  * UM (Low Voltage): Core i7-680UM, i7-660UM, i7-640UM, i7-620UM, i5-560UM, i5-540UM, i5-520UM, i3-380UM.

### Classic Mobile Microprocessors (Pre-Core i)
* **Core 2 Extreme & Quad Mobile:** `[██████████]` 98%
  * X9100, X9000, X7900, X7800, QX9300 (Quad), QX9200, Q9100, Q9000.
* **Core 2 Duo Mobile (Penryn 45nm & Merom 65nm):** `[██████████]` 98%
  * T-Series: T9900, T9800, T9600, T9550, T9500, T9400, T9300, T8300, T8100, T7800, T7700, T7500, T7300, T7250, T7200, T7100, T5870, T5800, T5600, T5500.
  * P-Series (25W): P9700, P9600, P9500, P8800, P8700, P8600, P8400, P7450, P7350.
  * SP / SL / SU: SP9600, SP9400, SL9600, SL9400, SU9600, SU9400, SU7300, U7700, U7600.
* **Intel Core Duo & Core Solo (Yonah 65nm):** `[██████████]` 98%
  * Core Duo T2700, T2600, T2500, T2450, T2400, T2350, T2300, T2250, T2050; L2500, L2400; U2500, U2400.
  * Core Solo T1400, T1350, T1300, T1200; U1500, U1400, U1300.
* **Intel Pentium M & Mobile Celeron (Banias & Dothan):** `[██████████]` 98%
  * Pentium M 780 (2.26 GHz), 770, 765, 760, 755, 750, 745, 740, 735, 730, 725, 715, 705.
  * Pentium M LV/ULV: 778, 758, 738, 733, 723; Banias 1.7 GHz, 1.6 GHz, 1.5 GHz, 1.4 GHz, 1.3 GHz.
  * Mobile Pentium 4-M: 2.6 GHz down to 1.8 GHz; Mobile Pentium III-M: 1.33 GHz down to 866 MHz; Mobile Pentium II: 400 MHz down to 266 MHz.

---

## 2. AMD Laptop & Mobile Processors

### Ryzen AI 300 & Ryzen AI 400 Series (Strix Point & Strix Halo)
* **Ryzen AI Max & Max+ (Strix Halo - Zen 5/5c, RDNA 3.5 40 CU):** `[████████░░]` 80%
  * Ryzen AI Max+ PRO 495 (16C/32T, Radeon 8065S)
  * Ryzen AI Max PRO 490 (12C/24T, Radeon 8050S)
  * Ryzen AI Max PRO 485 (8C/16T)
  * Ryzen AI Max PRO 480 (6C/12T)
* **Ryzen AI 300 Series (Strix Point - Zen 5/5c + XDNA 2 50 TOPS NPU):** `[████████░░]` 82%
  * Ryzen AI 9 HX 375 (12 Cores, 55 NPU TOPS)
  * Ryzen AI 9 HX 370 (12 Cores, 50 NPU TOPS, Radeon 890M)
  * Ryzen AI 9 365 (10 Cores, 50 NPU TOPS, Radeon 880M)
  * Ryzen AI 7 PRO 360 (8 Cores)

### Ryzen 8040 & 7040 Series (Hawk Point & Phoenix: Zen 4 Mobile)
* **Hawk Point (Ryzen 8040 Series - 4nm Zen 4 + RDNA3 + XDNA NPU):** `[█████████░]` 90%
  * Ryzen 9 8945HS (8C/16T, 45W)
  * Ryzen 7 8845HS, Ryzen 7 8840HS, Ryzen 7 8840U (15W–30W)
  * Ryzen 5 8645HS, Ryzen 5 8640HS, Ryzen 5 8640U
  * Ryzen 3 8440U
* **Phoenix (Ryzen 7040 Series - 4nm Zen 4 + RDNA3):** `[█████████░]` 90%
  * Ryzen 9 7940HS, Ryzen 9 7940H
  * Ryzen 7 7840HS, Ryzen 7 7840H, Ryzen 7 7840U
  * Ryzen 5 7640HS, Ryzen 5 7640H, Ryzen 5 7540U
  * Ryzen 3 7440U

### Ryzen 7045 Series (Dragon Range: Zen 4 Desktop Silicon in Mobile BGA)
* **55W–75W+ Extreme Enthusiast & Desktop Replacements:** `[█████████░]` 90%
  * Ryzen 9 7945HX3D (16C/32T, 128MB 3D V-Cache)
  * Ryzen 9 7945HX (16 Cores / 32 Threads)
  * Ryzen 9 7845HX (12 Cores / 24 Threads)
  * Ryzen 7 7745HX (8 Cores / 16 Threads)
  * Ryzen 5 7645HX (6 Cores / 12 Threads)

### Ryzen 6000 & 7035 Series (Rembrandt & Rembrandt-R: Zen 3+ 6nm + RDNA2)
* **Ryzen 7035 & 6000 Mobile Series:** `[█████████░]` 92%
  * Ryzen 7 7735HS, Ryzen 7 7735U; Ryzen 5 7535HS, Ryzen 5 7535U; Ryzen 3 7335U.
  * Ryzen 9 6980HX, Ryzen 9 6980HS, Ryzen 9 6900HX, Ryzen 9 6900HS, Ryzen 7 6800H, Ryzen 7 6800HS, Ryzen 5 6600H, Ryzen 5 6600HS; Ryzen 7 6800U, Ryzen 5 6600U.

### Ryzen 5000 & 7030 Series (Cezanne, Barcelo, & Lucienne: Zen 3 & Zen 2)
* **Ryzen 7030 & 5000 Mobile Series:** `[██████████]` 96%
  * Ryzen 7 7730U, Ryzen 5 7530U, Ryzen 3 7330U.
  * H / HX / HS: Ryzen 9 5980HX, Ryzen 9 5980HS, Ryzen 9 5900HX, Ryzen 9 5900HS, Ryzen 7 5800H, Ryzen 7 5800HS, Ryzen 5 5600H.
  * U-Series: Ryzen 7 5800U, Ryzen 5 5600U, Ryzen 7 5700U, Ryzen 5 5500U, Ryzen 3 5300U.

### Ryzen 4000 Mobile Series (Renoir - Zen 2 7nm)
* **H & HS Gaming & U-Series:** `[██████████]` 98%
  * Ryzen 9 4900H, Ryzen 9 4900HS; Ryzen 7 4800H, Ryzen 7 4800HS; Ryzen 5 4600H, Ryzen 5 4600HS.
  * Ryzen 7 4800U, Ryzen 7 4700U, Ryzen 5 4600U, Ryzen 5 4500U, Ryzen 3 4300U.

### Ryzen 7020 Series (Mendocino - Budget Zen 2 6nm + RDNA2)
* **Mendocino U & C Series:** `[█████████░]` 92%
  * Ryzen 5 7520U, Ryzen 5 7520C; Ryzen 3 7320U, Ryzen 3 7320C; Athlon Gold 7220U, Athlon Silver 7120U.

### Ryzen 3000 & 2000 Mobile Series (Picasso & Raven Ridge APUs)
* **Picasso & Raven Ridge:** `[██████████]` 98%
  * Ryzen 7 3750H, Ryzen 5 3550H; Ryzen 7 3700U, Ryzen 5 3500U, Ryzen 3 3300U, Ryzen 3 3200U.
  * Ryzen 7 2800H, Ryzen 5 2600H; Ryzen 7 2700U, Ryzen 5 2500U, Ryzen 3 2300U, Ryzen 3 2200U; Athlon 300U.

### Classic AMD Mobile APUs & CPUs (Pre-Ryzen)
* **Excavator, Steamroller, Piledriver, Llano, Bobcat, Turion:** `[██████████]` 98%
  * Bristol Ridge & Stoney Ridge: FX-9830P, FX-9800P, A12-9720P, A10-9620P, A9-9420.
  * Carrizo & Kaveri: FX-8800P, A10-8700P; FX-7600P, A10-7400P.
  * Richland & Trinity: A10-5750M, A8-5550M, A6-5350M; A10-4600M, A8-4500M.
  * Llano & Brazos: A8-3500M, A6-3400M; E2-2000, E-450, E-350; A6-5200 (Kabini).
  * Turion 64 X2 & Turion 64: M660, ZM-86, TL-68, TL-64, TL-60, TL-58, TL-50; ML-44 down to ML-30, Mobile Athlon 64.

---

## 3. Qualcomm Snapdragon Laptop Platforms (Windows on ARM)

### Snapdragon X Series (Oryon CPU Core Architecture)
* **Snapdragon X Elite:** `[███████░░░]` 70%
  * X1E-84-100 (12 Cores, 3.8 GHz All-Core, 4.2 GHz Boost, 4.6 TFLOPS Adreno, 45 TOPS NPU)
  * X1E-80-100 (12 Cores, 3.4 GHz All-Core, 4.0 GHz Boost, 3.8 TFLOPS, 45 TOPS NPU)
  * X1E-78-100 (12 Cores, 3.4 GHz All-Core, 3.8 TFLOPS, 45 TOPS NPU)
* **Snapdragon X Plus:** `[███████░░░]` 70%
  * X1P-66-100 (10 Cores, 3.4 GHz All-Core, 4.0 GHz Boost, 45 TOPS NPU)
  * X1P-64-100 (10 Cores, 3.4 GHz All-Core, 45 TOPS NPU)
  * X1P-46-100 (8 Cores, 3.4 GHz All-Core, 4.0 GHz Boost, 45 TOPS NPU)
  * X1P-42-100 (8 Cores, 3.2 GHz All-Core, 3.4 GHz Boost, 45 TOPS NPU)

### Snapdragon 8cx & Earlier Compute Platforms
* **8cx Series & Early Compute:** `[███████░░░]` 75%
  * Snapdragon 8cx Gen 3 (SC8280XP - 4x Cortex-X1 @ 3.0GHz + 4x A78 @ 2.4GHz).
  * Snapdragon 8cx Gen 2 5G (SC8180XP - 4x Kryo 495 Gold + 4x Silver).
  * Snapdragon 8cx (SC8180X - 8-core Kryo 495 @ 2.84 GHz).
  * Snapdragon 850 (SDM850 - 8x Kryo 385 @ 2.96 GHz) & Snapdragon 835 PC (MSM8998).
* **Snapdragon 7c Series (Chromebook & Education):** `[████████░░]` 80%
  * Snapdragon 7c+ Gen 3 (SC7380 - 4x Cortex-A78 + 4x Cortex-A55).
  * Snapdragon 7c Gen 2 (SC7180-2) & Snapdragon 7c (SC7180).

---

## 4. Apple Silicon MacBook Processors (M-Series)

* **Apple M4 Family:** `[██████░░░░]` 60%
  * Apple M4 Max (16-Core CPU: 12P + 4E, up to 40-Core GPU, 16-Core NPU, up to 128GB Unified Memory).
  * Apple M4 Pro (14-Core CPU: 10P + 4E or 12-Core: 8P + 4E, up to 20-Core GPU).
  * Apple M4 (10-Core CPU: 4P + 6E, 10-Core GPU).
* **Apple M3 Family:** `[██████░░░░]` 60%
  * Apple M3 Max (16-Core / 14-Core, 40/30-Core GPU, Dynamic Caching, Ray Tracing).
  * Apple M3 Pro (12-Core / 11-Core, 18/14-Core GPU).
  * Apple M3 (8-Core CPU: 4P + 4E, 10/8-Core GPU).
* **Apple M2 Family:** `[██████░░░░]` 62%
  * Apple M2 Max (12-Core CPU: 8P + 4E, up to 38-Core GPU).
  * Apple M2 Pro (12-Core or 10-Core CPU, up to 19-Core GPU).
  * Apple M2 (8-Core CPU: 4 Avalanche + 4 Blizzard, 10/8-Core GPU).
* **Apple M1 Family:** `[███████░░░]` 68%
  * Apple M1 Max (10-Core CPU: 8 Firestorm + 2 Icestorm, up to 32-Core GPU).
  * Apple M1 Pro (10-Core or 8-Core CPU, 16/14-Core GPU).
  * Apple M1 (8-Core CPU: 4 Firestorm + 4 Icestorm, 8/7-Core GPU).

---

## 5. MediaTek Kompanio (Laptop & Chromebook Platforms)

* **Premium Tier (Chromebooks & Clamshells):** `[██████░░░░]` 65%
  * Kompanio 1300T (MT8195T - 4x Cortex-A78 + 4x Cortex-A55, Mali-G77 MC9).
  * Kompanio 1200 (MT8195 - 4x Cortex-A78 + 4x Cortex-A55, Mali-G57 MC5).
  * Kompanio 900T (MT8791 - 2x Cortex-A78 + 6x Cortex-A55).
* **Mainstream & Educational Tier:** `[███████░░░]` 70%
  * Kompanio 838 (MT8188 - Octa-core + NPU 650 for AI education notebooks).
  * Kompanio 528 (MT8186T - 2x Cortex-A76 + 6x Cortex-A55).
  * Kompanio 520 (MT8186 - 2x Cortex-A76 + 6x Cortex-A55).
  * Kompanio 500 (MT8183 - 4x Cortex-A73 + 4x Cortex-A53).

---

## 6. Rockchip & ARM Open-Hardware Laptop SoCs

* **Rockchip RK3588S / RK3588:** `[████████░░]` 82%
  * 4x Cortex-A76 @ 2.4 GHz + 4x Cortex-A55 @ 1.8 GHz, ARM Mali-G610 MP4, 6 TOPS NPU (Powering ARM laptops like the Indiedroid Nova Laptop, Cool Pi 4B laptops, and Orange Pi Neo handheld setups).
* **Rockchip RK3399 / OP1:** `[█████████░]` 88%
  * Dual Cortex-A72 + Quad Cortex-A53 (Pine64 Pinebook Pro, ASUS Chromebook Flip C101PA, Samsung Chromebook Plus).
* **Allwinner A64:** `[█████████░]` 88%
  * Quad-core Cortex-A53 @ 1.15 GHz (The open-source Linux laptop engine powering the 11.6" and 14" Pine64 Pinebook).
