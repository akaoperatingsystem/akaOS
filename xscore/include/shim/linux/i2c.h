#ifndef _SHIM_LINUX_I2C_H
#define _SHIM_LINUX_I2C_H

#include <shim/linux/types.h>

struct i2c_adapter;
struct i2c_client;
struct i2c_msg;

int i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num);
int i2c_add_adapter(struct i2c_adapter *adapter);
void i2c_del_adapter(struct i2c_adapter *adapter);

#endif /* _SHIM_LINUX_I2C_H */
