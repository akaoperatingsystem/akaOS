#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>

#define TYPEMATIC_DELAY_250MS  0x00
#define TYPEMATIC_DELAY_500MS  0x20
#define TYPEMATIC_DELAY_750MS  0x40
#define TYPEMATIC_DELAY_1000MS 0x60

#define TYPEMATIC_RATE_30HZ    0x00
#define TYPEMATIC_RATE_20HZ    0x04
#define TYPEMATIC_RATE_15HZ    0x08
#define TYPEMATIC_RATE_10HZ    0x0C
#define TYPEMATIC_RATE_2HZ     0x1F

__attribute__((weak)) void xscore_keyboard_set_typematic(uint8_t delay_rate_byte) {
    (void)delay_rate_byte;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: kbdrate [rate_hz] [delay_ms]\n");
        return 1;
    }
    
    int rate = atoi(argv[1]);
    int delay = (argc > 2) ? atoi(argv[2]) : 500;
    
    uint8_t delay_rate = 0;
    if (delay <= 250) delay_rate |= TYPEMATIC_DELAY_250MS;
    else if (delay <= 500) delay_rate |= TYPEMATIC_DELAY_500MS;
    else if (delay <= 750) delay_rate |= TYPEMATIC_DELAY_750MS;
    else delay_rate |= TYPEMATIC_DELAY_1000MS;
    
    if (rate >= 30) delay_rate |= TYPEMATIC_RATE_30HZ;
    else if (rate >= 20) delay_rate |= TYPEMATIC_RATE_20HZ;
    else if (rate >= 15) delay_rate |= TYPEMATIC_RATE_15HZ;
    else if (rate >= 10) delay_rate |= TYPEMATIC_RATE_10HZ;
    else delay_rate |= TYPEMATIC_RATE_2HZ;
    
    xscore_keyboard_set_typematic(delay_rate);
    printf("Typematic set: Rate=%dHz, Delay=%dms\n", rate, delay);
    return 0;
}
