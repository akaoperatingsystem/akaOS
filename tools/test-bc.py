#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Check bc function accumulation boundaries using a supplied host binary."""

import pathlib
import subprocess
import sys
import tempfile


def definition(size):
    body = "return(7)\n"
    while len(body) < size - 2:
        count = min(700, size - 2 - len(body))
        body += "0" * (count - 1) + "\n"
    return "define f(x) {\n" + body + "}\n"


def main():
    binary = str(pathlib.Path(sys.argv[1]).resolve())
    cases = [
        ("ordinary", "2+3\ndefine f(x) {\nreturn(x*2)\n}\nf(6)\n", "5\n12\n", ""),
        ("below limit", definition(4094) + "f(0)\n", "7\n", ""),
        ("at limit", definition(4095) + "f(0)\n", "7\n", ""),
        ("one over", definition(4096) + "99\n", "", "bc: function definition too long\n"),
        ("large", definition(20000) + "99\n", "", "bc: function definition too long\n"),
        ("reset", definition(4095) + definition(4095) + "f(0)\n", "7\n", ""),
    ]
    with tempfile.TemporaryDirectory() as directory:
        for name, source, stdout, stderr in cases:
            for mode in ("stdin", "file"):
                args = [binary, "-q"]
                if mode == "file":
                    path = pathlib.Path(directory) / "input.bc"
                    path.write_text(source)
                    args.append(str(path))
                result = subprocess.run(args, input=source if mode == "stdin" else "",
                                        text=True, capture_output=True, timeout=10)
                assert (result.returncode, result.stdout, result.stderr) == (0, stdout, stderr), (
                    name, mode, result.returncode, result.stdout, result.stderr)
                print(f"PASS: {name} ({mode})")


if __name__ == "__main__":
    main()
