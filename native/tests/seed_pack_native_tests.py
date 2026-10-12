#!/usr/bin/env python3
"""The engine's native seed-pack expansion (native/app/seed_pack.hpp) against seedpack.py.

Usage: seed_pack_native_tests.py TOOL   (TOOL: the petari_seed_pack_tool binary)
"""
import glob
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "native", "tools"))
import seedpack  # noqa: E402

failures = 0


def expect(ok, what):
    global failures
    if not ok:
        print("FAIL:", what)
        failures += 1


def run(*args):
    return subprocess.run([sys.argv[1], *args], capture_output=True, text=True)


def main():
    pack = seedpack.newest_pack(os.path.join(ROOT, "native", "data"))
    expect(pack is not None, "a tracked pack exists")
    with tempfile.TemporaryDirectory() as temp:
        native, python = os.path.join(temp, "native"), os.path.join(temp, "python")
        expect(run("expand", pack, native).returncode == 0, "native expansion succeeds")
        seedpack.expand(pack, python)
        expect(seedpack.verify(python, native) == 0, "every database has seedpack.py's rows")
        expect(len(glob.glob(os.path.join(native, "*.db"))) == len(glob.glob(os.path.join(python, "*.db"))),
               "the same databases")
        stamp = os.path.getmtime(os.path.join(native, "__global__.db"))
        expect(run("ensure", pack, native).returncode == 0, "ensure on an up-to-date folder succeeds")
        expect(os.path.getmtime(os.path.join(native, "__global__.db")) == stamp, "an up-to-date folder is not rewritten")
        os.remove(os.path.join(native, "__global__.db"))
        expect(run("ensure", pack, native).returncode == 0 and os.path.exists(os.path.join(native, "__global__.db")),
               "a cleared folder is expanded again")
        damaged = os.path.join(temp, "damaged.xz")
        with open(pack, "rb") as source, open(damaged, "wb") as out:
            out.write(source.read()[:-40])
        target = os.path.join(temp, "from-damaged")
        expect(run("expand", damaged, target).returncode == 1, "a damaged pack is refused")
        expect(not os.path.exists(target) and not os.path.exists(target + ".partial"), "nothing is left behind")
    if failures:
        sys.exit(1)
    print("native seed pack expansion matches seedpack.py")


main()
