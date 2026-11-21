#!/usr/bin/env python3
from pathlib import Path
import sys


source_path = Path(sys.argv[1]) / "gdk" / "wayland" / "gdkdisplay-wayland.c"
source = source_path.read_text(encoding="utf-8")


def replace_once(source, marker, original, replacement):
    if marker in source:
        return source
    count = source.count(original)
    if count != 1:
        raise SystemExit(
            f"GTK 3 port patch expected one source block, found {count}: {marker}"
        )
    return source.replace(original, replacement, 1)


source = replace_once(
    source,
    "akaOS exposes memfd_create through libc",
    "#if !defined (__NR_memfd_create)\n  force_shm_open = TRUE;\n#endif",
    "/* akaOS exposes memfd_create through libc. */",
)
source = replace_once(
    source,
    'memfd_create ("gdk-wayland", MFD_CLOEXEC)',
    "#if defined (__NR_memfd_create)\n"
    "      if (!force_shm_open)\n"
    "        {\n"
    "          ret = syscall (__NR_memfd_create, \"gdk-wayland\", MFD_CLOEXEC);\n\n"
    "          /* fall back to shm_open until debian stops shipping 3.16 kernel\n"
    "           * See bug 766341\n"
    "           */\n"
    "          if (ret < 0 && errno == ENOSYS)\n"
    "            force_shm_open = TRUE;\n"
    "        }\n"
    "#endif",
    "      if (!force_shm_open)\n"
    "        {\n"
    "          ret = memfd_create (\"gdk-wayland\", MFD_CLOEXEC);\n\n"
    "          if (ret < 0 && errno == ENOSYS)\n"
    "            force_shm_open = TRUE;\n"
    "        }",
)

win_h_path = Path(sys.argv[1]) / "gdk" / "x11" / "gdkwindow-x11.h"
if win_h_path.exists():
    win_h = win_h_path.read_text(encoding="utf-8")
    if "#define HAVE_XSYNC 1" not in win_h:
        win_h = win_h.replace("#ifdef HAVE_XSYNC\n  XID update_counter;", "#ifndef HAVE_XSYNC\n#define HAVE_XSYNC 1\n#endif\n#include <X11/Xlib.h>\n#include <X11/extensions/sync.h>\n#ifdef HAVE_XSYNC\n  XID update_counter;")
        win_h_path.write_text(win_h, encoding="utf-8")

xi2_path = Path(sys.argv[1]) / "gdk" / "x11" / "gdkdevice-xi2.c"
if xi2_path.exists():
    xi2 = xi2_path.read_text(encoding="utf-8")
    if "_gdk_x11_device_xi2_translate_event_mask (" not in xi2[:2000]:
        xi2 = xi2.replace("static void gdk_x11_device_xi2_finalize     (GObject      *object);", "unsigned char * _gdk_x11_device_xi2_translate_event_mask (GdkX11DeviceManagerXI2 *device_manager_xi2, GdkEventMask event_mask, gint *len);\nGdkModifierType _gdk_x11_device_xi2_translate_state (XIModifierState *mods_state, XIButtonState *button_state, XIGroupState *group_state);\nstatic void gdk_x11_device_xi2_finalize     (GObject      *object);")
        xi2_path.write_text(xi2, encoding="utf-8")

xi2_h_path = Path(sys.argv[1]) / "gdk" / "x11" / "gdkx11device-xi2.h"
if xi2_h_path.exists():
    xi2_h = xi2_h_path.read_text(encoding="utf-8")
    if "_gdk_x11_device_xi2_translate_event_mask" not in xi2_h:
        xi2_h = xi2_h.replace("GType gdk_x11_device_xi2_get_type (void) G_GNUC_CONST;", "GType gdk_x11_device_xi2_get_type (void) G_GNUC_CONST;\n#include <X11/extensions/XInput2.h>\ntypedef struct _GdkX11DeviceManagerXI2 GdkX11DeviceManagerXI2;\nunsigned char * _gdk_x11_device_xi2_translate_event_mask (GdkX11DeviceManagerXI2 *device_manager_xi2, GdkEventMask event_mask, gint *len);\nGdkModifierType _gdk_x11_device_xi2_translate_state (XIModifierState *mods_state, XIButtonState *button_state, XIGroupState *group_state);\nvoid _gdk_device_xi2_reset_scroll_valuators (GdkX11DeviceXI2 *device);\nvoid _gdk_device_xi2_unset_scroll_valuators (GdkX11DeviceXI2 *device);\ngboolean _gdk_x11_device_xi2_get_scroll_delta (GdkX11DeviceXI2 *device, guint n_valuator, gdouble valuator_value, GdkScrollDirection *direction_ret, gdouble *delta_ret);\ngdouble gdk_x11_device_xi2_get_last_axis_value (GdkX11DeviceXI2 *device, gint n_axis);\nvoid gdk_x11_device_xi2_store_axes (GdkX11DeviceXI2 *device, gdouble *axes, gint n_axes);")
        xi2_h_path.write_text(xi2_h, encoding="utf-8")

disp_x11_path = Path(sys.argv[1]) / "gdk" / "x11" / "gdkdisplay-x11.c"
if disp_x11_path.exists():
    disp_x11 = disp_x11_path.read_text(encoding="utf-8")
    if "#include <X11/extensions/sync.h>" not in disp_x11:
        disp_x11 = disp_x11.replace("#ifdef HAVE_XFIXES\n#include <X11/extensions/Xfixes.h>\n#endif", "#ifdef HAVE_XFIXES\n#include <X11/extensions/Xfixes.h>\n#endif\n\n#ifdef HAVE_XSYNC\n#include <X11/extensions/sync.h>\n#endif")
        disp_x11_path.write_text(disp_x11, encoding="utf-8")

if "gtk3-wayland-shm-open result=" not in source:
    replacements = (
        (
            "#include <stdlib.h>\n",
            "#include <stdlib.h>\n#include <stdio.h>\n",
        ),
        (
            "  while (ret < 0 && errno == EINTR);\n\n"
            "  if (ret < 0)\n",
            "  while (ret < 0 && errno == EINTR);\n\n"
            "  if (getenv(\"AKAOS_XCURSOR_TRACE\"))\n"
            "    fprintf (stderr, \"gtk3-wayland-shm-open result=%d errno=%d fallback=%d\\n\", ret, errno, force_shm_open);\n\n"
            "  if (ret < 0)\n",
        ),
        (
            "  if (ftruncate (fd, size) < 0)\n"
            "    {\n"
            "      g_critical (G_STRLOC \": Truncating shared memory file failed: %m\");\n",
            "  if (ftruncate (fd, size) < 0)\n"
            "    {\n"
            "      if (getenv(\"AKAOS_XCURSOR_TRACE\"))\n"
            "        fprintf (stderr, \"gtk3-wayland-shm-ftruncate-failed size=%d errno=%d\\n\", size, errno);\n"
            "      g_critical (G_STRLOC \": Truncating shared memory file failed: %m\");\n",
        ),
        (
            "  if (data == MAP_FAILED)\n"
            "    {\n"
            "      g_critical (G_STRLOC \": mmap'ping shared memory file failed: %m\");\n",
            "  if (data == MAP_FAILED)\n"
            "    {\n"
            "      if (getenv(\"AKAOS_XCURSOR_TRACE\"))\n"
            "        fprintf (stderr, \"gtk3-wayland-shm-mmap-failed size=%d errno=%d\\n\", size, errno);\n"
            "      g_critical (G_STRLOC \": mmap'ping shared memory file failed: %m\");\n",
        ),
        (
            "  pool = wl_shm_create_pool (shm, fd, size);\n",
            "  pool = wl_shm_create_pool (shm, fd, size);\n"
            "  if (getenv(\"AKAOS_XCURSOR_TRACE\"))\n"
            "    fprintf (stderr, \"gtk3-wayland-shm-pool shm=%p fd=%d size=%d pool=%p errno=%d\\n\", shm, fd, size, pool, errno);\n",
        ),
        (
            "                                &data->buf_length,\n"
            "                                &data->buf);\n\n"
            "  surface = cairo_image_surface_create_for_data",
            "                                &data->buf_length,\n"
            "                                &data->buf);\n"
            "  if (getenv(\"AKAOS_XCURSOR_TRACE\"))\n"
            "    fprintf (stderr, \"gtk3-wayland-shm-surface pool=%p buf=%p length=%zu\\n\", data->pool, data->buf, data->buf_length);\n\n"
            "  surface = cairo_image_surface_create_for_data",
        ),
        (
            "                                            stride, WL_SHM_FORMAT_ARGB8888);\n\n"
            "  cairo_surface_set_user_data",
            "                                            stride, WL_SHM_FORMAT_ARGB8888);\n"
            "  if (getenv(\"AKAOS_XCURSOR_TRACE\"))\n"
            "    fprintf (stderr, \"gtk3-wayland-shm-buffer pool=%p buffer=%p\\n\", data->pool, data->buffer);\n\n"
            "  cairo_surface_set_user_data",
        ),
    )

    for original, replacement in replacements:
        count = source.count(original)
        if count != 1:
            raise SystemExit(
                f"GTK 3 SHM diagnostic patch expected one source block, found {count}"
            )
        source = source.replace(original, replacement, 1)

source_path.write_text(source, encoding="utf-8")
