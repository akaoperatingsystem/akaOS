/* ============================================================
 * akaOS — System Monitor (htop-like backend)
 * ============================================================ */
#ifndef SYSMON_H
#define SYSMON_H

#include <stdint.h>

#define MAX_CORES 64
#define MAX_PROCS 16

/* Static process structure to simulate htop rows */
struct sys_proc {
    int pid;
    char name[16];
    char user[16];
    char state[4]; /* R=Running, S=Sleeping, I=Idle */
    uint32_t mem_kb;
    uint32_t cpu_percent;
};

/* Real hardware metrics */
struct sys_metrics {
    int core_count;
    uint32_t cpu_load_percent[MAX_CORES];
    uint32_t mem_total_kb;
    uint32_t mem_used_kb;
    uint32_t uptime_ticks;
    int proc_count;
    struct sys_proc procs[MAX_PROCS];
};

/* Initialize sysmon */
void sysmon_init(void);

/* Called by the GUI to get the latest metrics */
void sysmon_update(struct sys_metrics *out_metrics);

/* Called by the PIT timer/idle loop to track execution time */
void sysmon_record_idle_start(void);
void sysmon_record_idle_end(void);
void sysmon_record_irq_start(void);
void sysmon_record_irq_end(void);

/* OS Logical Process Management */
int os_process_spawn(const char *name, const char *user, uint32_t mem_kb);
void os_process_kill(int pid);
void os_process_set_state(int pid, const char *state);
void os_process_set_cpu(int pid, uint32_t cpu_percent);

#endif
