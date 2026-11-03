#!/usr/bin/env bash
# Rebuild only the X11 display server and refresh the bootable images that
# carry it.
#
# The display server changes far more often than the rest of the system, and a
# complete build recompiles the whole user space, the kernel and the ISO.  This
# script repeats only the steps that the display server can invalidate:
#
#   1. the display server objects and its link,
#   2. the initramfs archive, with the new server overlaid on the old one,
#   3. the kernel objects and the kernel link, because the initramfs is
#      embedded into the kernel image,
#   4. the ISO, because the kernel is embedded into the ISO.
#
# The kernel source list and the user space library list are read out of
# build.sh, so this script cannot drift away from the real build.
set -euo pipefail

# The build host can carry include and pkg-config search paths for an unrelated
# toolchain.  Clang honours CPATH even under -nostdinc, so it is cleared here
# as well.
unset CPATH
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH
unset OBJC_INCLUDE_PATH
unset LIBRARY_PATH
unset PKG_CONFIG_PATH

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
SOURCE_DIR="$ROOT_DIR/userspace/bin/xserver"
GENERATED_DIR="$BUILD_DIR/generated"
INCLUDE_DIR="$ROOT_DIR/sdk/include"
USER_INCLUDE_DIR="$ROOT_DIR/userspace/libc/include"
BUILD_TIMESTAMP="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

for tool in clang ld.lld objcopy python3; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Missing build tool: $tool" >&2
        exit 2
    fi
done

if [[ ! -f "$BUILD_DIR/initramfs.img" ]]; then
    echo "Missing $BUILD_DIR/initramfs.img; run ./build.sh first." >&2
    exit 2
fi

# Read a shell array of source paths out of build.sh, so that the lists here
# always match the ones the real build uses.  The paths in build.sh carry the
# root directory as a variable, and the variable has to be expanded here: a
# path that does not exist is never newer than an object, and the object would
# be left untouched while the source changed.
read_build_array() {
    local name="$1"
    sed -n "/^${name}=(/,/^)/p" "$ROOT_DIR/build.sh" |
        sed -e '1d' -e '$d' -e 's/^[[:space:]]*"//' -e 's/"[[:space:]]*$//' |
        grep -v '^$' |
        sed -e "s|\\\$ROOT_DIR|$ROOT_DIR|g"
}

# ---------------------------------------------------------------- display server
echo "[BUILD] X11 display server"
objects=()
for unit in main client window events draw resource input setup xcore; do
    object="$BUILD_DIR/user-xserver-$unit.o"
    clang --target=x86_64-unknown-none-elf \
        -std=c17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -mno-red-zone -mcmodel=small \
        -DXSCORE_BUILD_TIMESTAMP="\"$BUILD_TIMESTAMP\"" \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" -I"$GENERATED_DIR" \
        -I"$SOURCE_DIR/include" \
        -c "$SOURCE_DIR/$unit.c" -o "$object"
    objects+=("$object")
done

user_libc_objects=()
while IFS= read -r source; do
    object="$BUILD_DIR/user-libc-$(basename "${source%.c}").o"
    # The library is part of the display server, so a change in it has to
    # reach the image as well.
    if [[ ! -f "$object" || "$source" -nt "$object" ]]; then
        clang --target=x86_64-unknown-none-elf \
            -std=c17 -O2 -Wall -Wextra -Werror \
            -ffreestanding -fno-builtin -fno-stack-protector -fpie \
            -mno-red-zone -mcmodel=small \
            -DXSCORE_BUILD_TIMESTAMP="\"$BUILD_TIMESTAMP\"" \
            -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
            -c "$source" -o "$object"
    fi
    user_libc_objects+=("$object")
done < <(read_build_array USER_LIBC_SOURCES)

if [[ ! -f "$BUILD_DIR/user-crt0.o" ]]; then
    clang --target=x86_64-unknown-none-elf \
        -ffreestanding -fpie -mno-red-zone -mcmodel=small \
        -c "$ROOT_DIR/userspace/libc/crt/x86_64/crt0.S" \
        -o "$BUILD_DIR/user-crt0.o"
fi

ld.lld -nostdlib -pie -static -z max-page-size=0x1000 \
    -T "$ROOT_DIR/userspace/linker/user.ld" \
    "$BUILD_DIR/user-crt0.o" "${user_libc_objects[@]}" "${objects[@]}" \
    -o "$BUILD_DIR/XSERVER.ELF"

# ------------------------------------------------------- dynamic loader and libc
# The dynamic loader is the program that loads every upstream desktop binary,
# and it statically links the C library.  A change in the loader or in the
# library therefore has to reach the image together with the display server,
# or the guest keeps running the old loader and the new trace never appears.
echo "[BUILD] shared C library"
shared_objects=()
while IFS= read -r source; do
    object="$BUILD_DIR/shared-libc-$(basename "${source%.c}").o"
    if [[ ! -f "$object" || "$source" -nt "$object" ]]; then
        clang --target=x86_64-unknown-none-elf \
            -std=c17 -O2 -Wall -Wextra -Werror \
            -ffreestanding -fno-builtin -fno-stack-protector -fPIC \
            -mno-red-zone -mcmodel=small \
            -DXSCORE_BUILD_TIMESTAMP="\"$BUILD_TIMESTAMP\"" \
            -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
            -c "$source" -o "$object"
    fi
    shared_objects+=("$object")
done < <(read_build_array USER_LIBC_SOURCES)

signal_object="$BUILD_DIR/shared-libc-signal.o"
clang --target=x86_64-unknown-none-elf \
    -ffreestanding -fPIC -mno-red-zone -mcmodel=small \
    -c "$ROOT_DIR/userspace/libc/tests/shared_signal.S" \
    -o "$signal_object"

ld.lld -m elf_x86_64 -nostdlib -shared -soname libc.so.6 \
    --hash-style=gnu -z max-page-size=0x1000 --no-undefined \
    -Bsymbolic-functions \
    "${shared_objects[@]}" "$signal_object" -o "$BUILD_DIR/libc.so.6"

echo "[BUILD] dynamic loader"
clang --target=x86_64-unknown-none-elf \
    -std=c17 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fno-builtin -fno-stack-protector -fpie \
    -mno-red-zone -mcmodel=small \
    -DXSCORE_BUILD_TIMESTAMP="\"$BUILD_TIMESTAMP\"" \
    -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
    -c "$ROOT_DIR/userspace/bin/ld-akaos/main.c" \
    -o "$BUILD_DIR/user-ld-akaos-main.o"

ld.lld -nostdlib -pie -static -z max-page-size=0x1000 \
    -T "$ROOT_DIR/userspace/linker/user.ld" \
    "$BUILD_DIR/user-crt0.o" "${user_libc_objects[@]}" \
    "$BUILD_DIR/user-ld-akaos-main.o" \
    -o "$BUILD_DIR/LD_AKAOS.ELF"

# ------------------------------------------------- thread compatibility library
# The upstream builds ask for a library under the name of the thread library,
# and a program links its libraries in the order of its own list, so the first
# library that answers for an entry point wins.  The library under that name
# therefore carries the marker of the runtime ABI and nothing else, and never
# takes the place of the real GLib of an upstream desktop program.
echo "[BUILD] thread compatibility library"
if [[ ! -f "$BUILD_DIR/runtime-io.o" ]]; then
    clang --target=x86_64-unknown-none-elf \
        -std=c17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fPIC \
        -mno-red-zone -mcmodel=small \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$ROOT_DIR/userspace/libc/tests/desktop_io.c" \
        -o "$BUILD_DIR/runtime-io.o"
fi
clang --target=x86_64-unknown-none-elf \
    -std=c17 -Os -Wall -Wextra -Werror \
    -ffreestanding -fno-builtin -fno-stack-protector -fPIC \
    -mno-red-zone -mcmodel=small \
    -DAKAOS_RUNTIME_ID=1 \
    -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
    -c "$ROOT_DIR/userspace/libc/tests/desktop_runtime_thread.c" \
    -o "$BUILD_DIR/runtime-1.o"

ld.lld -m elf_x86_64 -nostdlib -shared -soname libpthread.so \
    -z max-page-size=0x1000 \
    "$BUILD_DIR/runtime-1.o" "$BUILD_DIR/runtime-io.o" \
    -o "$BUILD_DIR/libpthread.so"

# ------------------------------------------------------------- upstream programs
# The upstream desktop programs and the libraries they link are staged in the
# image from the graphics sysroot.  A rebuild of one of them has to reach the
# image as well, or the guest keeps running the program of the last build.
UPSTREAM_SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
initramfs_files=(
    "--file=/bin/xserver:$BUILD_DIR/XSERVER.ELF:exec"
    "--file=/lib/ld-akaos.so:$BUILD_DIR/LD_AKAOS.ELF:exec"
    "--file=/lib/libc.so.6:$BUILD_DIR/libc.so.6:0444"
    "--file=/lib/libc.so:$BUILD_DIR/libc.so.6:0444"
    "--file=/lib/libpthread.so:$BUILD_DIR/libpthread.so:0444"
    "--file=/lib/libpthread.so.0:$BUILD_DIR/libpthread.so:0444"
)
# The upstream X11 client libraries are staged by build.sh.  A rebuilt
# library has to reach the image as well, or the guest keeps running the old
# one while the tests exercise the new source.
for x11_library in \
    libX11.so.6 libXext.so.6 libXRes.so.1 libXrender.so.1 libXfixes.so.3 \
    libXdamage.so.1 libXcomposite.so.1 libXi.so.6 libXrandr.so.2 \
    libXcursor.so.1 libXtst.so.6 libxcb.so.1 libXau.so.6 \
    libXdmcp.so.6 libXft.so.2 libXinerama.so.1 ; do
    x11_library_path="$UPSTREAM_SYSROOT/usr/local/lib/$x11_library"
    if [[ -f "$x11_library_path" ]]; then
        initramfs_files+=(
            "--file=/lib/$x11_library:$x11_library_path:0444"
        )
    fi
done

# The X11 probe programs are built by build.sh.  Stage them again so a
# rebuilt probe reaches the image together with the display server.
if [[ -f "$BUILD_DIR/X11_CLIENT.ELF" ]]; then
    initramfs_files+=(
        "--file=/bin/x11-client:$BUILD_DIR/X11_CLIENT.ELF:exec"
    )
fi
if [[ -f "$UPSTREAM_SYSROOT/usr/local/lib/libX11.so.6" ]]; then
    echo "[BUILD] X11 window manager probe"
    pkg_env=(
        "PKG_CONFIG_LIBDIR=$UPSTREAM_SYSROOT/usr/local/lib/pkgconfig:$UPSTREAM_SYSROOT/usr/local/share/pkgconfig:$UPSTREAM_SYSROOT/usr/lib/pkgconfig"
        "PKG_CONFIG_SYSROOT_DIR=$UPSTREAM_SYSROOT"
    )
    read -r -a wm_cflags <<< "$(env "${pkg_env[@]}" pkg-config --cflags x11)"
    read -r -a wm_libs <<< "$(env "${pkg_env[@]}" pkg-config --libs x11)"
    wm_link_args=()
    for arg in "${wm_libs[@]}"; do
        [[ "$arg" == "-pthread" || "$arg" == "-lm" ]] || wm_link_args+=("$arg")
    done
    clang --target=x86_64-unknown-none-elf \
        -std=gnu17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -fno-stack-protector -fpie \
        -mno-red-zone -mcmodel=small "${wm_cflags[@]}" \
        -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
        -c "$ROOT_DIR/userspace/bin/x11-wm-client/main.c" \
        -o "$BUILD_DIR/x11-wm-client.o"
    ld.lld -m elf_x86_64 -nostdlib -pie --eh-frame-hdr -z max-page-size=0x1000 \
        --dynamic-linker /lib/ld-akaos.so \
        -T "$ROOT_DIR/userspace/linker/user-dynamic.ld" \
        "$BUILD_DIR/user-dynamic-crt0.o" "$BUILD_DIR/x11-wm-client.o" \
        --no-as-needed "${wm_link_args[@]}" \
        "$BUILD_DIR/libm.so.6" "$BUILD_DIR/libc.so.6" \
        -o "$BUILD_DIR/X11_WM_CLIENT.ELF"
fi
if [[ -f "$BUILD_DIR/X11_WM_CLIENT.ELF" ]]; then
    initramfs_files+=(
        "--file=/bin/x11-wm-client:$BUILD_DIR/X11_WM_CLIENT.ELF:exec"
    )
fi

echo "[BUILD] user grep"
clang --target=x86_64-unknown-none-elf \
    -std=c17 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fno-builtin -fno-stack-protector -fpie \
    -mno-red-zone -mcmodel=small \
    -DXSCORE_BUILD_TIMESTAMP="\"$BUILD_TIMESTAMP\"" \
    -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
    -c "$ROOT_DIR/userspace/bin/grep/main.c" -o "$BUILD_DIR/user-grep-main.o"
ld.lld -nostdlib -pie -static -z max-page-size=0x1000 \
    -T "$ROOT_DIR/userspace/linker/user.ld" \
    "$BUILD_DIR/user-crt0.o" "${user_libc_objects[@]}" "$BUILD_DIR/user-grep-main.o" \
    -o "$BUILD_DIR/GREP.ELF"
initramfs_files+=(
    "--file=/bin/grep:$BUILD_DIR/GREP.ELF:exec"
)

echo "[BUILD] user akash"
clang --target=x86_64-unknown-none-elf \
    -std=c17 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fno-builtin -fno-stack-protector -fpie \
    -mno-red-zone -mcmodel=small \
    -DXSCORE_BUILD_TIMESTAMP="\"$BUILD_TIMESTAMP\"" \
    -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
    -c "$ROOT_DIR/userspace/bin/akash/main.c" -o "$BUILD_DIR/user-akash-main.o"
ld.lld -nostdlib -pie -static -z max-page-size=0x1000 \
    -T "$ROOT_DIR/userspace/linker/user.ld" \
    "$BUILD_DIR/user-crt0.o" "${user_libc_objects[@]}" "$BUILD_DIR/user-akash-main.o" \
    -o "$BUILD_DIR/AKASH.ELF"
initramfs_files+=(
    "--file=/bin/akash:$BUILD_DIR/AKASH.ELF:exec"
    "--file=/bin/sh:$BUILD_DIR/AKASH.ELF:exec"
)

echo "[BUILD] user ps"
clang --target=x86_64-unknown-none-elf \
    -std=c17 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fno-builtin -fno-stack-protector -fpie \
    -mno-red-zone -mcmodel=small \
    -DXSCORE_BUILD_TIMESTAMP="\"$BUILD_TIMESTAMP\"" \
    -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
    -c "$ROOT_DIR/userspace/bin/ps/main.c" -o "$BUILD_DIR/user-ps-main.o"
ld.lld -nostdlib -pie -static -z max-page-size=0x1000 \
    -T "$ROOT_DIR/userspace/linker/user.ld" \
    "$BUILD_DIR/user-crt0.o" "${user_libc_objects[@]}" "$BUILD_DIR/user-ps-main.o" \
    -o "$BUILD_DIR/PS.ELF"
initramfs_files+=(
    "--file=/bin/ps:$BUILD_DIR/PS.ELF:exec"
)

echo "[BUILD] user dbus-daemon"
clang --target=x86_64-unknown-none-elf \
    -std=c17 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fno-builtin -fno-stack-protector -fpie \
    -mno-red-zone -mcmodel=small \
    -DXSCORE_BUILD_TIMESTAMP="\"$BUILD_TIMESTAMP\"" \
    -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
    -c "$ROOT_DIR/userspace/bin/dbus-daemon/main.c" -o "$BUILD_DIR/user-dbus-daemon-main.o"
ld.lld -nostdlib -pie -static -z max-page-size=0x1000 \
    -T "$ROOT_DIR/userspace/linker/user.ld" \
    "$BUILD_DIR/user-crt0.o" "${user_libc_objects[@]}" "$BUILD_DIR/user-dbus-daemon-main.o" \
    -o "$BUILD_DIR/DBUS_DAEMON.ELF"
initramfs_files+=(
    "--file=/bin/dbus-daemon:$BUILD_DIR/DBUS_DAEMON.ELF:exec"
)

echo "[BUILD] user init"
clang --target=x86_64-unknown-none-elf \
    -std=c17 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fno-builtin -fno-stack-protector -fpie \
    -mno-red-zone -mcmodel=small \
    -DXSCORE_BUILD_TIMESTAMP=""$BUILD_TIMESTAMP"" \
    -I"$INCLUDE_DIR" -I"$USER_INCLUDE_DIR" \
    -c "$ROOT_DIR/userspace/bin/init/main.c" -o "$BUILD_DIR/user-init-main.o"
ld.lld -nostdlib -pie -static -z max-page-size=0x1000 \
    -T "$ROOT_DIR/userspace/linker/user.ld" \
    "$BUILD_DIR/user-crt0.o" "${user_libc_objects[@]}" "$BUILD_DIR/user-init-main.o" \
    -o "$BUILD_DIR/INIT.ELF"
initramfs_files+=(
    "--file=/bin/init:$BUILD_DIR/INIT.ELF:exec"
)

echo "[BUILD] staging core graphics & GUI runtime libraries"
for core_lib in \
    libgdk_pixbuf-2.0.so.0 libgtk-3.so.0 libgdk-3.so.0 libatk-1.0.so.0 \
    libpango-1.0.so.0 libpangocairo-1.0.so.0 libcairo.so.2 libcairo-gobject.so.2 \
    libharfbuzz.so.0 libfreetype.so.6 libfontconfig.so.1 libfribidi.so.0 \
    libpixman-1.so.0 libpng16.so.16 libgraphene-1.0.so.0 libepoxy.so.0; do
    library_path="$(find "$UPSTREAM_SYSROOT/usr/local/lib" "$UPSTREAM_SYSROOT/usr/lib" \
        -maxdepth 1 -type f -name "${core_lib}.*" -print | sort -V | tail -n 1)"
    if [[ -z "$library_path" ]]; then
        library_path="$(find "$UPSTREAM_SYSROOT/usr/local/lib" "$UPSTREAM_SYSROOT/usr/lib" \
            -maxdepth 1 -type f -name "${core_lib}" -print | head -n 1)"
    fi
    [[ -n "$library_path" ]] || continue
    initramfs_files+=(
        "--file=/lib/$core_lib:$library_path:0444"
        "--file=/usr/local/lib/$core_lib:$library_path:0444"
    )
done

if [[ -f "$UPSTREAM_SYSROOT/usr/local/bin/openbox" ]]; then
    echo "[BUILD] upstream window manager"
    initramfs_files+=(
        "--file=/usr/local/bin/openbox:$UPSTREAM_SYSROOT/usr/local/bin/openbox:exec"
    )
    for openbox_library in libobt.so.2 libobrender.so.32 libxml2.so.2 \
        libpangoxft-1.0.so.0 libpango-1.0.so.0 libXft.so.2 libglib-2.0.so.0; do
        library_path="$(find "$UPSTREAM_SYSROOT/usr/local/lib" \
            -maxdepth 1 -type f -name "${openbox_library}.*" -print | sort -V | tail -n 1)"
        [[ -n "$library_path" ]] || continue
        initramfs_files+=(
            "--file=/lib/$openbox_library:$library_path:0444"
            "--file=/usr/local/lib/$openbox_library:$library_path:0444"
        )
    done
fi

if [[ -f "$UPSTREAM_SYSROOT/usr/local/bin/lxpanel" ]]; then
    echo "[BUILD] upstream LXDE panel"
    initramfs_files+=(
        "--dir=/etc"
        "--file=/etc/dbus-services.conf:$ROOT_DIR/userspace/etc/dbus-services.conf:0644"
        "--file=/etc/test-desktop.sh:$ROOT_DIR/tools/test-desktop.sh:0755"
        "--dir=/run"
        "--dir=/run/dbus"
        "--dir=/run/user"
        "--dir=/run/user/0"
        "--dir=/etc/xdg"
        "--dir=/etc/xdg/menus"
        "--dir=/usr/share/desktop-directories"
        "--dir=/usr/local/share/desktop-directories"
        "--dir=/etc/xdg/lxpanel"
        "--dir=/etc/xdg/lxpanel/default"
        "--dir=/etc/xdg/lxpanel/default/panels"
        "--dir=/etc/xdg/lxpanel/two_panels"
        "--dir=/etc/xdg/lxpanel/two_panels/panels"
        "--dir=/usr/local/share/lxpanel"
        "--dir=/usr/local/share/lxpanel/images"
        "--dir=/usr/local/share/lxpanel/ui"
        "--dir=/usr/local/share/libfm"
        "--dir=/usr/local/share/libfm/images"
        "--dir=/usr/local/share/libfm/ui"
        "--dir=/usr/local/lib/lxpanel"
        "--dir=/usr/local/lib/lxpanel/plugins"
        "--dir=/usr/local/lib/libfm"
        "--dir=/usr/local/lib/libfm/modules"
        "--dir=/usr/local/libexec"
        "--dir=/usr/local/libexec/menu-cache"
        "--file=/usr/local/libexec/menu-cache/menu-cached:$UPSTREAM_SYSROOT/usr/local/libexec/menu-cache/menu-cached:exec"
        "--file=/usr/local/libexec/menu-cache/menu-cache-gen:$UPSTREAM_SYSROOT/usr/local/libexec/menu-cache/menu-cache-gen:exec"
        "--file=/usr/local/bin/lxpanel:$UPSTREAM_SYSROOT/usr/local/bin/lxpanel:exec"
        "--file=/usr/local/bin/lxpanelctl:$UPSTREAM_SYSROOT/usr/local/bin/lxpanelctl:exec"
        "--file=/lib/liblxpanel.so.0:$UPSTREAM_SYSROOT/usr/local/lib/lxpanel/liblxpanel.so.0.0.0:0444"
        "--file=/etc/xdg/lxpanel/default/config:$UPSTREAM_SYSROOT/etc/xdg/lxpanel/default/config:0444"
        "--file=/etc/xdg/lxpanel/default/panels/panel:$UPSTREAM_SYSROOT/etc/xdg/lxpanel/default/panels/panel:0444"
        "--file=/etc/xdg/lxpanel/two_panels/config:$UPSTREAM_SYSROOT/etc/xdg/lxpanel/two_panels/config:0444"
        "--file=/etc/xdg/lxpanel/two_panels/panels/bottom:$UPSTREAM_SYSROOT/etc/xdg/lxpanel/two_panels/panels/bottom:0444"
        "--file=/etc/xdg/lxpanel/two_panels/panels/top:$UPSTREAM_SYSROOT/etc/xdg/lxpanel/two_panels/panels/top:0444"
    )
    while IFS= read -r lxpanel_plugin; do
        plugin_name="${lxpanel_plugin##*/}"
        initramfs_files+=(
            "--file=/usr/local/lib/lxpanel/plugins/$plugin_name:$lxpanel_plugin:0444"
        )
    done < <(find "$UPSTREAM_SYSROOT/usr/local/lib/lxpanel/plugins" -maxdepth 1 -type f -name '*.so' -print 2>/dev/null | sort)
    while IFS= read -r fm_module; do
        mod_name="${fm_module##*/}"
        initramfs_files+=(
            "--file=/usr/local/lib/libfm/modules/$mod_name:$fm_module:0444"
        )
    done < <(find "$UPSTREAM_SYSROOT/usr/local/lib/libfm/modules" -maxdepth 1 -type f -name '*.so' -print 2>/dev/null | sort)
    for lxpanel_library in libwnck-3.so.0 libfm-gtk3.so.4 libfm.so.4 \
        libfm-extra.so.4 libmenu-cache.so.3 libkeybinder-3.0.so.0; do
        library_path="$(find "$UPSTREAM_SYSROOT/usr/local/lib" \
            -maxdepth 1 -type f -name "${lxpanel_library}.*" -print | sort -V | tail -n 1)"
        [[ -n "$library_path" ]] || continue
        initramfs_files+=(
            "--file=/lib/$lxpanel_library:$library_path:0444"
            "--file=/usr/local/lib/$lxpanel_library:$library_path:0444"
        )
    done
    while IFS= read -r data_file; do
        relative_path="${data_file#"$UPSTREAM_SYSROOT/usr/local/"}"
        initramfs_files+=(
            "--file=/usr/local/$relative_path:$data_file:0444"
        )
    done < <(find "$UPSTREAM_SYSROOT/usr/local/share/lxpanel/images" \
                  "$UPSTREAM_SYSROOT/usr/local/share/lxpanel/ui" \
                  "$UPSTREAM_SYSROOT/usr/local/share/libfm" \
                  -maxdepth 1 -type f | sort)
    while IFS= read -r data_file; do
        relative_path="${data_file#"$UPSTREAM_SYSROOT/usr/local/"}"
        initramfs_files+=(
            "--file=/usr/local/$relative_path:$data_file:0444"
        )
    done < <(find "$UPSTREAM_SYSROOT/usr/local/share/libfm/images" \
                  "$UPSTREAM_SYSROOT/usr/local/share/libfm/ui" \
                  -maxdepth 1 -type f | sort)
    if [[ -d "$UPSTREAM_SYSROOT/etc/xdg/menus" ]]; then
        for menu_file in "$UPSTREAM_SYSROOT"/etc/xdg/menus/*.menu; do
            [[ -f "$menu_file" ]] || continue
            initramfs_files+=(
                "--file=/etc/xdg/menus/${menu_file##*/}:$menu_file:0444"
            )
        done
    fi
    if [[ -d "$UPSTREAM_SYSROOT/usr/local/share/desktop-directories" ]]; then
        for dir_file in "$UPSTREAM_SYSROOT"/usr/local/share/desktop-directories/*.directory; do
            [[ -f "$dir_file" ]] || continue
            initramfs_files+=(
                "--file=/usr/share/desktop-directories/${dir_file##*/}:$dir_file:0444"
                "--file=/usr/local/share/desktop-directories/${dir_file##*/}:$dir_file:0444"
            )
        done
    fi
fi

# ------------------------------------------------------------- upstream PCManFM & LXSession
if [[ -f "$UPSTREAM_SYSROOT/usr/local/bin/pcmanfm" ]]; then
    echo "[BUILD] upstream PCManFM file manager"
    initramfs_files+=(
        "--file=/usr/local/bin/pcmanfm:$UPSTREAM_SYSROOT/usr/local/bin/pcmanfm:exec"
        "--dir=/etc/xdg/pcmanfm"
        "--dir=/etc/xdg/pcmanfm/default"
        "--dir=/etc/xdg/pcmanfm/LXDE"
        "--dir=/usr/local/share/pcmanfm"
        "--dir=/usr/local/share/pcmanfm/ui"
    )
    if [[ -f "$UPSTREAM_SYSROOT/etc/xdg/pcmanfm/default/pcmanfm.conf" ]]; then
        initramfs_files+=(
            "--file=/etc/xdg/pcmanfm/default/pcmanfm.conf:$UPSTREAM_SYSROOT/etc/xdg/pcmanfm/default/pcmanfm.conf:0444"
        )
    fi
    if [[ -f "$UPSTREAM_SYSROOT/etc/xdg/pcmanfm/LXDE/pcmanfm.conf" ]]; then
        initramfs_files+=(
            "--file=/etc/xdg/pcmanfm/LXDE/pcmanfm.conf:$UPSTREAM_SYSROOT/etc/xdg/pcmanfm/LXDE/pcmanfm.conf:0444"
        )
    fi
    while IFS= read -r ui_file; do
        relative_path="${ui_file#"$UPSTREAM_SYSROOT/usr/local/"}"
        initramfs_files+=(
            "--file=/usr/local/$relative_path:$ui_file:0444"
        )
    done < <(find "$UPSTREAM_SYSROOT/usr/local/share/pcmanfm/ui" -maxdepth 1 -type f 2>/dev/null | sort)
fi

if [[ -f "$UPSTREAM_SYSROOT/usr/local/bin/startlxde" ]]; then
    echo "[BUILD] upstream LXDE common session scripts & config"
    initramfs_files+=(
        "--file=/usr/local/bin/startlxde:$UPSTREAM_SYSROOT/usr/local/bin/startlxde:exec"
        "--file=/usr/local/bin/openbox-lxde:$UPSTREAM_SYSROOT/usr/local/bin/openbox-lxde:exec"
        "--file=/usr/local/bin/lxde-logout:$UPSTREAM_SYSROOT/usr/local/bin/lxde-logout:exec"
        "--dir=/etc/xdg/lxsession"
        "--dir=/etc/xdg/lxsession/LXDE"
        "--dir=/etc/xdg/openbox/LXDE"
        "--dir=/etc/xdg/lxpanel/LXDE"
        "--dir=/etc/xdg/lxpanel/LXDE/panels"
        "--dir=/usr/local/share/lxde"
        "--dir=/usr/local/share/lxde/images"
        "--dir=/usr/local/share/lxde/wallpapers"
        "--dir=/usr/local/share/xsessions"
    )
    if [[ -f "$UPSTREAM_SYSROOT/etc/xdg/lxsession/LXDE/autostart" ]]; then
        initramfs_files+=(
            "--file=/etc/xdg/lxsession/LXDE/autostart:$UPSTREAM_SYSROOT/etc/xdg/lxsession/LXDE/autostart:0444"
            "--file=/etc/xdg/lxsession/LXDE/desktop.conf:$UPSTREAM_SYSROOT/etc/xdg/lxsession/LXDE/desktop.conf:0444"
        )
    fi
    if [[ -f "$UPSTREAM_SYSROOT/etc/xdg/openbox/LXDE/rc.xml" ]]; then
        initramfs_files+=(
            "--file=/etc/xdg/openbox/LXDE/rc.xml:$UPSTREAM_SYSROOT/etc/xdg/openbox/LXDE/rc.xml:0444"
            "--file=/etc/xdg/openbox/LXDE/menu.xml:$UPSTREAM_SYSROOT/etc/xdg/openbox/LXDE/menu.xml:0444"
        )
    fi
    if [[ -f "$UPSTREAM_SYSROOT/etc/xdg/lxpanel/LXDE/config" ]]; then
        initramfs_files+=(
            "--file=/etc/xdg/lxpanel/LXDE/config:$UPSTREAM_SYSROOT/etc/xdg/lxpanel/LXDE/config:0444"
            "--file=/etc/xdg/lxpanel/LXDE/panels/panel:$UPSTREAM_SYSROOT/etc/xdg/lxpanel/LXDE/panels/panel:0444"
        )
    fi
    while IFS= read -r share_file; do
        relative_path="${share_file#"$UPSTREAM_SYSROOT/usr/local/"}"
        initramfs_files+=(
            "--file=/usr/local/$relative_path:$share_file:0444"
        )
    done < <(find "$UPSTREAM_SYSROOT/usr/local/share/lxde" "$UPSTREAM_SYSROOT/usr/local/share/xsessions" -type f 2>/dev/null | sort)
fi

if [[ -f "$UPSTREAM_SYSROOT/usr/local/bin/lxsession" ]]; then
    echo "[BUILD] upstream LXSession binaries"
    initramfs_files+=(
        "--file=/usr/local/bin/lxsession:$UPSTREAM_SYSROOT/usr/local/bin/lxsession:exec"
        "--file=/usr/local/bin/lxsettings-daemon:$UPSTREAM_SYSROOT/usr/local/bin/lxsettings-daemon:exec"
        "--file=/usr/local/bin/lxsession-xdg-autostart:$UPSTREAM_SYSROOT/usr/local/bin/lxsession-xdg-autostart:exec"
        "--file=/usr/local/bin/lxsession-logout:$UPSTREAM_SYSROOT/usr/local/bin/lxsession-logout:exec"
        "--file=/usr/local/bin/lxsession-edit:$UPSTREAM_SYSROOT/usr/local/bin/lxsession-edit:exec"
        "--file=/usr/local/bin/lxsession-db:$UPSTREAM_SYSROOT/usr/local/bin/lxsession-db:exec"
        "--file=/usr/local/bin/lxlock:$UPSTREAM_SYSROOT/usr/local/bin/lxlock:exec"
        "--file=/usr/local/bin/lxsession-default:$UPSTREAM_SYSROOT/usr/local/bin/lxsession-default:exec"
        "--file=/usr/local/bin/lxsession-default-terminal:$UPSTREAM_SYSROOT/usr/local/bin/lxsession-default-terminal:exec"
        "--file=/usr/local/libexec/lxsession-xsettings:$UPSTREAM_SYSROOT/usr/local/libexec/lxsession-xsettings:exec"
        "--dir=/usr/local/share/lxsession"
        "--dir=/usr/local/share/lxsession/ui"
        "--dir=/usr/local/share/lxsession/images"
    )
    while IFS= read -r share_file; do
        relative_path="${share_file#"$UPSTREAM_SYSROOT/usr/local/"}"
        initramfs_files+=(
            "--file=/usr/local/$relative_path:$share_file:0444"
        )
    done < <(find "$UPSTREAM_SYSROOT/usr/local/share/lxsession" -type f 2>/dev/null | sort)
fi

# ------------------------------------------------------------- upstream XFCE & xfwm4
if [[ -f "$UPSTREAM_SYSROOT/usr/local/bin/xfwm4" ]]; then
    echo "[BUILD] upstream XFCE components & xfwm4"
    initramfs_files+=(
        "--file=/usr/local/bin/xfwm4:$UPSTREAM_SYSROOT/usr/local/bin/xfwm4:exec"
        "--dir=/usr/local/share/xfwm4"
        "--dir=/usr/local/share/themes"
    )
    for xfce_library in libxfce4util.so.7 libxfconf-0.so.3 libxfce4ui-2.so.0 libxfce4kbd-private-3.so.0 \
        libgarcon-1.so.0 libgarcon-gtk3-1.so.0 libexo-2.so.0 libxfce4panel-2.0.so.4 \
        libICE.so.6 libSM.so.6; do
        library_path="$(find "$UPSTREAM_SYSROOT/usr/local/lib"             -maxdepth 1 -type f -name "${xfce_library}.*" -print | sort -V | tail -n 1)"
        [[ -n "$library_path" ]] || continue
        initramfs_files+=(
            "--file=/lib/$xfce_library:$library_path:0444"
            "--file=/usr/local/lib/$xfce_library:$library_path:0444"
        )
    done
    if [[ -f "$UPSTREAM_SYSROOT/usr/local/bin/xfce4-panel" ]]; then
        initramfs_files+=(
            "--file=/usr/local/bin/xfce4-panel:$UPSTREAM_SYSROOT/usr/local/bin/xfce4-panel:exec"
            "--file=/usr/local/lib/xfce4/panel/wrapper-2.0:$UPSTREAM_SYSROOT/usr/local/lib/xfce4/panel/wrapper-2.0:exec"
            "--file=/usr/local/lib/xfce4/panel/migrate:$UPSTREAM_SYSROOT/usr/local/lib/xfce4/panel/migrate:exec"
            "--dir=/usr/local/lib/xfce4/panel"
            "--dir=/usr/local/lib/xfce4/panel/plugins"
            "--dir=/usr/local/share/xfce4"
            "--dir=/usr/local/share/xfce4/panel"
            "--dir=/usr/local/share/xfce4/panel/plugins"
            "--dir=/etc/xdg/xfce4"
            "--dir=/etc/xdg/xfce4/panel"
        )
        if [[ -f "$UPSTREAM_SYSROOT/etc/xdg/xfce4/panel/default.xml" ]]; then
            initramfs_files+=(
                "--file=/etc/xdg/xfce4/panel/default.xml:$UPSTREAM_SYSROOT/etc/xdg/xfce4/panel/default.xml:0444"
            )
        fi
        while IFS= read -r plugin_file; do
            plugin_name="${plugin_file##*/}"
            initramfs_files+=(
                "--file=/usr/local/lib/xfce4/panel/plugins/$plugin_name:$plugin_file:0444"
            )
        done < <(find "$UPSTREAM_SYSROOT/usr/local/lib/xfce4/panel/plugins" -maxdepth 1 -type f -name '*.so' 2>/dev/null | sort)
        while IFS= read -r desktop_file; do
            desktop_name="${desktop_file##*/}"
            initramfs_files+=(
                "--file=/usr/local/share/xfce4/panel/plugins/$desktop_name:$desktop_file:0444"
            )
        done < <(find "$UPSTREAM_SYSROOT/usr/local/share/xfce4/panel/plugins" -maxdepth 1 -type f -name '*.desktop' 2>/dev/null | sort)
    fi
    if [[ -f "$UPSTREAM_SYSROOT/usr/local/bin/xfce4-session" ]]; then
        initramfs_files+=(
            "--file=/usr/local/bin/xfce4-session:$UPSTREAM_SYSROOT/usr/local/bin/xfce4-session:exec"
            "--file=/usr/local/bin/xfce4-session-logout:$UPSTREAM_SYSROOT/usr/local/bin/xfce4-session-logout:exec"
            "--file=/usr/local/bin/startxfce4:$UPSTREAM_SYSROOT/usr/local/bin/startxfce4:exec"
            "--dir=/etc/xdg/xfce4"
        )
        if [[ -f "$UPSTREAM_SYSROOT/etc/xdg/xfce4/xinitrc" ]]; then
            initramfs_files+=(
                "--file=/etc/xdg/xfce4/xinitrc:$UPSTREAM_SYSROOT/etc/xdg/xfce4/xinitrc:0444"
            )
        fi
        if [[ -f "$UPSTREAM_SYSROOT/etc/xdg/xfce4/Xft.xrdb" ]]; then
            initramfs_files+=(
                "--file=/etc/xdg/xfce4/Xft.xrdb:$UPSTREAM_SYSROOT/etc/xdg/xfce4/Xft.xrdb:0444"
            )
        fi
    fi
    if [[ -f "$UPSTREAM_SYSROOT/usr/local/bin/xfconf-query" ]]; then
        initramfs_files+=(
            "--file=/usr/local/bin/xfconf-query:$UPSTREAM_SYSROOT/usr/local/bin/xfconf-query:exec"
            "--file=/usr/local/lib/xfce4/xfconf/xfconfd:$UPSTREAM_SYSROOT/usr/local/lib/xfce4/xfconf/xfconfd:exec"
            "--dir=/usr/local/lib/xfce4"
            "--dir=/usr/local/lib/xfce4/xfconf"
            "--dir=/usr/local/share/dbus-1"
            "--dir=/usr/local/share/dbus-1/services"
        )
        if [[ -f "$UPSTREAM_SYSROOT/usr/local/share/dbus-1/services/org.xfce.Xfconf.service" ]]; then
            initramfs_files+=(
                "--file=/usr/local/share/dbus-1/services/org.xfce.Xfconf.service:$UPSTREAM_SYSROOT/usr/local/share/dbus-1/services/org.xfce.Xfconf.service:0444"
            )
        fi
    fi
    while IFS= read -r share_file; do
        relative_path="${share_file#"$UPSTREAM_SYSROOT/usr/local/"}"
        initramfs_files+=(
            "--file=/usr/local/$relative_path:$share_file:0444"
        )
    done < <(find "$UPSTREAM_SYSROOT/usr/local/share/xfwm4" "$UPSTREAM_SYSROOT/usr/local/share/themes" -type f 2>/dev/null | sort)
fi

# ------------------------------------------------------------- upstream MATE & marco
if [[ -f "$UPSTREAM_SYSROOT/usr/local/bin/marco" ]]; then
    echo "[BUILD] upstream MATE components & marco"
    initramfs_files+=(
        "--file=/usr/local/bin/marco:$UPSTREAM_SYSROOT/usr/local/bin/marco:exec"
        "--dir=/usr/local/share/themes"
        "--dir=/usr/local/share/glib-2.0"
        "--dir=/usr/local/share/glib-2.0/schemas"
    )
    for mate_bin in mate-panel mate-session mate-session-save mate-session-inhibit mate-wm dconf; do
        if [[ -f "$UPSTREAM_SYSROOT/usr/local/bin/$mate_bin" ]]; then
            initramfs_files+=(
                "--file=/usr/local/bin/$mate_bin:$UPSTREAM_SYSROOT/usr/local/bin/$mate_bin:exec"
            )
        fi
    done
    for mate_libexec in clock-applet wnck-applet notification-area-applet fish-applet dconf-service; do
        if [[ -f "$UPSTREAM_SYSROOT/usr/local/libexec/$mate_libexec" ]]; then
            initramfs_files+=(
                "--file=/usr/local/libexec/$mate_libexec:$UPSTREAM_SYSROOT/usr/local/libexec/$mate_libexec:exec"
            )
        fi
    done
    for mate_library in libmarco-private.so.2 libcanberra-gtk3.so.0 libcanberra.so.0 libvorbisfile.so.3 libvorbis.so.0 libogg.so.0 libltdl.so.7 \
        libmate-desktop-2.so.17 libmate-menu.so.2 libmate-panel-applet-4.so.1 libmateweather.so.1 libdconf.so.1 libdbus-glib-1.so.2; do
        library_path="$(find "$UPSTREAM_SYSROOT/usr/local/lib"             -maxdepth 1 -type f -name "${mate_library}.*" -print | sort -V | tail -n 1)"
        [[ -n "$library_path" ]] || continue
        initramfs_files+=(
            "--file=/lib/$mate_library:$library_path:0444"
            "--file=/usr/local/lib/$mate_library:$library_path:0444"
        )
    done
    while IFS= read -r schema_file; do
        schema_name="${schema_file##*/}"
        initramfs_files+=(
            "--file=/usr/local/share/glib-2.0/schemas/$schema_name:$schema_file:0444"
        )
    done < <(find "$UPSTREAM_SYSROOT/usr/local/share/glib-2.0/schemas" -maxdepth 1 -type f 2>/dev/null | sort)
fi

# ------------------------------------------------------------- upstream Cinnamon & muffin
if [[ -f "$UPSTREAM_SYSROOT/usr/local/bin/muffin" ]]; then
    echo "[BUILD] upstream Cinnamon components & muffin"
    initramfs_files+=(
        "--file=/usr/local/bin/muffin:$UPSTREAM_SYSROOT/usr/local/bin/muffin:exec"
    )
    for cinnamon_bin in cinnamon-session cinnamon-session-quit; do
        if [[ -f "$UPSTREAM_SYSROOT/usr/local/bin/$cinnamon_bin" ]]; then
            initramfs_files+=(
                "--file=/usr/local/bin/$cinnamon_bin:$UPSTREAM_SYSROOT/usr/local/bin/$cinnamon_bin:exec"
            )
        fi
    done
    for cinnamon_library in libmuffin.so.0 libmuffin-cogl-0.so.0 libmuffin-clutter-0.so.0 \
        libmuffin-cogl-pango-0.so.0 libmuffin-cogl-path-0.so.0 \
        libcinnamon-desktop.so.4 libcvc.so.0 libjson-glib-1.0.so.0 libxkbfile.so.1 \
        libxkbcommon-x11.so.0 libEGL.so.1 libGLdispatch.so.0 libGL.so.1 libGLESv2.so.2 libGLX.so.0; do
        library_path="$(find "$UPSTREAM_SYSROOT/usr/local/lib" \
            -maxdepth 1 -type f -name "${cinnamon_library}.*" -print | sort -V | tail -n 1)"
        if [[ -z "$library_path" ]]; then
            library_path="$(find "$UPSTREAM_SYSROOT/usr/local/lib" \
                -maxdepth 1 -type f -name "${cinnamon_library}" -print | head -n 1)"
        fi
        [[ -n "$library_path" ]] || continue
        initramfs_files+=(
            "--file=/lib/$cinnamon_library:$library_path:0444"
            "--file=/usr/local/lib/$cinnamon_library:$library_path:0444"
        )
    done
fi

# -------------------------------------------------------------------- initramfs
echo "[BUILD] initramfs"
python3 "$ROOT_DIR/tools/mkinitramfs.py" "$BUILD_DIR/initramfs.img.new" \
    --base "$BUILD_DIR/initramfs.img" \
    "${initramfs_files[@]}" \
    --exclude-long-vfs-names
mv -f "$BUILD_DIR/initramfs.img.new" "$BUILD_DIR/initramfs.img"

# ----------------------------------------------------------------------- kernel
echo "[BUILD] XSCore kernel"
kernel_objects=()
while IFS= read -r source; do
    object="$BUILD_DIR/$(basename "${source%.c}").o"
    if [[ ! -f "$source" ]]; then
        echo "The kernel source list names a file that does not exist: $source" >&2
        exit 2
    fi
    if [[ ! -f "$object" || "$source" -nt "$object" ]]; then
        clang --target=x86_64-unknown-none-elf \
            -std=c17 -O2 -Wall -Wextra -Werror \
            -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
            -mno-red-zone -mcmodel=kernel -mno-sse -mno-mmx -mno-80387 \
            -DXSCORE_BUILD_TIMESTAMP="\"$BUILD_TIMESTAMP\"" \
            -I"$INCLUDE_DIR" -I"$ROOT_DIR/xscore/include" -I"$GENERATED_DIR" \
            -c "$source" -o "$object"
    fi
    kernel_objects+=("$object")
done < <(read_build_array XSCORE_C_SOURCES)

for assembly in "$ROOT_DIR/xscore/arch/x86_64/entry.S" \
                "$ROOT_DIR/xscore/arch/x86_64/smp_trampoline.S"; do
    object="$BUILD_DIR/$(basename "${assembly%.S}").o"
    if [[ ! -f "$object" || "$assembly" -nt "$object" ]]; then
        clang --target=x86_64-unknown-none-elf \
            -ffreestanding -fno-pic -fno-pie -mno-red-zone -mcmodel=kernel \
            -c "$assembly" -o "$object"
    fi
    kernel_objects+=("$object")
done

(cd "$BUILD_DIR" && objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
    initramfs.img initramfs_archive.o)
kernel_objects+=("$BUILD_DIR/initramfs_archive.o")

ld.lld -nostdlib -static -z max-page-size=0x1000 \
    -T "$ROOT_DIR/xscore/arch/x86_64/linker.ld" \
    "${kernel_objects[@]}" -o "$BUILD_DIR/XSCORE.ELF"

ld.lld -nostdlib -static -z max-page-size=0x1000 \
    -e xscore_multiboot_entry \
    -T "$ROOT_DIR/xscore/arch/x86_64/linker.ld" \
    "${kernel_objects[@]}" -o "$BUILD_DIR/XSCORE-BIOS.ELF"

# ------------------------------------------------------------------------- ISO
echo "[BUILD] dual-firmware ISO"
bash "$ROOT_DIR/tools/build-bios.sh" --no-build
