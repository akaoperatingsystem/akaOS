#include <xscore/power.h>
#include <xscore/i2c_smbus.h>

#define SBS_ADDR 0x0B
#define SBS_CMD_VOLTAGE 0x09
#define SBS_CMD_CURRENT 0x0A
#define SBS_CMD_CAPACITY 0x0F
#define SBS_CMD_DESIGN_CAPACITY 0x18
#define SBS_CMD_CYCLE_COUNT 0x17

// Assume a base address for now, usually discovered via PCI
#define I801_BASE 0x400

int sbs_get_telemetry(uint16_t smbus_base, struct xscore_battery_info *info) {
    if (!info) return 0;
    
    uint16_t val;
    // Real-world implementation would iterate and handle SMBus bus errors
    if (smbus_read_word(smbus_base, SBS_ADDR, SBS_CMD_VOLTAGE, &val)) info->temp_celsius = val / 1000;
    if (smbus_read_word(smbus_base, SBS_ADDR, SBS_CMD_CAPACITY, &val)) info->percentage = (val * 100) / 5000;
    
    info->present = 1;
    info->charging = 1;
    info->ac_online = 1;
    
    const char tech[] = "Li-ion";
    for (int i = 0; i < 6; i++) info->technology[i] = tech[i];
    
    return 1;
}
