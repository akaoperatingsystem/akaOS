#!/usr/bin/env bash
# Build the upstream X.Org client stack for the akaOS x86-64 userland.
#
# The X11 client libraries are the shared prerequisite for the unmodified
# upstream LXDE, XFCE, MATE and Cinnamon sessions: their window managers
# (openbox, xfwm4, marco, muffin) are X11 clients, and GTK 3 currently ships
# with its X11 backend disabled on akaOS.
#
# This script cross-builds the X.Org libraries with autotools against the akaOS
# SDK and the shared akaOS libc, then publishes the staged tree into the
# graphics sysroot that build.sh consumes.
#
# The build runs in the Ubuntu ext4 cache by default because /mnt/c is a DrvFS
# mount and is far too slow for repeated autotools rebuilds.
#
# Environment:
#   AKAOS_X11_WORKROOT   work root for sources, build trees, stage and logs
#   AKAOS_X11_PACKAGES   comma separated subset of package names to build
#   AKAOS_X11_BUILD_JOBS parallel make jobs
set -euo pipefail
# The build host can carry include and pkg-config search paths for an unrelated
# toolchain.  Clang honours CPATH even under -nostdinc and pkg-config prefers
# PKG_CONFIG_PATH over PKG_CONFIG_LIBDIR, so both are cleared to keep the cross
# build self contained.
unset CPATH
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH
unset OBJC_INCLUDE_PATH
unset LIBRARY_PATH
unset PKG_CONFIG_PATH

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
SYSROOT="$BUILD_DIR/upstream-sysroot/graphics-x86_64"
WORKROOT="${AKAOS_X11_WORKROOT:-$HOME/.cache/akaos/x11}"
SRC_DIR="$WORKROOT/src"
BUILD_ROOT="$WORKROOT/build"
STAGE="$WORKROOT/stage"
# The X.Org packages are installed under the stage prefix so that libtool
# resolves the absolute library paths it records while linking.  The staged
# pkg-config files are rewritten to the runtime prefix when the tree is
# published into the graphics sysroot.
STAGE_PREFIX="$STAGE/usr/local"
LOG_DIR="$WORKROOT/logs"
CC_WRAPPER="$ROOT_DIR/tools/akaos-cc-x86_64.sh"
JOBS="${AKAOS_X11_BUILD_JOBS:-$(nproc)}"
LIB_URL="https://www.x.org/releases/individual/lib"
PROTO_URL="https://www.x.org/releases/individual/proto"
UTIL_URL="https://www.x.org/releases/individual/util"
XCB_URL="https://xorg.freedesktop.org/releases/individual/xcb"

for command_name in curl sha256sum tar clang pkg-config python3 make; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing upstream X11 build tool: $command_name" >&2
        echo "On Ubuntu: sudo apt install curl coreutils tar clang lld pkg-config python3 make" >&2
        exit 2
    fi
done
if [[ ! -x "$CC_WRAPPER" ]]; then
    echo "Missing cross compiler wrapper: $CC_WRAPPER" >&2
    exit 2
fi
if [[ ! -f "$BUILD_DIR/libc.so.6" || ! -f "$BUILD_DIR/libm.so.6" ]]; then
    echo "Missing shared akaOS libc in $BUILD_DIR; run build.sh first" >&2
    exit 2
fi

mkdir -p "$SRC_DIR" "$BUILD_ROOT" "$STAGE" "$LOG_DIR"

# The akaOS allocator returns a usable pointer for a zero-sized request, so
# the X.Org runtime test for malloc(0) is answered from the cache instead of
# running a program on the build host.
export xorg_cv_malloc0_returns_null=no

# name|version|tarball|url|sha256|extra configure arguments|installed marker
PACKAGES=(
    "xorg-macros|1.19.2|util-macros-1.19.2.tar.gz|$UTIL_URL/util-macros-1.19.2.tar.gz|9225c45c3de60faf971979a55a5536f3562baa4b6f02246c23e98ac0c09a75b7||xorg-macros.pc"
    "xtrans|1.6.0|xtrans-1.6.0.tar.gz|$LIB_URL/xtrans-1.6.0.tar.gz|936b74c60b19c317c3f3cb1b114575032528dbdaf428740483200ea874c2ca0a||xtrans.pc"
    "xorgproto|2024.1|xorgproto-2024.1.tar.gz|$PROTO_URL/xorgproto-2024.1.tar.gz|4f6b9b4faf91e5df8265b71843a91fc73dc895be6210c84117a996545df296ce||xproto.pc"
    "libpthread-stubs|0.5|libpthread-stubs-0.5.tar.gz|$LIB_URL/libpthread-stubs-0.5.tar.gz|593196cc746173d1e25cb54a93a87fd749952df68699aab7e02c085530e87747|-|pthread-stubs.pc"
    "libXau|1.0.12|libXau-1.0.12.tar.gz|$LIB_URL/libXau-1.0.12.tar.gz|2402dd938da4d0a332349ab3d3586606175e19cb32cb9fe013c19f1dc922dcee||libXau.so.6"
    "libXdmcp|1.1.5|libXdmcp-1.1.5.tar.gz|$LIB_URL/libXdmcp-1.1.5.tar.gz|31a7abc4f129dcf6f27ae912c3eedcb94d25ad2e8f317f69df6eda0bc4e4f2f3||libXdmcp.so.6"
    "xcb-proto|1.17.0|xcb-proto-1.17.0.tar.gz|$PROTO_URL/xcb-proto-1.17.0.tar.gz|392d3c9690f8c8202a68fdb89c16fd55159ab8d65000a6da213f4a1576e97a16|-|xcb-proto.pc"
    "libxcb|1.17.0|libxcb-1.17.0.tar.xz|$XCB_URL/libxcb-1.17.0.tar.xz|599ebf9996710fea71622e6e184f3a8ad5b43d0e5fa8c4e407123c88a59a6d55|--disable-xinput|libxcb.so.1"
    "libX11|1.8.12|libX11-1.8.12.tar.gz|$LIB_URL/libX11-1.8.12.tar.gz|220fbcf54b6e4d8dc40076ff4ab87954358019982490b33c7802190b62d89ce1||libX11.so.6"
    "libXext|1.3.6|libXext-1.3.6.tar.gz|$LIB_URL/libXext-1.3.6.tar.gz|1a0ac5cd792a55d5d465ced8dbf403ed016c8e6d14380c0ea3646c4415496e3d||libXext.so.6"
    "libXres|1.2.2|libXres-1.2.2.tar.gz|$LIB_URL/libXres-1.2.2.tar.gz|8abce597ced4a7ab89032aee91f6f784d9960adc772b2b59f17e515cd4127950||libXRes.so.1"
    "libXrender|0.9.12|libXrender-0.9.12.tar.gz|$LIB_URL/libXrender-0.9.12.tar.gz|0fff64125819c02d1102b6236f3d7d861a07b5216d8eea336c3811d31494ecf7||libXrender.so.1"
    "libXfixes|6.0.1|libXfixes-6.0.1.tar.gz|$LIB_URL/libXfixes-6.0.1.tar.gz|e69eaa321173c748ba6e2f15c7cf8da87f911d3ea1b6af4b547974aef6366bec||libXfixes.so.3"
    "libXdamage|1.1.6|libXdamage-1.1.6.tar.gz|$LIB_URL/libXdamage-1.1.6.tar.gz|2afcc139eb6eb926ffe344494b1fc023da25def42874496e6e6d3aa8acef8595||libXdamage.so.1"
    "libXcomposite|0.4.6|libXcomposite-0.4.6.tar.gz|$LIB_URL/libXcomposite-0.4.6.tar.gz|3599dfcd96cd48d45e6aeb08578aa27636fa903f480f880c863622c2b352d076||libXcomposite.so.1"
    "libXi|1.8|libXi-1.8.tar.gz|$LIB_URL/libXi-1.8.tar.gz|c80fd200a1190e4406bb4cc6958839d9651638cb47fa546a595d4bebcd3b9e2d||libXi.so.6"
    "libXrandr|1.5.4|libXrandr-1.5.4.tar.gz|$LIB_URL/libXrandr-1.5.4.tar.gz|c72c94dc3373512ceb67f578952c5d10915b38cc9ebb0fd176a49857b8048e22||libXrandr.so.2"
    "libXcursor|1.2.2|libXcursor-1.2.2.tar.gz|$LIB_URL/libXcursor-1.2.2.tar.gz|98c3a30a3f85274c167d1ac5419d681ce41f14e27bfa5fe3003c8172cd8af104||libXcursor.so.1"
    "libXtst|1.2.5|libXtst-1.2.5.tar.gz|$LIB_URL/libXtst-1.2.5.tar.gz|244ba6e1c5ffa44f1ba251affdfa984d55d99c94bb925a342657e5e7aaf6d39c||libXtst.so.6"
    "libXft|2.3.8|libXft-2.3.8.tar.gz|$LIB_URL/libXft-2.3.8.tar.gz|32e48fe2d844422e64809e4e99b9d8aed26c1b541a5acf837c5037b8d9f278a8||libXft.so.2"
    "libXinerama|1.1.5|libXinerama-1.1.5.tar.xz|$LIB_URL/libXinerama-1.1.5.tar.xz|5094d1f0fcc1828cb1696d0d39d9e866ae32520c54d01f618f1a3c1e30c2085c||libXinerama.so.1"
    "libICE|1.1.1|libICE-1.1.1.tar.xz|$LIB_URL/libICE-1.1.1.tar.xz|03e77afaf72942c7ac02ccebb19034e6e20f456dcf8dddadfeb572aa5ad3e451||libICE.so.6"
    "libSM|1.2.4|libSM-1.2.4.tar.xz|$LIB_URL/libSM-1.2.4.tar.xz|fdcbe51e4d1276b1183da77a8a4e74a137ca203e0bcfb20972dd5f3347e97b84||libSM.so.6"
)

selected_packages() {
    if [[ -z "${AKAOS_X11_PACKAGES:-}" ]]; then
        printf '%s\n' "${PACKAGES[@]}"
        return
    fi
    local wanted entry name wanted_name
    IFS=',' read -r -a wanted <<<"${AKAOS_X11_PACKAGES}"
    for entry in "${PACKAGES[@]}"; do
        name="${entry%%|*}"
        for wanted_name in "${wanted[@]}"; do
            if [[ "$name" == "$wanted_name" ]]; then
                printf '%s\n' "$entry"
                break
            fi
        done
    done
}

fetch_package() {
    local name="$1" tarball="$2" url="$3" expected_sha="$4"
    local path="$SRC_DIR/$tarball"
    if [[ ! -s "$path" ]]; then
        echo "[x11] fetching $name"
        curl -fL --retry 3 --max-time 600 -o "$path.part" "$url" ||
            { echo "[x11] download failed: $url" >&2; return 1; }
        mv "$path.part" "$path"
    fi
    if [[ "$expected_sha" != "-" && -n "$expected_sha" ]]; then
        local actual
        actual="$(sha256sum "$path" | cut -d' ' -f1)"
        if [[ "$actual" != "$expected_sha" ]]; then
            echo "[x11] checksum mismatch for $tarball" >&2
            echo "  expected $expected_sha" >&2
            echo "  actual   $actual" >&2
            return 1
        fi
    else
        echo "[x11] recording unpinned checksum for $tarball" >&2
        sha256sum "$path" >&2
    fi
}

extract_package() {
    local name="$1" version="$2" tarball="$3"
    local source_dir="$SRC_DIR/$name-$version"
    if [[ -f "$source_dir/.akaos-extracted" ]]; then
        return 0
    fi
    echo "[x11] extracting $name $version"
    rm -rf "$source_dir"
    mkdir -p "$source_dir"
    tar -xf "$SRC_DIR/$tarball" -C "$source_dir" --strip-components=1
    touch "$source_dir/.akaos-extracted"
}

build_package() {
    local entry="$1"
    local name version tarball url sha extra marker
    IFS='|' read -r name version tarball url sha extra marker <<<"$entry"

    fetch_package "$name" "$tarball" "$url" "$sha" || return 1
    extract_package "$name" "$version" "$tarball" || return 1

    local source_dir="$SRC_DIR/$name-$version"
    local build_dir="$BUILD_ROOT/$name-$version"

    if [[ -n "$(find "$STAGE" -name "$marker" -print -quit)" ]]; then
        echo "[x11] $name already staged"
        return 0
    fi

    echo "[x11] configuring $name $version"
    rm -rf "$build_dir"
    mkdir -p "$build_dir"

    local -a configure_args=(
        --host=x86_64-pc-linux-gnu
        --prefix="$STAGE_PREFIX"
        --disable-static
        --enable-shared
    )
    if [[ -n "$extra" && "$extra" != "-" ]]; then
        read -r -a extra_args <<<"$extra"
        configure_args+=("${extra_args[@]}")
    fi

    (
        cd "$build_dir" || exit 1
        env \
            CC="$CC_WRAPPER" \
            CFLAGS="-O2 -g0 -fno-strict-aliasing" \
            CPPFLAGS="-I$SYSROOT/usr/local/include -I$SYSROOT/usr/local/include/freetype2" \
            LDFLAGS="-nostdlib -fuse-ld=lld -Wl,--as-needed -L$STAGE/usr/local/lib -L$STAGE/usr/lib -L$SYSROOT/usr/local/lib -L$SYSROOT/usr/lib -L$BUILD_DIR $BUILD_DIR/libm.so.6 $BUILD_DIR/libc.so.6" \
            LIBS="" \
            PKG_CONFIG_LIBDIR="$STAGE/usr/local/lib/pkgconfig:$STAGE/usr/local/share/pkgconfig:$STAGE/usr/lib/pkgconfig:$STAGE/usr/share/pkgconfig:$SYSROOT/usr/local/lib/pkgconfig:$SYSROOT/usr/local/share/pkgconfig" \
            PKG_CONFIG_SYSROOT_DIR="" \
            xorg_cv_malloc0_returns_null=no \
            PYTHONPATH="$STAGE/usr/local/local/lib/python3.14/dist-packages:$STAGE/usr/local/lib/python3.14/dist-packages${PYTHONPATH:+:$PYTHONPATH}" \
            "$source_dir/configure" "${configure_args[@]}"
    ) >"$LOG_DIR/$name-configure.log" 2>&1 || {
        echo "[x11] configure failed for $name; see $LOG_DIR/$name-configure.log" >&2
        tail -40 "$LOG_DIR/$name-configure.log" >&2
        return 1
    }

    # libxcb enables the Linux abstract socket namespace for every host
    # whose operating system is named linux.  akaOS has no abstract socket
    # namespace, and libxcb only falls back to the filesystem socket when the
    # abstract attempt fails with ENOENT or ECONNREFUSED, so the generated
    # configuration is adjusted to use the filesystem socket directly.
    local config_header
    while IFS= read -r config_header; do
        if grep -q '^#define[[:space:]]\+HAVE_ABSTRACT_SOCKETS' "$config_header"; then
            echo "[x11] $name: disabling the abstract socket namespace, which akaOS does not implement" >&2
            sed -i 's|^#define[[:space:]]\+HAVE_ABSTRACT_SOCKETS.*|/* akaOS has no abstract socket namespace. */\n/* #undef HAVE_ABSTRACT_SOCKETS */|' \
                "$config_header"
        fi
    done < <(find "$build_dir" -name config.h -type f 2>/dev/null)

    echo "[x11] building $name"
    make -C "$build_dir" -j"$JOBS" >"$LOG_DIR/$name-build.log" 2>&1 || {
        echo "[x11] build failed for $name; see $LOG_DIR/$name-build.log" >&2
        tail -60 "$LOG_DIR/$name-build.log" >&2
        return 1
    }
    # The packages install directly into the stage prefix.  A DESTDIR copy
    # would keep the runtime prefix inside the recorded libtool paths, and
    # libtool then cannot open the dependency archives while linking.
    make -C "$build_dir" install >"$LOG_DIR/$name-install.log" 2>&1 || {
        echo "[x11] install failed for $name; see $LOG_DIR/$name-install.log" >&2
        tail -30 "$LOG_DIR/$name-install.log" >&2
        return 1
    }

    # The marker names a file that must exist somewhere in the staged tree.
    # X.Org packages disagree about whether pkg-config files belong under
    # lib/pkgconfig or share/pkgconfig, so match on the file name.
    if [[ -z "$(find "$STAGE" -name "$marker" -print -quit)" ]]; then
        echo "[x11] $name did not install expected marker $marker" >&2
        return 1
    fi
    echo "[x11] staged $name"
}

while IFS= read -r entry; do
    [[ -z "$entry" ]] && continue
    build_package "$entry" || {
        echo "[x11] FAILED at ${entry%%|*}" >&2
        exit 1
    }
done < <(selected_packages)

echo "[x11] publishing staged tree to $SYSROOT"
mkdir -p "$SYSROOT/usr/local"
# Merge into the graphics sysroot.  The X11 stack shares that tree with the
# already built GTK, Qt and Cairo libraries, so the copy must overlay rather
# than replace the shared include, lib and share subtrees.
for subtree in include lib share; do
    if [[ -d "$STAGE/usr/local/$subtree" ]]; then
        mkdir -p "$SYSROOT/usr/local/$subtree"
        cp -a "$STAGE/usr/local/$subtree/." "$SYSROOT/usr/local/$subtree/"
    fi
done
# The published pkg-config files must describe the runtime prefix.
while IFS= read -r descriptor; do
    sed -i "s|^prefix=$STAGE_PREFIX$|prefix=/usr/local|" "$descriptor"
done < <(find "$SYSROOT/usr/local" -name "*.pc" -type f)

# The X.Org packages split their package descriptions over the library and the
# share directory, so the ones that landed in the share directory are published
# beside the others.  Every consumer then finds the whole set in the one
# directory that the package search path always contains.
mkdir -p "$SYSROOT/usr/local/lib/pkgconfig"
for descriptor in "$SYSROOT/usr/local/share/pkgconfig"/*.pc; do
    [[ -e "$descriptor" ]] || continue
    cp -f "$descriptor" "$SYSROOT/usr/local/lib/pkgconfig/$(basename "$descriptor")"
done

echo "[x11] DONE"
