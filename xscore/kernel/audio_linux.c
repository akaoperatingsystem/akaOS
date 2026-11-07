#include <shim/linux/sound/core.h>
#include <xscore/serial.h>

struct snd_card {
    int id;
    char xid[32];
};

struct device;
struct module;

int snd_card_new(struct device *parent, int idx, const char *xid,
                 struct module *module, int extra_size,
                 struct snd_card **card_ret) {
    (void)parent; (void)idx; (void)xid; (void)module; (void)extra_size;
    xscore_serial_write("[INFO] Linux sound: snd_card_new called\n");
    *card_ret = (struct snd_card *)0x1234;
    return 0;
}

int snd_card_register(struct snd_card *card) {
    (void)card;
    xscore_serial_write("[INFO] Linux sound: snd_card_register called\n");
    return 0;
}

void snd_card_free(struct snd_card *card) {
    (void)card;
    xscore_serial_write("[INFO] Linux sound: snd_card_free called\n");
}
