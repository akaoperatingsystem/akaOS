#!/usr/bin/env python3
from pathlib import Path
import sys


source = Path(sys.argv[1]) / "tools" / "dbus-update-activation-environment.c"
text = source.read_text()
broken = (
    "dbus_set_error_from_message (&error, msg) ||\n"
    "      !dbus_message_get_args (msg, &error, DBUS_TYPE_INVALID)"
)
fixed = (
    "dbus_set_error_from_message (&error, reply) ||\n"
    "      !dbus_message_get_args (reply, &error, DBUS_TYPE_INVALID)"
)

if text.count(fixed) == 1:
    print("[SKIP] D-Bus activation-environment reply check already uses the reply")
elif text.count(broken) == 1:
    source.write_text(text.replace(broken, fixed, 1))
    if source.read_text().count(fixed) != 1:
        raise SystemExit("failed to patch dbus-update-activation-environment")
    print("[PATCH] Check the D-Bus method reply, not the outgoing call")
else:
    raise SystemExit("unexpected dbus-update-activation-environment source")
