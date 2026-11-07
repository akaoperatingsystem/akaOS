#ifndef _SHIM_LINUX_FS_PROC_H
#define _SHIM_LINUX_FS_PROC_H

#include <shim/linux/types.h>

struct proc_dir_entry;
struct seq_operations;

struct proc_dir_entry *proc_create(const char *name, umode_t mode,
                                   struct proc_dir_entry *parent,
                                   const struct file_operations *proc_fops);
void proc_remove(struct proc_dir_entry *de);

#endif /* _SHIM_LINUX_FS_PROC_H */
