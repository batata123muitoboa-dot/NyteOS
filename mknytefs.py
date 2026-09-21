#!/usr/bin/env python3

import os
import struct

SECTOR_SIZE = 512
IMAGE_SIZE = 1024 * 512

FS_START = 74
ENTRY_START = 75
BITMAP_SECTOR = 91
DATA_START = 92

MAX_ENTRIES = 128
ENTRY_SIZE = 64

FS_FILE = 1
FS_DIR = 2


def make_entry(name, size, start, sectors, parent, entry_type):
    name_bytes = name.encode("ascii")[:31]
    name_bytes += b"\0" * (32 - len(name_bytes))

    return struct.pack(
        "<32sIIII BB18s",
        name_bytes,
        size,
        start,
        sectors,
        parent,
        entry_type,
        1,
        b"\0" * 18
    )


def main():
    image = bytearray(IMAGE_SIZE)

    # --------------------------------------------------------
    # Superblock
    # --------------------------------------------------------

    superblock = bytearray(SECTOR_SIZE)

    superblock[0:8] = b"NYTEFS01"

    struct.pack_into("<I", superblock, 8, 1)
    struct.pack_into("<I", superblock, 12, IMAGE_SIZE // SECTOR_SIZE)
    struct.pack_into("<I", superblock, 16, ENTRY_START)
    struct.pack_into("<I", superblock, 20, BITMAP_SECTOR)
    struct.pack_into("<I", superblock, 24, DATA_START)
    struct.pack_into("<I", superblock, 28, MAX_ENTRIES)

    image[
        FS_START * SECTOR_SIZE:
        (FS_START + 1) * SECTOR_SIZE
    ] = superblock

    # --------------------------------------------------------
    # Entries
    # --------------------------------------------------------

    entries = bytearray(16 * SECTOR_SIZE)
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

    image[
        ENTRY_START * SECTOR_SIZE:
        (ENTRY_START + 16) * SECTOR_SIZE
    ] = entries

    # --------------------------------------------------------
    # Write files
    # --------------------------------------------------------

    def write_file(filename, start_sector):

        with open(filename, "rb") as f:
            data = f.read()

        offset = start_sector * SECTOR_SIZE
        end = offset + len(data)

        if end > IMAGE_SIZE:
            print("[!] File does not fit:", filename)
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
        write_file(filename, start_sector)

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