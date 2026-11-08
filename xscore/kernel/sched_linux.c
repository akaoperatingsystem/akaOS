#include <shim/linux/kernel/sched/sched.h>
#include <xscore/thread.h>

void schedule(void) {
    xscore_thread_yield();
}
