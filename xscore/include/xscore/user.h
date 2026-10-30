#ifndef XSCORE_USER_H
#define XSCORE_USER_H

int xscore_user_process_self_test(void);

/* Native ARM/RISC-V BSP-only ABI probe. It validates isolated-root
 * activation before secondary migration is enabled; trap-return safety is
 * covered by the native parity boots plus the compile-time context checks. */
int xscore_user_native_context_self_test(void);

#endif
