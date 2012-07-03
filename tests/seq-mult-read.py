#!/usr/bin/env python

"""
seq-sing-read.py

Open a file specified on the command line for direct IO and read it block-by-block in a single thread.
"""

import sys, os
import mmap
from multiprocessing import Process

block_size  =  4096
nthreads    =  4

def buf_eq(m1, m2):
    """
    Tests whether two mmap's are the same byte-by-byte. Returns True if they
    are, False otherwise.
    """
    if len(m1) != len(m2):
        return False
    for byte1, byte2 in zip(m1, m2):
        if byte1 != byte2:
            return False
    return True

def run_test(filename, starting_block, num_blocks):
    with os.fdopen(os.open(filename, os.O_RDONLY)) as f:
        while True:
            blocks = list()
            f.seek(starting_block * block_size)
            for block in range(starting_block, starting_block + num_blocks):
                m = mmap.mmap(-1, block_size)
                m.write(f.read(block_size))
                blocks.append(m)
            f.seek(starting_block * block_size)
            for old in blocks:
                m = mmap.mmap(-1, block_size)
                m.write(f.read(block_size))
                if not buf_eq(m, old):
                    print "error"

def main():
    if len(sys.argv) != 4:
        print "wrong usage; must specify filename"
        sys.exit(1)
    starting_block = int(sys.argv[2])
    num_blocks = int(sys.argv[3])
    filename = sys.argv[1]
    processes = list()
    for num in range(nthreads):
        process = Process(target = run_test, args = (filename, starting_block, num_blocks))
        process.start()
        processes.append(process)
    try:
        for process in processes:
            process.join()
    except KeyboardInterrupt:
        for process in processes:
            process.terminate()

if __name__ == '__main__':
    main()
