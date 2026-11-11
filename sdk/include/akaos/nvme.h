
#ifndef AKAOS_NVME_H
#define AKAOS_NVME_H

#include <stdint.h>

struct nvme_smart_log {
    uint8_t critical_warning;
    uint8_t temperature[2];
    uint8_t available_spare;
    uint8_t available_spare_threshold;
    uint8_t percentage_used;
    uint8_t reserved0[26];
    uint64_t data_units_read[2];
    uint64_t data_units_written[2];
    uint64_t host_read_commands[2];
    uint64_t host_write_commands[2];
    uint64_t controller_busy_time[2];
    uint64_t power_cycles[2];
    uint64_t power_on_hours[2];
    uint64_t unsafe_shutdowns[2];
    uint64_t media_errors[2];
    uint64_t number_of_error_info_log_entries[2];
    uint32_t warning_composite_temperature_time;
    uint32_t critical_composite_temperature_time;
    uint16_t temperature_sensor[8];
    uint8_t reserved1[296];
};


struct nvme_dsm_range {
    uint32_t context_attributes;
    uint32_t length;
    uint64_t starting_lba;
};

#endif
