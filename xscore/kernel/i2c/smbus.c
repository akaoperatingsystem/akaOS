#include <xscore/i2c_smbus.h>
#include <xscore/io.h>

int smbus_read_word(uint16_t base, uint8_t addr, uint8_t command, uint16_t *value) {
    /* Clear status */
    port_out8(base + I801_SMBHSTSTS, 0xFF);
    
    /* Set address and command */
    port_out8(base + I801_SMBHSTADD, (addr << 1) | 1);
    port_out8(base + I801_SMBHSTCMD, command);
    
    /* Start read word command */
    port_out8(base + I801_SMBHSTCNT, 0x0B); 
    
    /* Wait for completion */
    while (!(port_in8(base + I801_SMBHSTSTS) & 0x02));
    
    *value = port_in16(base + I801_SMBHSTDAT0);
    return 1;
}
