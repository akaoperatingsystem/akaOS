
#include <xscore/vfs.h>
#include <xscore/drivers/audio/hda/hda.h>

static int audio_open(vfs_node_t *node, uint32_t flags) { return 0; }
static int audio_read(vfs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buffer) { return 0; }
static int audio_write(vfs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buffer) { return 0; }
static int audio_ioctl(vfs_node_t *node, uint32_t cmd, void *arg) { return 0; }

void audio_init(void) {
    vfs_register_device("/dev/audio", audio_open, audio_read, audio_write, audio_ioctl);
    vfs_register_device("/dev/dsp", audio_open, audio_read, audio_write, audio_ioctl);
}
