#!/usr/bin/env python3
import sys

if len(sys.argv) < 3:
    sys.exit(1)

root_dir = sys.argv[1]
name = sys.argv[2]
build_sh = f"{root_dir}/build.sh"

with open(build_sh, 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

start_marker = name + "=("
p = text.find(start_marker)
if p != -1:
    end_p = text.find(")", p)
    block = text[p + len(start_marker):end_p]
    for line in block.splitlines():
        line = line.strip().strip('"').strip("'")
        if line and not line.startswith('#'):
            print(line.replace('$ROOT_DIR', root_dir))
