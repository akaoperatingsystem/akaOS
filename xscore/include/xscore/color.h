#ifndef XSCORE_COLOR_H
#define XSCORE_COLOR_H

/* ANSI Escape Codes for Foreground Colors */
#define ANSI_RESET          "\033[0m"
#define ANSI_BLACK          "\033[30m"
#define ANSI_RED            "\033[31m"
#define ANSI_GREEN          "\033[32m"
#define ANSI_YELLOW         "\033[33m"
#define ANSI_BLUE           "\033[34m"
#define ANSI_MAGENTA        "\033[35m"
#define ANSI_CYAN           "\033[36m"
#define ANSI_WHITE          "\033[37m"
#define ANSI_BRIGHT_RED     "\033[1;31m"

/* Standard Status Tags */
#define TAG_OK      "[" ANSI_GREEN " OK " ANSI_RESET "]"
#define TAG_INFO    "[" ANSI_CYAN "INFO" ANSI_RESET "]"
#define TAG_WARN    "[" ANSI_YELLOW "WARN" ANSI_RESET "]"
#define TAG_FAIL    "[" ANSI_BRIGHT_RED "FAIL" ANSI_RESET "]"
#define TAG_DEBUG   "[" ANSI_BLACK ";1mDEBUG" ANSI_RESET "]"

#endif /* XSCORE_COLOR_H */
