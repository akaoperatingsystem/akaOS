#ifndef _SHIM_LINUX_MMC_SDHCI_H
#define _SHIM_LINUX_MMC_SDHCI_H

#include <shim/linux/types.h>

struct sdhci_host;

int sdhci_add_host(struct sdhci_host *host);
void sdhci_remove_host(struct sdhci_host *host, int dead);

#endif /* _SHIM_LINUX_MMC_SDHCI_H */
