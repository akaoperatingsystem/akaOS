
#include <xscore/drivers/audio/hda/hda.h>
#include <xscore/drivers/audio/ac97/ac97.h>
#include <xscore/drivers/audio/uac/uac.h>

void audio_subsystem_init(void) {
    // Probe and initialize HDA
    // Probe and initialize AC97
    // Probe and initialize UAC
    audio_init(); // Register VFS nodes
}
