#!/usr/bin/env python3
"""Build or verify an SD-card update with the NRO and its compatible pinned Windows components."""
import argparse
import filecmp
import hashlib
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile
from zipfile import ZIP_DEFLATED, ZipFile

tools = Path(__file__).resolve().parent
wine = tools.parent
repo = wine.parent / "horizon-dlls"
sd_root = Path("switch/wine")


def build_helper(build, tmp):
    libs = ["-lzstd", "-lz", "-pthread"]
    if platform.system() == "Darwin":
        prefix = subprocess.check_output(["brew", "--prefix", "zstd"], text=True).strip()
        libs += [f"-I{prefix}/include", f"-L{prefix}/lib"]
    else:
        libs.append("-lcrypto")
    helper = tmp / "offline-dlls"
    subprocess.run(["cc", "-std=gnu11", "-O2", "-Wno-deprecated-declarations", "-I", build,
                    "-I", wine / "source", tools / "offline-dlls.c", wine / "source/horizon_dlls.c",
                    *libs, "-o", helper], check=True)
    return helper


def stage_update(build, tmp, helper):
    revision = subprocess.check_output([helper, "revision"], text=True).strip()
    nro = build / "wine-nx-runtime.nro"
    data = nro.read_bytes()
    if data[16:20] != b"NRO0" or revision.encode() not in data:
        raise SystemExit(f"{nro}: expected a Switch NRO built with DLL revision {revision}")
    print(f"DLL revision {revision}", flush=True)
    # Export the runtime pin, which can differ from the submodule working tree.
    with subprocess.Popen(["git", "-C", repo, "archive", revision, "switch", "LICENSES", "NOTICE.md"],
                          stdout=subprocess.PIPE) as archive:
        subprocess.run(["tar", "-x", "-C", tmp], stdin=archive.stdout, check=True)
        if archive.wait():
            raise SystemExit(f"Cannot export {revision} from {repo}")

    source = tmp / sd_root
    stage = tmp / "stage"
    card = stage / sd_root
    selected = subprocess.check_output([helper, "list", source], text=True).splitlines()
    for rel in selected:
        target = card / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        os.link(source / rel, target)
    records = card / "horizon-dlls"
    records.mkdir(parents=True)
    subprocess.run([helper, "record", source, card], check=True)
    shutil.copyfile(nro, card / nro.name)
    shutil.copyfile(tmp / "NOTICE.md", records / "NOTICE.md")
    shutil.copytree(tmp / "LICENSES", records / "LICENSES")
    return stage


def verify(archive, stage, tmp, helper):
    files = {p.relative_to(stage).as_posix(): p for p in stage.rglob("*") if p.is_file()}
    readback = tmp / "readback"
    with ZipFile(archive) as z:
        names = z.namelist()
        if len(names) != len(files) or set(names) != files.keys():
            raise SystemExit("ZIP file list differs from the expected SD-card update")
        for entry in z.infolist():
            if entry.file_size != files[entry.filename].stat().st_size:
                raise SystemExit(f"Unexpected ZIP member size: {entry.filename}")
        # Extracting every expected file also checks each member's CRC.
        z.extractall(readback)
    for name, source in files.items():
        if not filecmp.cmp(source, readback / name, shallow=False):
            raise SystemExit(f"ZIP contents differ: {name}")
    subprocess.run([helper, "verify", tmp / sd_root, readback / sd_root], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=wine / "build-switch-amd64",
                        help="CMake build directory containing the NRO and horizon_dll_features.h")
    parser.add_argument("--output", type=Path, required=True, help="offline update ZIP")
    parser.add_argument("--verify-only", action="store_true", help="check an existing ZIP against the build and pin")
    args = parser.parse_args()
    output = args.output.resolve()
    if output.suffix.lower() != ".zip":
        parser.error("--output must name a .zip file")
    if args.verify_only and not output.is_file():
        parser.error(f"archive does not exist: {output}")
    with tempfile.TemporaryDirectory(prefix="corkdeck-dlls-") as work:
        tmp = Path(work)
        helper = build_helper(args.build.resolve(), tmp)
        stage = stage_update(args.build.resolve(), tmp, helper)
        if args.verify_only:
            verify(output, stage, tmp, helper)
        else:
            output.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.TemporaryDirectory(dir=output.parent) as pending:
                archive = Path(pending) / output.name
                with ZipFile(archive, "w", ZIP_DEFLATED, compresslevel=6) as z:
                    for path in sorted(stage.rglob("*")):
                        if path.is_file():
                            z.write(path, path.relative_to(stage))
                verify(archive, stage, tmp, helper)
                os.replace(archive, output)
    with output.open("rb") as f:
        digest = hashlib.sha256()
        for block in iter(lambda: f.read(1024 * 1024), b""):
            digest.update(block)
    print(f"{digest.hexdigest()}  {output}")


if __name__ == "__main__":
    main()
