#include <unistd.h>

int qtxdg_mat_cpp_main(int argc, char *argv[]);

extern "C" int main(int argc, char *argv[]) {
#ifdef AKAOS_QTXDG_TRACE_MAIN
    static const char marker[] = "qtxdg-mat: CRT wrapper entered\n";
    (void)write(2, marker, sizeof(marker) - 1U);
#endif
    return qtxdg_mat_cpp_main(argc, argv);
}
