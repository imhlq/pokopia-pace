"""Build this Windows mod using the compiler bundled with Pokopia Recomp."""
from pathlib import Path
import argparse
import hashlib
import os
import shutil
import subprocess
import tomllib
import zipfile

HERE = Path(__file__).resolve().parent
GAME = HERE.parent.parent
ZIG = GAME / "tools/build/zig/zig.exe"
OUT = HERE / "build"
INCLUDE = HERE / "vendor/modtopia/sdk/include"


def run(*args):
    env = dict(os.environ)
    env["ZIG_GLOBAL_CACHE_DIR"] = str(OUT / "zig-global")
    env["ZIG_LOCAL_CACHE_DIR"] = str(OUT / "zig-local")
    subprocess.run([str(a) for a in args], cwd=HERE, env=env, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install", action="store_true")
    parser.add_argument("--zig", type=Path, default=ZIG, help="path to the Zig executable")
    args = parser.parse_args()
    zig = args.zig.resolve()
    if not zig.is_file():
        parser.error("Zig was not found. Pass --zig with the path to zig.exe.")
    OUT.mkdir(exist_ok=True)
    dll = OUT / "mod.dll"
    flags = ["-target", "x86_64-windows-gnu", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-I", INCLUDE]
    run(zig, "cc", *flags, "-shared", HERE / "mod.c", "-o", dll)
    run(zig, "cc", *flags, HERE / "test_host.c", "-o", OUT / "test_host.exe")
    run(OUT / "test_host.exe", dll)
    meta = tomllib.loads((HERE / "mod.toml").read_text(encoding="utf8"))
    package = OUT / f"{meta['id']}-{meta['version']}.pkmod"
    with zipfile.ZipFile(package, "w", compression=zipfile.ZIP_DEFLATED) as z:
        z.write(HERE / "mod.toml", "mod.toml")
        z.write(dll, "bin/windows-x64/mod.dll")
        for filename in ("README.md", "README.zh-CN.md", "LICENSE"):
            z.write(HERE / filename, filename)
        z.write(HERE / "vendor/modtopia/LICENSE", "licenses/Modtopia-LICENSE")
    digest = hashlib.sha256(package.read_bytes()).hexdigest()
    (OUT / "SHA256.txt").write_text(f"{digest}  {package.name}\n", encoding="utf8")
    print(f"Built {package}\nSHA256 {digest}")
    if args.install:
        dest = GAME / "mods" / package.name
        shutil.copy2(package, dest)
        print(f"Installed {dest}")


if __name__ == "__main__":
    main()
