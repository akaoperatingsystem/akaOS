#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

typedef struct {
    char *ifile;
    char *ofile;
    size_t bs;
    size_t count;
    off_t skip;
    off_t seek;
} dd_args;

static void parse_args(int argc, char **argv, dd_args *args) {
    args->ifile = NULL;
    args->ofile = NULL;
    args->bs = 512;
    args->count = (size_t)-1;
    args->skip = 0;
    args->seek = 0;

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "if=", 3) == 0) args->ifile = argv[i] + 3;
        else if (strncmp(argv[i], "of=", 3) == 0) args->ofile = argv[i] + 3;
        else if (strncmp(argv[i], "bs=", 3) == 0) args->bs = atol(argv[i] + 3);
        else if (strncmp(argv[i], "count=", 6) == 0) args->count = atol(argv[i] + 6);
        else if (strncmp(argv[i], "skip=", 5) == 0) args->skip = atol(argv[i] + 5);
        else if (strncmp(argv[i], "seek=", 5) == 0) args->seek = atol(argv[i] + 5);
    }
}

int main(int argc, char **argv) {
    dd_args args;
    parse_args(argc, argv, &args);

    int ifd = args.ifile ? open(args.ifile, O_RDONLY) : STDIN_FILENO;
    int ofd = args.ofile ? open(args.ofile, O_WRONLY | O_CREAT | O_TRUNC, 0666) : STDOUT_FILENO;

    if (ifd < 0 || ofd < 0) {
        perror("dd: open");
        return 1;
    }

    if (args.skip > 0) lseek(ifd, args.skip * args.bs, SEEK_SET);
    if (args.seek > 0) lseek(ofd, args.seek * args.bs, SEEK_SET);

    char *buf = malloc(args.bs);
    size_t copied = 0;
    size_t blocks_in = 0;
    size_t blocks_out = 0;

    while (copied < args.count) {
        ssize_t n = read(ifd, buf, args.bs);
        if (n <= 0) break;
        blocks_in++;
        
        ssize_t w = write(ofd, buf, n);
        if (w != n) {
            perror("dd: write");
            break;
        }
        blocks_out++;
        copied++;
    }

    printf("%zu+%zu records in\n", blocks_in, 0UL);
    printf("%zu+%zu records out\n", blocks_out, 0UL);

    free(buf);
    if (ifd != STDIN_FILENO) close(ifd);
    if (ofd != STDOUT_FILENO) close(ofd);

    return 0;
}
