"""Package the on-demand Switch renderer test with the official Windows Electron distribution."""
import argparse
import hashlib
import pathlib
import struct
import zipfile

HERE = pathlib.Path(__file__).resolve().parent
ELECTRON_SHA = "e5f0bf09aea0d1def6839dc08e7a56bac5679bb1001ab3cb3088922391dba53c"
PROGRAM = 0x0532434400524E44
BASE = "switch/wine/drive_c/ElectronProbe-10.1.5/"


def check_system_module(section, table_field, label):
    offset, size = struct.unpack_from("<II", section, table_field)
    if offset < table_field + 8 or offset + size > len(section) or size % 4:
        raise ValueError(f"Invalid {label} kernel capability table")
    # Program type descriptors have 13 trailing one bits and a zero delimiter.
    program_types = [word >> 14 for (word,) in struct.iter_unpack("<I", section[offset:offset + size])
                     if word & 0x3fff == 0x1fff]
    if program_types != [0]:
        raise ValueError(f"{label} application_type must be 0 (SystemModule), got {program_types}")


def check_exefs(data):
    magic, count, string_size, _ = struct.unpack_from("<4sIII", data)
    if magic != b"PFS0" or count != 2:
        raise ValueError("Expected an ExeFS with main and main.npdm")
    strings = 16 + count * 24
    payload = strings + string_size
    files = {}
    for i in range(count):
        offset, size, name, _ = struct.unpack_from("<QQII", data, 16 + 24 * i)
        key = data[strings + name:payload].split(b"\0", 1)[0]
        content = data[payload + offset:payload + offset + size]
        if len(content) != size:
            raise ValueError("Truncated ExeFS")
        files[key] = content
    meta = files[b"main.npdm"]
    aci, aci_size, acid, acid_size = struct.unpack_from("<IIII", meta, 0x70)
    if (files[b"main"][:4] != b"NSO0" or meta[:4] != b"META" or meta[12] & 15 != 7
            or meta[aci:aci + 4] != b"ACI0" or meta[acid + 0x200:acid + 0x204] != b"ACID"
            or struct.unpack_from("<Q", meta, aci + 0x10)[0] != PROGRAM
            or struct.unpack_from("<QQ", meta, acid + 0x210) != (PROGRAM, PROGRAM)):
        raise ValueError("Unexpected companion executable, address space, or program ID")
    check_system_module(meta[aci:aci + aci_size], 0x30, "ACI0")
    check_system_module(meta[acid:acid + acid_size], 0x230, "ACID")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=pathlib.Path)
    parser.add_argument("electron_zip", type=pathlib.Path)
    args = parser.parse_args()
    if hashlib.sha256(args.electron_zip.read_bytes()).hexdigest() != ELECTRON_SHA:
        raise ValueError("Electron 10.1.5 Windows x64 release checksum mismatch")
    nro = (args.build / "wine-nx-runtime.nro").read_bytes()
    if nro[16:20] != b"NRO0":
        raise ValueError("Invalid Switch NRO")
    exefs = (args.build / "ElectronProbe-exefs.nsp").read_bytes()
    check_exefs(exefs)
    out = args.build / "CorkDeck-ElectronProbe-SD.zip"
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=5) as dest:
        dest.writestr("switch/wine/wine-nx-runtime.nro", nro)
        dest.writestr(f"atmosphere/contents/{PROGRAM:016x}/exefs.nsp", exefs)
        with zipfile.ZipFile(args.electron_zip) as source:
            for item in source.infolist():
                if item.is_dir() or item.filename == "resources/default_app.asar":
                    continue
                name = "ElectronProbe.exe" if item.filename == "electron.exe" else item.filename
                dest.writestr(BASE + name, source.read(item))
        for name in ("package.json", "index.html", "main.js", "preload.js"):
            dest.write(HERE / name, BASE + "resources/app/" + name)
    with zipfile.ZipFile(out) as archive:
        if archive.testzip():
            raise ValueError("ZIP CRC mismatch")
        check_exefs(archive.read(f"atmosphere/contents/{PROGRAM:016x}/exefs.nsp"))
        if archive.read(BASE + "version").strip() != b"10.1.5":
            raise ValueError("Unexpected Electron version")
    print(f"{hashlib.sha256(out.read_bytes()).hexdigest()}  {out}")


if __name__ == "__main__":
    main()
