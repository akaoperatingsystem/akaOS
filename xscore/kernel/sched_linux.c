#include <shim/linux/kernel/sched/sched.h>
#include <xscore/thread.h>
#include <xscore/serial.h>

void schedule(void) {
    xscore_serial_write("[INFO] Linux scheduler hook: schedule() invoked -> yielding thread\n");
    xscore_thread_yield();
}
