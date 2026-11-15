#!/usr/bin/env python3

import os
import struct

SECTOR_SIZE = 512
IMAGE_SECTORS = 8192
IMAGE_SIZE = IMAGE_SECTORS * SECTOR_SIZE

STAGE2_START = 160
FS_START = 183
DATA_START = FS_START + 1

MAX_ENTRIES = 16384
ENTRY_SIZE = 64

FS_FILE = 1
FS_DIR = 2


def make_entry(name, size, start, sectors, parent, entry_type):
    name_bytes = name.encode("ascii")[:31]
    name_bytes += b"\0" * (32 - len(name_bytes))

    return struct.pack(
        "<32sQQQIBB2s",
        name_bytes,
        size,
        start,
        sectors,
        parent,
        entry_type,
        1,
        b"\0" * 14
    )


def main():
    image = bytearray(IMAGE_SIZE)

    # --------------------------------------------------------
    # Superblock
    # --------------------------------------------------------

    superblock = bytearray(SECTOR_SIZE)

    superblock[0:8] = b"NYTEFS01"
    superblock[32:40] = b"NYTESET1"

    entry_sectors = (
        MAX_ENTRIES * ENTRY_SIZE + SECTOR_SIZE - 1
    ) // SECTOR_SIZE
    bitmap_sectors = (
        IMAGE_SECTORS - DATA_START - entry_sectors + 4095
    ) // 4096
    bitmap_start = IMAGE_SECTORS - entry_sectors - bitmap_sectors
    entry_start = bitmap_start + bitmap_sectors

    struct.pack_into("<I", superblock, 8, 2)
    struct.pack_into("<I", superblock, 12, IMAGE_SECTORS & 0xFFFFFFFF)
    struct.pack_into("<I", superblock, 16, entry_start & 0xFFFFFFFF)
    struct.pack_into("<I", superblock, 20, bitmap_start & 0xFFFFFFFF)
    struct.pack_into("<I", superblock, 24, DATA_START)
    struct.pack_into("<I", superblock, 28, MAX_ENTRIES)
    struct.pack_into("<I", superblock, 40, IMAGE_SECTORS >> 32)
    struct.pack_into("<I", superblock, 44, entry_start >> 32)
    struct.pack_into("<I", superblock, 48, bitmap_start >> 32)
    struct.pack_into("<Q", superblock, 52, bitmap_sectors)
    struct.pack_into("<I", superblock, 60, entry_sectors)

    image[FS_START * SECTOR_SIZE:(FS_START + 1) * SECTOR_SIZE] = superblock

    # --------------------------------------------------------
    # Entries
    # --------------------------------------------------------

    entries = bytearray(entry_sectors * SECTOR_SIZE)
    entry_index = 0

    entries[
        entry_index * ENTRY_SIZE:
        (entry_index + 1) * ENTRY_SIZE
    ] = make_entry(
        "/", 0, 0, 0, 0, FS_DIR
    )

    entry_index += 1

    entries[
        entry_index * ENTRY_SIZE:
        (entry_index + 1) * ENTRY_SIZE
    ] = make_entry(
        "user", 0, 0, 0, 0, FS_DIR
    )

    entry_index += 1

    # --------------------------------------------------------
    # Files
    # --------------------------------------------------------

    files = []

    if os.path.exists("boot.bin"):
        files.append(("boot.bin", 0))

    if os.path.exists("stage2.bin"):
        files.append(("stage2.bin", 0))

    if os.path.exists("entry.bin"):
        files.append(("entry.bin", 0))

    if os.path.exists("kernel.bin"):
        files.append(("kernel.bin", 0))

    if os.path.exists("term.bmp"):
        files.append(("term.bmp", 0))

    if os.path.exists("files.bmp"):
        files.append(("files.bmp", 0))

    if os.path.exists("nyteos.bmp"):
        files.append(("nyteos.bmp", 0))

    if os.path.exists("settings.bmp"):
        files.append(("settings.bmp", 0))

    if os.path.exists("calc.bmp"):
        files.append(("calc.bmp", 0))

    if os.path.exists("dwallpaper.bmp"):
        files.append(("dwallpaper.bmp", 0))

    if os.path.exists("wallpaper2.bmp"):
        files.append(("wallpaper2.bmp", 0))

    if os.path.exists("3d.bmp"):
        files.append(("3d.bmp", 0))

    if os.path.exists("folder.bmp"):
        files.append(("folder.bmp", 0))

    if os.path.exists("file.bmp"):
        files.append(("file.bmp", 0))

    if os.path.exists("image.bmp"):
        files.append(("image.bmp", 0))

    if os.path.exists("code.bmp"):
        files.append(("code.bmp", 0))

    if os.path.exists("txt.bmp"):
        files.append(("txt.bmp", 0))

    # --------------------------------------------------------
    # Allocate files
    # --------------------------------------------------------

    next_sector = DATA_START
    file_entries = []

    for filename, _ in files:

        size = os.path.getsize(filename)

        sectors = (
            size + SECTOR_SIZE - 1
        ) // SECTOR_SIZE

        if filename == "boot.bin":
            start_sector = 0
        elif filename == "kernel.bin":
            start_sector = 1
        elif filename == "stage2.bin":
            start_sector = STAGE2_START
        else:
            start_sector = next_sector
            next_sector += sectors

        file_entries.append(
            (
                filename,
                size,
                start_sector,
                sectors
            )
        )

    # --------------------------------------------------------
    # Bitmap
    # --------------------------------------------------------

    bitmap = bytearray(bitmap_sectors * SECTOR_SIZE)
    for filename, size, start_sector, sectors in file_entries:
        if start_sector < DATA_START:
            continue

        for i in range(sectors):
            relative = start_sector + i - DATA_START
            bitmap[relative // 8] |= 1 << (relative % 8)

    image[bitmap_start * SECTOR_SIZE:(bitmap_start + bitmap_sectors) * SECTOR_SIZE] = bitmap

    # --------------------------------------------------------
    # Filesystem entries
    # --------------------------------------------------------

    for filename, size, start_sector, sectors in file_entries:

        if entry_index >= MAX_ENTRIES:
            print("[!] Too many filesystem entries.")
            return

        entries[
            entry_index * ENTRY_SIZE:
            (entry_index + 1) * ENTRY_SIZE
        ] = make_entry(
            filename,
            size,
            start_sector,
            sectors,
            0,
            FS_FILE
        )

        entry_index += 1

    image[entry_start * SECTOR_SIZE:(entry_start + entry_sectors) * SECTOR_SIZE] = entries

    # --------------------------------------------------------
    # Write files
    # --------------------------------------------------------

    def write_file(filename, start_sector, sectors):

        with open(filename, "rb") as f:
            data = f.read()

        offset = start_sector * SECTOR_SIZE
        end = offset + len(data)

        if end > IMAGE_SIZE:
            print("[!] File does not fit:", filename)
            raise SystemExit(1)

        if start_sector >= DATA_START and end > bitmap_start * SECTOR_SIZE:
            print("[!] File overlaps NyteFS metadata:", filename)
            raise SystemExit(1)

        if filename == "kernel.bin" and sectors > STAGE2_START - 1:
            print("[!] Kernel exceeds the reserved boot area.")
            raise SystemExit(1)

        if filename == "stage2.bin" and sectors > 23:
            print("[!] Stage2 exceeds the reserved boot area.")
            raise SystemExit(1)

        image[offset:end] = data

        print(
            "[+] %-12s sector=%d size=%d"
            % (
                filename,
                start_sector,
                len(data)
            )
        )

    for filename, size, start_sector, sectors in file_entries:
        write_file(filename, start_sector, sectors)

    # --------------------------------------------------------
    # Save
    # --------------------------------------------------------

    with open("nyteos.img", "wb") as f:
        f.write(image)

    print()
    print("[i] NyteFS image created.")
    print("[i] Size: %d bytes" % IMAGE_SIZE)


if __name__ == "__main__":
    main()
