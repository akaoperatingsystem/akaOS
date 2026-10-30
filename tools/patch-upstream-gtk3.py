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
