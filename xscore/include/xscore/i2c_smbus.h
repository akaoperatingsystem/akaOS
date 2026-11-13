#ifndef XSCORE_I2C_SMBUS_H
#define XSCORE_I2C_SMBUS_H

#include <stdint.h>

/* Intel i801 SMBus Controller Registers */
#define I801_SMBHSTSTS    0x00
#define I801_SMBHSTCNT    0x02
#define I801_SMBHSTCMD    0x03
#define I801_SMBHSTADD    0x04
#define I801_SMBHSTDAT0   0x05
#define I801_SMBHSTDAT1   0x06
#define I801_SMBBLKDAT    0x07

/* SMBus Commands */
int smbus_read_word(uint16_t base, uint8_t addr, uint8_t command, uint16_t *value);
int smbus_write_word(uint16_t base, uint8_t addr, uint8_t command, uint16_t value);

#endif
