#!/usr/bin/env python3
from pathlib import Path
import sys


source_root = Path(sys.argv[1])

platform_path = (
    source_root / "src" / "3rdparty" / "masm" / "wtf" / "Platform.h"
)
platform_source = platform_path.read_text(encoding="utf-8")
akaos_unix = (
    "    || OS(VXWORKS)          \\\n"
    "    || defined(Q_OS_AKAOS)  \\\n"
    "    || defined(unix)        \\\n"
)
original_unix = (
    "    || OS(VXWORKS)          \\\n"
    "    || defined(unix)        \\\n"
)
if akaos_unix not in platform_source:
    if platform_source.count(original_unix) != 1:
        raise SystemExit("Qt Declarative Unix-platform patch target changed")
    platform_source = platform_source.replace(original_unix, akaos_unix, 1)
    platform_path.write_text(platform_source, encoding="utf-8")

akaos_randomness = (
    "#if defined(Q_OS_AKAOS) && !defined(WTF_USE_OS_RANDOMNESS)\n"
    "#define WTF_USE_OS_RANDOMNESS 1\n"
    "#endif\n\n"
    "/* ENABLE macro defaults */"
)
if akaos_randomness not in platform_source:
    anchor = "/* ENABLE macro defaults */"
    if platform_source.count(anchor) != 1:
        raise SystemExit("Qt Declarative OS-randomness patch target changed")
    platform_source = platform_source.replace(anchor, akaos_randomness, 1)
    platform_path.write_text(platform_source, encoding="utf-8")

assembler_path = source_root / "src" / "qml" / "jit" / "qv4assemblercommon_p.h"
assembler_source = assembler_path.read_text(encoding="utf-8")
akaos_sysv_abi = "defined(Q_OS_HURD) || defined(Q_OS_AKAOS)"
original_sysv_abi = "defined(Q_OS_HURD)"
if akaos_sysv_abi not in assembler_source:
    if assembler_source.count(original_sysv_abi) != 1:
        raise SystemExit("Qt Declarative x86-64 SysV assembler patch target changed")
    assembler_source = assembler_source.replace(
        original_sysv_abi, akaos_sysv_abi, 1
    )
    assembler_path.write_text(assembler_source, encoding="utf-8")

stubs_path = (
    source_root / "src" / "3rdparty" / "masm" / "stubs" / "WTFStubs.cpp"
)
stubs_source = stubs_path.read_text(encoding="utf-8")
akaos_random_headers = (
    "#if defined(__AKAOS__)\n"
    "#include <cerrno>\n"
    "#include <fcntl.h>\n"
    "#include <unistd.h>\n"
    "#endif"
)
if akaos_random_headers not in stubs_source:
    anchor = "#include <stdlib.h>\n#include <stdio.h>\n#include <stdarg.h>"
    if stubs_source.count(anchor) != 1:
        raise SystemExit("Qt Declarative randomness include patch target changed")
    stubs_source = stubs_source.replace(
        anchor, anchor + "\n" + akaos_random_headers, 1
    )

akaos_random_helper = (
    "#if defined(__AKAOS__)\n"
    "static bool fillCryptographicRandomBytes(void* buffer, size_t length)\n"
    "{\n"
    "    int descriptor = open(\"/dev/urandom\", O_RDONLY);\n"
    "    if (descriptor < 0)\n"
    "        return false;\n"
    "    auto* bytes = static_cast<unsigned char*>(buffer);\n"
    "    size_t offset = 0;\n"
    "    while (offset < length) {\n"
    "        ssize_t amount = read(descriptor, bytes + offset, length - offset);\n"
    "        if (amount < 0 && errno == EINTR)\n"
    "            continue;\n"
    "        if (amount <= 0) {\n"
    "            close(descriptor);\n"
    "            return false;\n"
    "        }\n"
    "        offset += static_cast<size_t>(amount);\n"
    "    }\n"
    "    close(descriptor);\n"
    "    return true;\n"
    "}\n"
    "#endif\n\n"
    "uint32_t cryptographicallyRandomNumber()\n"
    "{\n"
    "#if defined(__AKAOS__)\n"
    "    uint32_t value = 0;\n"
    "    if (!fillCryptographicRandomBytes(&value, sizeof(value)))\n"
    "        abort();\n"
    "    return value;\n"
    "#else\n"
    "    return 0;\n"
    "#endif\n"
    "}\n\n"
    "#if defined(__AKAOS__)\n"
    "void cryptographicallyRandomValues(void* buffer, size_t length)\n"
    "{\n"
    "    if ((!buffer && length) ||\n"
    "        !fillCryptographicRandomBytes(buffer, length))\n"
    "        abort();\n"
    "}\n"
    "#endif"
)
original_random_stub = (
    "uint32_t cryptographicallyRandomNumber()\n"
    "{\n"
    "    return 0;\n"
    "}"
)
if akaos_random_helper not in stubs_source:
    if stubs_source.count(original_random_stub) != 1:
        raise SystemExit("Qt Declarative randomness implementation patch target changed")
    stubs_source = stubs_source.replace(
        original_random_stub, akaos_random_helper, 1
    )
    stubs_path.write_text(stubs_source, encoding="utf-8")
