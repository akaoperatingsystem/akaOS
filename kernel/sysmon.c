/* ============================================================
 * akaOS — System Monitor Implementation
 * ============================================================ */
#include "arch.h"
#include "sysmon.h"
#include "string.h"
#include "time.h"
#include "fs.h"

extern uint32_t sys_total_memory_mb;

/* Hardware state */
static int cpu_cores = 1;

/* Execution trackers (using rdtsc) */
static uint64_t last_tsc_tick = 0;
static uint64_t idle_ticks_acc = 0;
static uint64_t irq_ticks_acc = 0;
static uint64_t current_idle_start = 0;
static uint64_t current_irq_start = 0;

/* Smooth load calculation */
static uint32_t last_cpu_load = 0;

/* Global OR OS Process Table */
static struct sys_proc os_process_table[MAX_PROCS];
static int os_process_count = 0;
static int next_pid = 100;

int os_process_spawn(const char *name, const char *user, uint32_t mem_kb) {
    if (os_process_count >= MAX_PROCS) return -1;
    struct sys_proc *p = &os_process_table[os_process_count++];
    p->pid = next_pid++;
    strncpy(p->name, name, 15);
    p->name[15] = '\0';
    strncpy(p->user, user, 15);
    p->user[15] = '\0';
    strcpy(p->state, "S");
    p->mem_kb = mem_kb;
    p->cpu_percent = 0;
    return p->pid;
}

void os_process_kill(int pid) {
    int found = -1;
    for (int i = 0; i < os_process_count; i++) {
        if (os_process_table[i].pid == pid) {
            found = i;
            break;
        }
    }
    if (found != -1) {
        for (int i = found; i < os_process_count - 1; i++) {
            os_process_table[i] = os_process_table[i + 1];
        }
        os_process_count--;
    }
}

void os_process_set_state(int pid, const char *state) {
    for (int i = 0; i < os_process_count; i++) {
        if (os_process_table[i].pid == pid) {
            strncpy(os_process_table[i].state, state, 3);
            os_process_table[i].state[3] = '\0';
            break;
        }
    }
}

void os_process_set_cpu(int pid, uint32_t cpu_percent) {
    for (int i = 0; i < os_process_count; i++) {
        if (os_process_table[i].pid == pid) {
            os_process_table[i].cpu_percent = cpu_percent;
            break;
        }
    }
}

/* Helper to read the Time Stamp Counter (or arch equivalent) */
static inline uint64_t rdtsc(void) {
    return arch_rdtsc();
}

static void detect_cores(void) {
#if defined(ARCH_X86_64) || defined(ARCH_X86_32)
    uint32_t eax, ebx, ecx, edx;
    /* CPUID level 1: EBX[23:16] = logical processors */
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    int cores = (ebx >> 16) & 0xFF;
    if (cores < 1) cores = 1;
    if (cores > MAX_CORES) cores = MAX_CORES;
    cpu_cores = cores;
#else
    cpu_cores = 1;
#endif
}

void sysmon_init(void) {
    detect_cores();
    last_tsc_tick = rdtsc();

    // Initialize core OS processes
    os_process_count = 0;
    
    struct sys_proc *p;
    
    // 0: idle
    p = &os_process_table[os_process_count++];
    p->pid = 0; strcpy(p->name, "idle"); strcpy(p->user, "root"); strcpy(p->state, "S");
    p->mem_kb = 0; p->cpu_percent = 0;

    // 1: init
    p = &os_process_table[os_process_count++];
    p->pid = 1; strcpy(p->name, "init"); strcpy(p->user, "root"); strcpy(p->state, "S");
    p->mem_kb = 120; p->cpu_percent = 0;

    // 2: gui_wm
    p = &os_process_table[os_process_count++];
    p->pid = 2; strcpy(p->name, "gui_wm"); strcpy(p->user, "root"); strcpy(p->state, "R");
    p->mem_kb = 3872; p->cpu_percent = 0;

    // 3: net_e1000
    p = &os_process_table[os_process_count++];
    p->pid = 3; strcpy(p->name, "net_e1000"); strcpy(p->user, "root"); strcpy(p->state, "S");
    p->mem_kb = 1024; p->cpu_percent = 0;
    
    // 4: vfs_ramfs
    // memory assigned in sysmon_update
    p = &os_process_table[os_process_count++];
    p->pid = 4; strcpy(p->name, "vfs_ramfs"); strcpy(p->user, "root"); strcpy(p->state, "S");
    p->mem_kb = 0; p->cpu_percent = 0;
}

void sysmon_record_idle_start(void) { current_idle_start = rdtsc(); }
void sysmon_record_idle_end(void) { 
    if (current_idle_start) idle_ticks_acc += (rdtsc() - current_idle_start); 
}

void sysmon_record_irq_start(void) { current_irq_start = rdtsc(); }
void sysmon_record_irq_end(void) { 
    if (current_irq_start) irq_ticks_acc += (rdtsc() - current_irq_start); 
}

void sysmon_update(struct sys_metrics *m) {
    if (!m) return;
    
    /* 1. CPU Load Calculation */
    uint64_t now_tsc = rdtsc();
    uint64_t diff = now_tsc - last_tsc_tick;
    last_tsc_tick = now_tsc;

    if (diff > 0) {
        /* CPU usage is the proportion of time NOT spent in the idle loop */
        uint64_t active_ticks = diff > idle_ticks_acc ? diff - idle_ticks_acc : 0;
        uint32_t raw_load = (uint32_t)((active_ticks * 100) / diff);
        
        /* Add some synthetic noise so the graph looks alive (HTOP style) */
        uint32_t noise = (timer_get_ticks() % 5);
        uint32_t load = raw_load + noise;
        if (load > 100) load = 100;
        
        /* Smooth it */
        last_cpu_load = (last_cpu_load * 3 + load) / 4;
    }
    idle_ticks_acc = 0;
    irq_ticks_acc = 0;

    m->core_count = cpu_cores;
    /* Map the load across cores. AkaOS only runs on Core 0 right now, 
       but we simulate slight background noise on the other cores. */
    m->cpu_load_percent[0] = last_cpu_load;
    for (int i = 1; i < cpu_cores; i++) {
        m->cpu_load_percent[i] = (timer_get_ticks() + i * 17) % 7;
    }

    /* 2. Memory Usage */
    m->mem_total_kb = sys_total_memory_mb * 1024;
    if (m->mem_total_kb == 0) m->mem_total_kb = 128 * 1024; /* Fallback */
    
    /* Kernel baseline (~2MB) + Framebuffer (3MB) + FS pool */
    uint32_t base_kb = 5120;
    uint32_t fs_kb = fs_get_node_count() * 1; /* Approx 1KB per node in total overhead */
    m->mem_used_kb = base_kb + fs_kb;

    /* 3. Processes (Managed List) */
    m->proc_count = os_process_count;
    
    /* Update core process dynamic stats */
    for (int i = 0; i < os_process_count; i++) {
        if (os_process_table[i].pid == 0) { // idle
            os_process_table[i].cpu_percent = 100 - last_cpu_load;
        } else if (os_process_table[i].pid == 2) { // gui_wm
            os_process_table[i].cpu_percent = last_cpu_load > 2 ? last_cpu_load - 2 : 0;
        } else if (os_process_table[i].pid == 4) { // vfs_ramfs
            os_process_table[i].mem_kb = fs_kb;
        }
    }

    extern volatile int doom_running;
    if (doom_running) {
        for (int i = 0; i < os_process_count; i++) {
            if (strncmp(os_process_table[i].name, "doom", 4) == 0) {
                os_process_table[i].cpu_percent = (doom_running == 2) ? (last_cpu_load > 10 ? last_cpu_load - 5 : last_cpu_load) : 0;
                if (doom_running == 3) strcpy(os_process_table[i].state, "Z");
                else if (doom_running == 1 || doom_running == 2) strcpy(os_process_table[i].state, "R");
                else strcpy(os_process_table[i].state, "S");
                break;
            }
        }
    }

    for (int i = 0; i < m->proc_count; i++) {
        m->procs[i] = os_process_table[i];
    }
}
