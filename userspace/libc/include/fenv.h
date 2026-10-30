#ifndef _FENV_H
#define _FENV_H

typedef int fenv_t;
typedef unsigned int fexcept_t;

#define FE_TONEAREST  0
#define FE_DOWNWARD   1
#define FE_UPWARD     2
#define FE_TOWARDZERO 3

#define FE_INVALID    0x01
#define FE_DIVBYZERO  0x02
#define FE_OVERFLOW   0x04
#define FE_UNDERFLOW  0x08
#define FE_INEXACT    0x10
#define FE_ALL_EXCEPT (FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW | \
                       FE_UNDERFLOW | FE_INEXACT)

#define FE_DFL_ENV ((const fenv_t *)0)

#ifdef __cplusplus
extern "C" {
#endif

int fegetround(void);
int fesetround(int round);
int feclearexcept(int excepts);
int fetestexcept(int excepts);
int fegetexceptflag(fexcept_t *flagp, int excepts);
int fesetexceptflag(const fexcept_t *flagp, int excepts);
int fedisableexcept(int excepts);
int feenableexcept(int excepts);
int fegetenv(fenv_t *envp);
int fesetenv(const fenv_t *envp);
int feholdexcept(fenv_t *envp);
int feupdateenv(const fenv_t *envp);

#ifdef __cplusplus
}
#endif

#endif
