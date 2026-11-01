# Global CPU & SoC Architecture Architecture & Identification

XSCore provides hardware model identification across multiple CPU instruction set architectures.

## Supported Microarchitectures & Model Matching
- **x86-64 / x86-32**: Model extraction via CPUID leaf `0x80000002-0x80000004` brand strings, vendor signature matching (GenuineIntel, AuthenticAMD, Zhaoxin, Hygon, CentaurHauls).
- **ARM64 / ARM32**: Extraction via MIDR_EL1 (Main ID Register), implementer codes, part numbers (Cortex-A, Cortex-X, Neoverse, Apple Silicon, Qualcomm Oryon/Kryo).
- **RISC-V (64 & 32)**: Device-tree `compatible` strings, mvendorid, marchid, and mimpid CSR parsing for SiFive, XuanTie, and StarFive cores.

Detailed hardware support tables for Intel, AMD, Qualcomm, MediaTek, Rockchip, Allwinner, Broadcom, NVIDIA Grace, Apple, Samsung, Unisoc, and RISC-V are documented in **[PROGRESS.md](../../PROGRESS.md#comprehensive-global-cpu--soc-model-directory)**.
