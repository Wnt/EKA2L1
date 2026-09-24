#!/usr/bin/env python3
"""mkcaseinc.py <sdk-include-dir> <out-dir> [extra source files/dirs ...]

Symbian SDK headers were written on a case-insensitive filesystem: they #include
each other as <E32STD.H>, <e32std.h>, "W32Std.h" ... while the files on disk have
one fixed case. This builds a symlink farm so every spelling that any header (or
our own sources) actually uses resolves on Linux: for each #include name that
does not exist verbatim under <sdk-include-dir> but matches a file there
case-insensitively, <out-dir>/<name> -> the real file. Put <out-dir> on the
include path AFTER the SDK include dir.  (Kernel Hive lab, 2026, MIT)
"""
import os, re, sys

inc, out = sys.argv[1], sys.argv[2]
extra = sys.argv[3:]
index = {}
for root, _dirs, files in os.walk(inc):
    for f in files:
        rel = os.path.relpath(os.path.join(root, f), inc)
        index.setdefault(rel.lower().replace('\\', '/'), rel)

pat = re.compile(rb'^[ \t]*#[ \t]*include[ \t]*[<"]([^>"]+)[>"]', re.M)
names = set()

def scan(path):
    try:
        data = open(path, 'rb').read()
    except OSError:
        return
    for m in pat.finditer(data):
        names.add(m.group(1).decode('latin-1').replace('\\', '/'))

for root, _dirs, files in os.walk(inc):
    for f in files:
        scan(os.path.join(root, f))
for e in extra:
    if os.path.isdir(e):
        for root, _dirs, files in os.walk(e):
            for f in files:
                scan(os.path.join(root, f))
    else:
        scan(e)

made = 0
missing = []
for n in sorted(names):
    if os.path.exists(os.path.join(inc, n)):
        continue
    rel = index.get(n.lower())
    if not rel:
        missing.append(n)
        continue
    dst = os.path.join(out, n)
    os.makedirs(os.path.dirname(dst) or out, exist_ok=True)
    if not os.path.lexists(dst):
        os.symlink(os.path.abspath(os.path.join(inc, rel)), dst)
        made += 1
print(f"caseinc: {len(names)} include names, {made} links made, {len(missing)} not in SDK")
