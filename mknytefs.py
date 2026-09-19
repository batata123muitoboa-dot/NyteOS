#!/usr/bin/env python3

import os
import struct

SECTOR_SIZE = 512
IMAGE_SIZE = 1024 * 1024

FS_START = 18
ENTRY_START = 19
BITMAP_SECTOR = 35
DATA_START = 36

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

    struct.pack_into("<I", superblock, 8, 1)              # version
    struct.pack_into("<I", superblock, 12, IMAGE_SIZE // SECTOR_SIZE)
    struct.pack_into("<I", superblock, 16, ENTRY_START)
    struct.pack_into("<I", superblock, 20, BITMAP_SECTOR)
    struct.pack_into("<I", superblock, 24, DATA_START)
    struct.pack_into("<I", superblock, 28, MAX_ENTRIES)

    image[FS_START * SECTOR_SIZE:
          (FS_START + 1) * SECTOR_SIZE] = superblock

    # --------------------------------------------------------
    # Entries
    # --------------------------------------------------------

    entries = bytearray(16 * SECTOR_SIZE)

    entries[0:ENTRY_SIZE] = make_entry(
        "/",
        0,
        0,
        0,
        0,
        FS_DIR
    )

    entries[ENTRY_SIZE:ENTRY_SIZE * 2] = make_entry(
        "user",
        0,
        0,
        0,
        0,
        FS_DIR
    )

    entries[ENTRY_SIZE * 2:ENTRY_SIZE * 3] = make_entry(
        "boot.bin",
        os.path.getsize("boot.bin"),
        0,
        0,
        0,
        FS_FILE
    )

    if os.path.exists("entry.bin"):
        entries[ENTRY_SIZE * 3:ENTRY_SIZE * 4] = make_entry(
            "entry.bin",
            os.path.getsize("entry.bin"),
            0,
            0,
            0,
            FS_FILE
        )

    entries[ENTRY_SIZE * 4:ENTRY_SIZE * 5] = make_entry(
        "kernel.bin",
        os.path.getsize("kernel.bin"),
        1,
        (os.path.getsize("kernel.bin") + 511) // 512,
        0,
        FS_FILE
    )

    image[
        ENTRY_START * SECTOR_SIZE:
        (ENTRY_START + 16) * SECTOR_SIZE
    ] = entries

    # --------------------------------------------------------
    # Files
    # --------------------------------------------------------

    def write_file(filename, start_sector):
        with open(filename, "rb") as f:
            data = f.read()

        offset = start_sector * SECTOR_SIZE
        image[offset:offset + len(data)] = data

    write_file("boot.bin", 0)
    write_file("kernel.bin", 1)

    if os.path.exists("entry.bin"):
        pass

    # --------------------------------------------------------
    # Save
    # --------------------------------------------------------

    with open("nyteos.img", "wb") as f:
        f.write(image)

    print("NyteFS criado.")


if __name__ == "__main__":
    main()
