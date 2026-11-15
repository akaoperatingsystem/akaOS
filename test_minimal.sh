
#!/bin/sh
for cmd in [ about acpi akafetch akaos-config akash alias apropos arp aspell at atq atrm aurora awk base32 base64 basename bash batch bc beep bg bind break builtin bunzip2 bzcat bzip2 cairo-upstream-test cal caller case cat cd chmod chown clear cmp cp curl cut cxx-dynamic-test cxxabi-test dbus-daemon dbus-update-activation-environment desktop df diff dig; do
    echo "Testing $cmd"
    ./build/AKASH.ELF -c "$cmd --help" || ./build/AKASH.ELF -c "$cmd"
done
