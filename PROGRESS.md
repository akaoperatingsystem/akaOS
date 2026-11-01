# akaOS Development Progress

[![akaOS Version](https://img.shields.io/badge/akaOS-v0.1.22-blue.svg)](sdk/include/akaos/version.h)
[![XSCore Version](https://img.shields.io/badge/XSCore-v0.1.12-green.svg)](sdk/include/akaos/version.h)

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
| **Memory Management** | 4-Level Paging, Physical Page Allocator, NX, Guard Stacks, COW, Demand Paging, 32MB Heap & mmap Fallback | 96% | `[██████████]` 96% |
| **Process & Scheduling** | Preemptive Multitasking, Local APIC Timer, POSIX Fork/Execve, Signals, Expanded Process Table (64) | 92% | `[█████████░]` 92% |
| **GPU & Display Drivers** | Framebuffer (`/dev/fb0`), Lumen, X11 Server (64MB Backing, XYBitmap/PutImage, SHAPE, X-Res), Mode Query | 94% | `[█████████░]` 94% |
| **Storage & akaFS Overlay** | VirtIO-Block, IDE/ATA PIO Fallback, Persistent Flash Overlay VFS | 85% | `[████████░░]` 85% |
| **Native libc & Linker** | Freestanding C Runtime, Dynamic Linker (`ld-akaos`), POSIX ABI (daemon, getopt, getgroups, umask, stat) | 88% | `[█████████░]` 88% |
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
| **XFCE Desktop & Xfwm4** | Unmodified Upstream (libxfce4util, xfconf, libxfce4ui, xfwm4) | 80% | `[████████░░]` 80% |
| **MATE Desktop & Marco** | Unmodified Upstream | 30% | `[███░░░░░░░]` 30% |
| **KDE Plasma & KWin** | Unmodified Upstream | 25% | `[██░░░░░░░░]` 25% |
| **GNOME Desktop & Mutter** | Unmodified Upstream | 20% | `[██░░░░░░░░]` 20% |
| **Cinnamon Desktop & Muffin** | Unmodified Upstream | 20% | `[██░░░░░░░░]` 20% |
| **Overall System Completion** | Full Native Operating System | 65% | `[██████░░░░]` 65% |


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
