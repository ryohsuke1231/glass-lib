#!/usr/bin/env python3
# glsl-include.py — expands `#include "file"` in GLSL sources.
#
#   glsl-include.py INPUT OUTPUT [--line TEXT]... [--depfile FILE]
#
# Includes are resolved relative to the including file and expanded
# recursively; a file included twice is only emitted once. Each `--line`
# puts one line in front, e.g. a #version line for validation (one argument
# per line: meson rewrites backslashes in command arguments).
# `--depfile` writes a Makefile-style dependency file for meson/ninja.
#
# SPDX-License-Identifier: MIT

import argparse
import os
import re
import sys

INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"\s*$')


def expand(path, seen, deps, out):
    path = os.path.normpath(path)
    if path in seen:
        return
    seen.add(path)
    deps.append(path)
    with open(path, encoding='utf-8') as f:
        for number, line in enumerate(f, 1):
            m = INCLUDE.match(line)
            if m:
                target = os.path.join(os.path.dirname(path), m.group(1))
                if not os.path.exists(target):
                    sys.exit(f'{path}:{number}: cannot find "{m.group(1)}"')
                out.append(f'// ---- begin {os.path.basename(target)} ----\n')
                expand(target, seen, deps, out)
                out.append(f'// ---- end {os.path.basename(target)} ----\n')
            else:
                out.append(line if line.endswith('\n') else line + '\n')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('input')
    parser.add_argument('output')
    parser.add_argument('--line', action='append', default=[])
    parser.add_argument('--depfile')
    args = parser.parse_args()

    out, deps = [], []
    expand(args.input, set(), deps, out)

    with open(args.output, 'w', encoding='utf-8') as f:
        for line in args.line:
            f.write(line + '\n')
        f.writelines(out)

    if args.depfile:
        with open(args.depfile, 'w', encoding='utf-8') as f:
            f.write(f'{args.output}: ' + ' '.join(deps) + '\n')


if __name__ == '__main__':
    main()
