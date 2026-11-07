#include <shim/linux/fs/ext2_fs.h>
#include <xscore/serial.h>

void ext2_bridge_init(void) {
    xscore_serial_write("[INFO] Initializing ext2 bridge shim...\n");
}
