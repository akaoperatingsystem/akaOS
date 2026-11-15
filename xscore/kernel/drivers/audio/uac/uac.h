
#ifndef UAC_DRIVER_H
#define UAC_DRIVER_H

#include <stdint.h>

typedef struct {
    uint8_t interface_num;
    uint8_t alt_setting;
    uint32_t terminal_link;
} uac_stream_t;

void uac_init(void);
void uac_start_stream(uac_stream_t *stream);

#endif
