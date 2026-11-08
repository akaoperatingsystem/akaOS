#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>

#define CRON_DIR "/etc/cron.d"

typedef struct {
    int min, hour, day, month, dow;
    char command[256];
} job_t;

void run_job(job_t *job) {
    if (fork() == 0) {
        system(job->command);
        exit(0);
    }
}

int main() {
    printf("crond: starting\n");
    if (fork() > 0) exit(0);
    setsid();

    while (1) {
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);

        DIR *d = opendir(CRON_DIR);
        if (d) {
            struct dirent *ent;
            while ((ent = readdir(d)) != NULL) {
                if (ent->d_name[0] == '.') continue;
                
                char path[512];
                snprintf(path, sizeof(path), "%s/%s", CRON_DIR, ent->d_name);
                
                FILE *f = fopen(path, "r");
                if (!f) continue;
                
                job_t job;
                // Use a proper string for fscanf pattern to avoid newline issues in C source
                char format[32];
                snprintf(format, sizeof(format), "%%d %%d %%d %%d %%d %%%%[^\n]");
                
                while (fscanf(f, format, &job.min, &job.hour, &job.day, &job.month, &job.dow, job.command) == 6) {
                    if ((job.min == -1 || job.min == tm->tm_min) &&
                        (job.hour == -1 || job.hour == tm->tm_hour) &&
                        (job.day == -1 || job.day == tm->tm_mday) &&
                        (job.month == -1 || job.month == tm->tm_mon + 1) &&
                        (job.dow == -1 || job.dow == tm->tm_wday)) {
                        run_job(&job);
                    }
                }
                fclose(f);
            }
            closedir(d);
        }
        sleep(60);
    }
    return 0;
}
