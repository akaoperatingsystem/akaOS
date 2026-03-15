#ifndef _SIGNAL_H_
#define _SIGNAL_H_

#define SIGINT  2
#define SIGTERM 15
#define SIGQUIT 3
#define SIG_DFL ((void(*)(int))0)
#define SIG_IGN ((void(*)(int))1)

typedef void (*sighandler_t)(int);
sighandler_t signal(int signum, sighandler_t handler);

#endif
