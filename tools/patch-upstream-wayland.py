#!/usr/bin/env python3
from pathlib import Path
import sys


def insert_guard(source, signature, marker, guard):
    if marker in source:
        return source

    function_start = source.find(signature)
    if function_start < 0:
        raise SystemExit(f"Wayland patch target not found: {signature}")

    brace = source.find("\n{", function_start)
    if brace < 0:
        raise SystemExit(f"Wayland patch target has no function body: {signature}")

    insert_at = brace + 2
    return source[:insert_at] + "\n" + guard + source[insert_at:]


source_path = Path(sys.argv[1]) / "src" / "wayland-client.c"
source = source_path.read_text(encoding="utf-8")
source = insert_guard(
    source,
    "wl_proxy_marshal_flags(struct wl_proxy *proxy",
    "wl-proxy-marshal-null",
    "\tif (!proxy) {\n"
    "\t\tconst char *trace = getenv(\"AKAOS_XCURSOR_TRACE\");\n"
    "\t\tif (trace && trace[0] && trace[0] != '0') {\n"
    "\t\t\tfprintf(stderr,\n"
    "\t\t\t\t\"wl-proxy-marshal-null opcode=%u interface=%s caller=%p\\n\",\n"
    "\t\t\t\topcode, interface ? interface->name : \"(null)\",\n"
    "\t\t\t\t__builtin_return_address(0));\n"
    "\t\t\tfflush(stderr);\n"
    "\t\t}\n"
    "\t\terrno = EINVAL;\n"
    "\t\treturn NULL;\n"
    "\t}\n",
)
source = insert_guard(
    source,
    "wl_proxy_get_version(struct wl_proxy *proxy)",
    "wl-proxy-get-version-null",
    "\tif (!proxy) {\n"
    "\t\tconst char *trace = getenv(\"AKAOS_XCURSOR_TRACE\");\n"
    "\t\tif (trace && trace[0] && trace[0] != '0')\n"
    "\t\t\tfprintf(stderr, \"wl-proxy-get-version-null caller=%p\\n\",\n"
    "\t\t\t\t__builtin_return_address(0));\n"
    "\t\terrno = EINVAL;\n"
    "\t\treturn 0;\n"
    "\t}\n",
)
source = insert_guard(
    source,
    "wl_proxy_add_listener(struct wl_proxy *proxy,",
    "wl-proxy-add-listener-null",
    "\tif (!proxy) {\n"
    "\t\tconst char *trace = getenv(\"AKAOS_XCURSOR_TRACE\");\n"
    "\t\tif (trace && trace[0] && trace[0] != '0')\n"
    "\t\t\tfprintf(stderr, \"wl-proxy-add-listener-null caller=%p\\n\",\n"
    "\t\t\t\t__builtin_return_address(0));\n"
    "\t\terrno = EINVAL;\n"
    "\t\treturn -1;\n"
    "\t}\n",
)
source_path.write_text(source, encoding="utf-8")
