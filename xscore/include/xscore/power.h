#ifndef XSCORE_POWER_H
#define XSCORE_POWER_H

#include <stdint.h>
#include <stddef.h>

enum xscore_cpu_governor {
    CPU_GOV_PERFORMANCE = 0,
    CPU_GOV_POWERSAVE,
    CPU_GOV_ONDEMAND,
    CPU_GOV_MAX
};

struct xscore_battery_info {
    int present;
    int charging;
    int percentage;
    int ac_online;
    int temp_celsius;
    char technology[16];
};

/* CPU Frequency & DVFS Management */
int xscore_cpufreq_set_governor(int governor);
int xscore_cpufreq_get_governor(void);
const char *xscore_cpufreq_governor_name(int governor);
uint32_t xscore_cpufreq_get_mhz(void);
void xscore_cpufreq_scale_load(int load_percentage);

/* C-State Idle Optimization */
void xscore_power_idle_cstate(int cstate);

/* Battery & Thermal Telemetry */
int xscore_power_get_battery(struct xscore_battery_info *info);

/* Telemetry Formatters for ProcFS / SysFS */
size_t xscore_power_format_proc_battery(char *buf, size_t max_size);
size_t xscore_power_format_proc_thermal(char *buf, size_t max_size);

#endif /* XSCORE_POWER_H */
