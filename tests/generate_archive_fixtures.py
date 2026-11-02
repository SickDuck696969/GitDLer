"""Generate small extraction fixtures without downloading third-party archives."""
import bz2
import gzip
import io
import lzma
import pathlib
import os
import shutil
import subprocess
import sys
import tarfile
import zipfile

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "build/archive-fixtures")
root.mkdir(parents=True, exist_ok=True)
for method, name in [(zipfile.ZIP_STORED, "stored.zip"), (zipfile.ZIP_DEFLATED, "deflated.zip")]:
    with zipfile.ZipFile(root / name, "w", compression=method) as archive:
        archive.writestr("folder/hello.txt", "hello archive")
        archive.writestr("empty.txt", "")
for mode, name in [("w", "test.tar"), ("w:gz", "test.tar.gz"), ("w:bz2", "test.tar.bz2"), ("w:xz", "test.tar.xz")]:
    with tarfile.open(root / name, mode) as archive:
        entry = tarfile.TarInfo("./folder/")
        entry.type = tarfile.DIRTYPE
        archive.addfile(entry)
        entry = tarfile.TarInfo("./folder/hello.txt")
        entry.size = 13
        archive.addfile(entry, io.BytesIO(b"hello archive"))
for extension, compress in [("gz", gzip.compress), ("bz2", bz2.compress), ("xz", lzma.compress)]:
    (root / ("single.txt." + extension)).write_bytes(compress(b"hello archive"))
for name, member in [("traversal.zip", "../escape.txt"), ("absolute.zip", "/escape.txt"), ("drive.zip", "C:/escape.txt"), ("backslash.zip", "..\\escape.txt")]:
    with zipfile.ZipFile(root / name, "w") as archive:
        archive.writestr("valid.txt", "must not be written")
        archive.writestr(member, "bad")
with zipfile.ZipFile(root / "duplicate.zip", "w") as archive:
    archive.writestr("same.txt", "one")
    archive.writestr("SAME.TXT", "two")
for name, kind in [("symlink.tar", tarfile.SYMTYPE), ("hardlink.tar", tarfile.LNKTYPE)]:
    with tarfile.open(root / name, "w") as archive:
        entry = tarfile.TarInfo("link")
        entry.type = kind
        entry.linkname = "../outside"
        archive.addfile(entry)
with zipfile.ZipFile(root / "cancel.zip", "w", compression=zipfile.ZIP_DEFLATED) as archive:
    archive.writestr("big.bin", b"x" * (512 * 1024))
with zipfile.ZipFile(root / "self.zip", "w") as archive:
    archive.writestr("self.zip", "must not overwrite source")
(root / "broken.zip").write_bytes(b"not a zip")
corrupt = bytearray((root / "deflated.zip").read_bytes())
central = corrupt.find(b"PK\x01\x02")
corrupt[central + 16] ^= 0xFF
corrupt[14] ^= 0xFF  # Match local and central metadata, but disagree with actual data.
(root / "badcrc.zip").write_bytes(corrupt)
(root / "magic.dat").write_bytes((root / "deflated.zip").read_bytes())
(root / "compressed.dat").write_bytes(gzip.compress(b"hello archive"))
bsdtar = shutil.which(os.environ.get("BSDTAR", "bsdtar"))
if bsdtar:
    source = root / "source" / "folder"
    source.mkdir(parents=True, exist_ok=True)
    (source / "hello.txt").write_bytes(b"hello archive")
    for name, option in [("test.7z", "--format=7zip"), ("test.tar.zst", "--zstd"), ("test.tar.lz4", "--lz4")]:
        subprocess.run([bsdtar, option, "-cf", str(root / name), "-C", str(source.parent), "folder"], check=True)
print(f"Archive fixtures written to {root}")
