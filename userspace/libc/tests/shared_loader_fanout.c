/* The caller's dependency list is deliberately larger than the old four
 * DT_NEEDED slots.  Keep the return value sensitive to every provider so the
 * integration check proves that all five relocations resolve and execute. */
extern int akaos_loader_dependency_0(void);
extern int akaos_loader_dependency_1(void);
extern int akaos_loader_dependency_2(void);
extern int akaos_loader_dependency_3(void);
extern int akaos_loader_dependency_4(void);

int akaos_loader_dependency_fanout(void) {
    return akaos_loader_dependency_0() +
           akaos_loader_dependency_1() +
           akaos_loader_dependency_2() +
           akaos_loader_dependency_3() +
           akaos_loader_dependency_4();
}
