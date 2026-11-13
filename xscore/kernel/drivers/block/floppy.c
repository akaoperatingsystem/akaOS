#include <xscore/arch.h>
#include <xscore/ioctl.h>

// Basic Floppy driver framework
int floppy_ioctl(int request, void *arg) {
    switch (request) {
        case AKAOS_BLKFORMAT:
            // Formatting logic
            return 0;
        default:
            return -1;
    }
}
