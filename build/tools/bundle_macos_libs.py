#!/usr/bin/env python3
"""
Make the macOS client binaries self-contained.

`make macos` links against Homebrew, so the binaries reference absolute paths
like /opt/homebrew/opt/sdl3/lib/libSDL3.0.dylib, and libastonia_net.dylib even
carries the CI runner's build directory as its install name. None of those paths
exist on a player's Mac, so the client cannot start there - dyld fails before
main() runs. Several dependencies (libpng, libzip, mimalloc, freetype,
harfbuzz) were not shipped at all.

This walks the real dependency graph, copies every non-system library next to
the executable, and rewrites each reference to @rpath/<name>. moac already
carries an LC_RPATH of @loader_path, so @rpath resolves to the directory the
binaries live in.

Reading Mach-O load commands is done here rather than by shelling out to otool,
so the planning half can be exercised on any platform. Only the rewriting needs
macOS (install_name_tool).

    bundle_macos_libs.py <dir>              rewrite in place
    bundle_macos_libs.py <dir> --dry-run    report the plan and change nothing
"""

import argparse
import os
import shutil
import struct
import subprocess
import sys

LC_LOAD_DYLIB = 0xC
LC_LOAD_WEAK_DYLIB = 0x80000018
LC_ID_DYLIB = 0xD
LC_REEXPORT_DYLIB = 0x8000001F
LC_RPATH = 0x8000001C

DEPENDENCY_COMMANDS = (LC_LOAD_DYLIB, LC_LOAD_WEAK_DYLIB, LC_REEXPORT_DYLIB)
# Libraries that ship with macOS; every Mac has them and they must not be copied.
SYSTEM_PREFIXES = ("/usr/lib/", "/System/")

MAGIC_64 = 0xFEEDFACF
MAGIC_32 = 0xFEEDFACE
FAT_MAGICS = (0xCAFEBABE, 0xCAFEBABF)


def _thin_slices(data):
    """Yield each architecture slice of a (possibly fat) Mach-O."""
    if len(data) < 8:
        return []
    magic = struct.unpack(">I", data[:4])[0]
    if magic not in FAT_MAGICS:
        return [data]
    count = struct.unpack(">I", data[4:8])[0]
    out = []
    for i in range(count):
        entry = 8 + i * 20
        offset, size = struct.unpack(">II", data[entry + 8:entry + 16])
        out.append(data[offset:offset + size])
    return out


def read_macho(path):
    """Return (dependencies, install_id, rpaths) for a Mach-O, or None."""
    try:
        with open(path, "rb") as handle:
            data = handle.read()
    except OSError:
        return None
    deps, install_id, rpaths = [], None, []
    found = False
    for chunk in _thin_slices(data):
        if len(chunk) < 32:
            continue
        magic = struct.unpack("<I", chunk[:4])[0]
        if magic not in (MAGIC_64, MAGIC_32):
            continue
        found = True
        ncmds = struct.unpack("<I", chunk[16:20])[0]
        offset = 32 if magic == MAGIC_64 else 28
        for _ in range(ncmds):
            if offset + 8 > len(chunk):
                break
            cmd, size = struct.unpack("<II", chunk[offset:offset + 8])
            if size == 0:
                break
            if cmd in DEPENDENCY_COMMANDS or cmd in (LC_ID_DYLIB, LC_RPATH):
                str_offset = struct.unpack("<I", chunk[offset + 8:offset + 12])[0]
                value = chunk[offset + str_offset:offset + size].split(b"\0")[0]
                value = value.decode("utf-8", "replace")
                if cmd in DEPENDENCY_COMMANDS:
                    if value not in deps:
                        deps.append(value)
                elif cmd == LC_ID_DYLIB:
                    install_id = value
                elif value not in rpaths:
                    rpaths.append(value)
            offset += size
    return (deps, install_id, rpaths) if found else None


def is_system(ref):
    return ref.startswith(SYSTEM_PREFIXES)


def collect(target_dir):
    """Walk the dependency graph, copying non-system libraries into target_dir.

    Returns the list of Mach-O files in target_dir once everything is present.
    """
    pending = []
    for name in sorted(os.listdir(target_dir)):
        path = os.path.join(target_dir, name)
        if os.path.isfile(path) and read_macho(path):
            pending.append(path)

    if not pending:
        sys.exit(f"ERROR: no Mach-O binaries found in {target_dir}")

    known = {os.path.basename(p) for p in pending}
    copied = []
    while pending:
        current = pending.pop()
        info = read_macho(current)
        if not info:
            continue
        for ref in info[0]:
            if is_system(ref) or ref.startswith("@"):
                continue
            base = os.path.basename(ref)
            if base in known:
                continue
            if not os.path.exists(ref):
                # A stale absolute path that is not on this machine either. The
                # reference is still rewritten below; flag it loudly because the
                # library will be missing at runtime.
                print(f"  WARNING: {os.path.basename(current)} needs {ref}, which does not exist here")
                continue
            destination = os.path.join(target_dir, base)
            shutil.copy(os.path.realpath(ref), destination)
            os.chmod(destination, 0o755)
            known.add(base)
            copied.append(base)
            pending.append(destination)
            print(f"  bundled {base}  (from {ref})")

    if not copied:
        print("  no additional libraries needed")
    return [os.path.join(target_dir, n) for n in sorted(known)]


def plan_rewrites(machos):
    """Return {path: (new_id_or_None, [(old_ref, new_ref), ...])}."""
    plan = {}
    for path in machos:
        info = read_macho(path)
        if not info:
            continue
        deps, install_id, _ = info
        base = os.path.basename(path)
        new_id = None
        if install_id is not None and install_id != f"@rpath/{base}":
            new_id = f"@rpath/{base}"
        changes = []
        for ref in deps:
            if is_system(ref) or ref.startswith("@"):
                continue
            changes.append((ref, f"@rpath/{os.path.basename(ref)}"))
        if new_id or changes:
            plan[path] = (new_id, changes)
    return plan


def apply_rewrites(plan):
    for path, (new_id, changes) in sorted(plan.items()):
        os.chmod(path, 0o755)
        if new_id:
            subprocess.run(["install_name_tool", "-id", new_id, path], check=True)
        for old, new in changes:
            subprocess.run(["install_name_tool", "-change", old, new, path], check=True)
        info = read_macho(path)
        if info and "@loader_path" not in info[2]:
            # Harmless if it is already there under another spelling; a duplicate
            # rpath is not an error, a missing one is.
            subprocess.run(["install_name_tool", "-add_rpath", "@loader_path", path], check=False)
        print(f"  rewrote {os.path.basename(path)}"
              f"{' (id)' if new_id else ''}"
              f"{f' ({len(changes)} refs)' if changes else ''}")


def report(machos):
    print("\n=== resulting dependencies ===")
    unresolved = 0
    present = {os.path.basename(p) for p in machos}
    for path in sorted(machos):
        info = read_macho(path)
        if not info:
            continue
        external = [r for r in info[0] if not is_system(r)]
        print(f"  {os.path.basename(path)}")
        for ref in external:
            base = os.path.basename(ref)
            ok = ref.startswith("@rpath/") and base in present
            print(f"     {'ok     ' if ok else 'PROBLEM'} {ref}")
            if not ok:
                unresolved += 1
    return unresolved


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", help="directory holding moac and its libraries")
    parser.add_argument("--dry-run", action="store_true",
                        help="report the plan without copying or rewriting")
    args = parser.parse_args()

    target = os.path.abspath(args.directory)
    if not os.path.isdir(target):
        sys.exit(f"ERROR: {target} is not a directory")

    print(f"=== bundling macOS libraries in {target} ===")
    if not args.dry_run and shutil.which("install_name_tool") is None:
        sys.exit("ERROR: install_name_tool not found - rewriting install names needs "
                 "macOS. Use --dry-run to inspect the plan on another platform.")
    if args.dry_run:
        machos = [os.path.join(target, n) for n in sorted(os.listdir(target))
                  if os.path.isfile(os.path.join(target, n)) and read_macho(os.path.join(target, n))]
        for path, (new_id, changes) in sorted(plan_rewrites(machos).items()):
            print(f"  would rewrite {os.path.basename(path)}: "
                  f"id={new_id or 'unchanged'}, {len(changes)} reference(s)")
            for old, new in changes:
                print(f"      {old} -> {new}")
        report(machos)
        return 0

    machos = collect(target)
    apply_rewrites(plan_rewrites(machos))
    unresolved = report(machos)
    if unresolved:
        sys.exit(f"\nERROR: {unresolved} dependency reference(s) still unresolved; "
                 "the client would fail to start on a machine without Homebrew")
    print("\nAll dependencies resolve through @rpath and ship alongside the binaries.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
