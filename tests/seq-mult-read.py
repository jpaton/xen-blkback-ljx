#!/usr/bin/env python

"""
seq-sing-read.py

Open a file specified on the command line for direct IO and read it block-by-block in a single thread.
"""

import sys, os
import mmap
from multiprocessing import Process
import directio

block_size  =  4096
nthreads    =  4

def buf_eq(m1, m2, offset):
    """
    Tests whether m1 == m2[offset:offset + len(m1)]
    """
    for byte in range(len(m1)):
        if m1[byte] != m2[offset + byte]:
            return False
    return True

def read_in_file(filename, starting_block, num_blocks):
    global m_file
    
    m_file = mmap.mmap(-1, block_size * num_blocks)
    f = directio.open(filename, directio.O_RDONLY)
    #with os.fdopen(os.open(filename, os.O_RDONLY)) as f:
    f.seek(starting_block * block_size)
    for block in range(num_blocks):
        directio.read(f, m_file)
        #m_file.write(f.read(block_size))

def run_test(filename, starting_block, num_blocks):
    global m_file

    m = mmap.mmap(-1, block_size)
    f = directio.open(filename, directio.O_RDONLY)
    #with os.fdopen(os.open(filename, os.O_RDONLY)) as f:
    while True:
        f.seek(starting_block * block_size)
        for block in range(num_blocks):
            m.seek(0)
            directio.read(f, m)
            #m.write(f.read(block_size))
            if not buf_eq(m, m_file, block * block_size):
                print "error"

def main():
    if len(sys.argv) != 4:
        print "wrong usage; must specify filename"
        sys.exit(1)
    starting_block = int(sys.argv[2])
    num_blocks = int(sys.argv[3])
    filename = sys.argv[1]
    processes = list()
    read_in_file(filename, starting_block, num_blocks)
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
