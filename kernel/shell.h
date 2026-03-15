/* ============================================================
 * akaOS — Shell Header
 * ============================================================ */
#ifndef SHELL_H
#define SHELL_H

/* Run the text-mode shell (blocking, for non-GUI mode) */
void shell_run(void);

/* Execute a single command (used by GUI terminal) */
void shell_execute_one(const char *input);

/* Print the shell prompt */
void shell_print_prompt(void);

/* History access (used by GUI terminal) */
int  shell_history_count(void);
const char *shell_history_get(int idx);

#endif
