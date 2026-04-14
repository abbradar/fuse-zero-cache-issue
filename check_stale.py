#!/usr/bin/env python3

import os, sys, threading, time

FILL = 0xAA
STAT_THREADS = 4


def stat_hammer(path, stop):
    """Tight-loop stat() to force FUSE getattr → i_size updates."""
    while not stop.is_set():
        try:
            os.stat(path)
        except OSError:
            pass


def main():
    if len(sys.argv) != 2:
        sys.exit(f"usage: {sys.argv[0]} MOUNTPOINT/testfile")

    path = sys.argv[1]
    stop = threading.Event()

    for _ in range(STAT_THREADS):
        threading.Thread(target=stat_hammer, args=(path, stop), daemon=True).start()

    fd = os.open(path, os.O_RDONLY)
    offset = 0
    hits = 0
    t0 = time.monotonic()

    print(f"reading {path}, checking for stale zeros ...")
    try:
        while hits < 20:
            size = os.fstat(fd).st_size
            if offset >= size:
                time.sleep(0.001)
                continue

            data = os.pread(fd, size - offset, offset)
            if not data:
                time.sleep(0.001)
                continue

            zero = data.find(b"\x00")
            if zero != -1:
                foff = offset + zero
                dt = time.monotonic() - t0
                print(
                    f"  STALE ZERO at offset {foff} "
                    f"(page {foff // 4096}, +{foff % 4096}) "
                    f"t={dt:.3f}s"
                )
                hits += 1

            offset += len(data)
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        os.close(fd)

    if hits:
        print(
            f"\nBug reproduced: {hits} stale-zero region(s) in "
            f"{time.monotonic() - t0:.1f}s ({offset} bytes read)."
        )
        sys.exit(1)
    else:
        print(f"No corruption after {offset} bytes.")


if __name__ == "__main__":
    main()
