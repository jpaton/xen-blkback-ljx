#define _GNU_SOURCE

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
#include <time.h>
#include <stdbool.h>
#include <string.h>

#define OPEN_FLAGS O_RDONLY | O_DIRECT
#define BLOCK_SIZE 4096 /* size of a block in bytes */
#define WORD_SIZE 4   /* size of 32-bit word in bytes */
#define FILENAME "tmpXXXXXX" /* template of temporary file name */
#define FILE_SIZE 10000   /* size of file in blocks */

#define for_each_block(block, size) for (block = 0; block < size; block++) 
#define for_each_word(word, size) for (word = 0; word < size; word++) 

/**
 * Test a condition. If the condition is true, call perror and exit with status
 * EXIT_FAILURE.
 * @param value The condition to be tested.
 * @param msg A parameter to pass to perror.
 */
static inline void die_on_true(bool value, char * msg) {
  if (value) {
    perror(msg);
    exit(EXIT_FAILURE);
  }
}

static inline void die_on_false(bool value, char * msg) {
  die_on_true(!value, msg);
}

/* Gets current time. Taken from https://gist.github.com/1087739 */
void current_time(struct timespec *ts) {
#ifdef __MACH__ // OS X does not have clock_gettime, use clock_get_time
  clock_serv_t cclock;
  mach_timespec_t mts;
  host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
  clock_get_time(cclock, &mts);
  mach_port_deallocate(mach_task_self(), cclock);
  ts->tv_sec = mts.tv_sec;
  ts->tv_nsec = mts.tv_nsec;
#else
  clock_gettime(CLOCK_REALTIME, ts);
#endif
}

/**
 * Opens a file for raw I/O (reading only).
 * @param filename The name of the file to be opened.
 * @return The file descriptor.
 */
int open_file(char * filename) {
  // open file for raw I/O
  int f = open(filename, OPEN_FLAGS, 0);
  return f;
}

void close_and_delete(int f, char * filename) {
  int ret;

  do {
    ret = close(f);
  } while (ret == -1 && errno == EINTR);

  die_on_true(unlink(filename), "unlink");
}

/**
 * Creates a new file of specified size in blocks and fills it with 
 * 32-bit unsigned ints. The first block is filled with 1's, the 
 * second block is filled with 2's, etc. Returns close(file descriptor).
 */
int fill_file(char * filename, unsigned int size) {
  int f = mkstemp(filename);
  uint32_t buf[BLOCK_SIZE];
  int ret;

  if (f == -1)
    return f;

  for (unsigned int block = 0; block < size; block++) {
    for (unsigned int word_in_block = 0; word_in_block < BLOCK_SIZE / WORD_SIZE; word_in_block++) 
      buf[word_in_block] = block + 1;
    if ((ret = write(f, buf, BLOCK_SIZE)) != BLOCK_SIZE) {
      /* error, or not enough space */
      if (ret >= 0) 
        errno = ENOSPC;
      close_and_delete(f, filename);
      return -1;
    }
  }

  return close(f);
}

/**
 * Uses posix_memalign to get a buffer aligned to appropriate boundaries for
 * direct I/O.
 */
int get_buf(void ** buf, size_t size) {
  return posix_memalign(buf, BLOCK_SIZE, size);
}

/**
 * Reads each block and makes sure they are correct. Does a single read through.
 * Size is in blocks.
 */
int test_seq_read(int f, int size) {
  unsigned int block;
  unsigned int word;
  uint32_t * buf;

  die_on_true(get_buf((void **)&buf, BLOCK_SIZE), "get_buf");

  for_each_block(block, size) {
    die_on_true(read(f, buf, BLOCK_SIZE) < 0, "read");
    for_each_word(word, BLOCK_SIZE / WORD_SIZE) {
      if (buf[word] != block + 1) {
        printf("%d, %d\n", block, word);
        free(buf);
        return -1;
      }
    }
  }

  free(buf);
  return 0;
}

int main(int argc, char ** argv) {
  char * filename;
  int f;

  if (argc != 2) {
    printf("Usage: %s <directory>\n", argv[0]);
    exit(1);
  }
  filename = malloc(strlen(FILENAME) + strlen(argv[1]) + 1);
  die_on_false(filename, "malloc");
  strcpy(filename, argv[1]);
  strcat(filename, "/");
  strcat(filename, FILENAME);
  die_on_true(fill_file(filename, FILE_SIZE), "fill_file");

  f = open(filename, O_RDONLY | O_DIRECT);
  die_on_false(f, "open");
  
  if (test_seq_read(f, FILE_SIZE) < 0)
    printf("FAILED\n");
  else
    printf("PASSED\n");

  close_and_delete(f, filename);
}
