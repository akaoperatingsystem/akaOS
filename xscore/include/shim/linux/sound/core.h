#ifndef _SHIM_LINUX_SOUND_CORE_H
#define _SHIM_LINUX_SOUND_CORE_H

#include <shim/linux/types.h>

struct snd_card;
struct device;
struct module;

int snd_card_new(struct device *parent, int idx, const char *xid,
                 struct module *module, int extra_size,
                 struct snd_card **card_ret);
int snd_card_register(struct snd_card *card);
void snd_card_free(struct snd_card *card);

#endif /* _SHIM_LINUX_SOUND_CORE_H */
