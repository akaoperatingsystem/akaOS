
#ifndef REALTEK_CODEC_H
#define REALTEK_CODEC_H

#include <stdint.h>

void realtek_codec_init(uint32_t codec_addr);
void realtek_set_volume(uint32_t nid, uint8_t volume);

#endif
