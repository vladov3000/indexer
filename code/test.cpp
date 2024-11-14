#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "prelude.hpp"
#include "print.hpp"
#include "arena.hpp"

static void write_int(I32 fd, I64 n) {
  assert(write(fd, &n, sizeof(I64)) == sizeof(I64));
}

static void write_string(I32 fd, String s) {
  assert(write(fd, s.data, s.size) == s.size);
}

static void insert(Arena* arena, I32 fd, I64 offset, String word, I64 value) {
  I64 end = lseek(fd, 0, SEEK_END);
  assert(end != -1);
  
  write_int(fd, word.size);
  write_string(fd, word);
  write_int(fd, value);
  write_int(fd, 0);
}

static I64 read_int(I32 fd) {
  I64 n = 0;
  assert(read(fd, &n, sizeof(I64)) == sizeof(I64));
  return n;
}

static bool lookup(Arena* arena, I32 fd, I64 offset, String target, I64* value_out) {
  bool found = false;
  assert(lseek(fd, 0, SEEK_SET) != -1);

  while (1) {
    I64 word_size  = 0;
    I64 bytes_read = read(fd, &word_size, sizeof(I64));
    if (bytes_read == -1) {
      println(ERROR, get_error());
      flush();
    }
    assert(bytes_read != -1);
    if (bytes_read == 0) {
      break;
    }
  
    String word = allocate_bytes(arena, word_size, 1);
    assert(read(fd, word.data, word.size) == word.size);

    I64 value  = read_int(fd);
    I64 offset = read_int(fd);

    if (word == target) {
      found      = true;
      *value_out = value;
      break;
    }
  }

  return found;
}

I32 main() {
  atexit(flush);
  println(INFO "Running tests.");

  Arena arena = make_arena(1ll << 32);

  const char* index_path = "build/test.index";
  I32         fd         = open(index_path, O_RDWR | O_CREAT | O_EXCL, 0777);
  assert(fd != -1);
  assert(unlink(index_path) == 0);

  char   storage[] = "Hello0000000";
  String word      = storage;
  
  for (I64 i = 0; i < 999; i++) {
    I64 suffix = i;
    I64 end    = word.size;
    while (end > 0) {
      end       = end - 1;
      word[end] = suffix % 10 + '0';
      suffix    = suffix / 10;
    }
    
    insert(&arena, fd, 0, word, i);

    I64 value = 0;
    assert(lookup(&arena, fd, 0, word, &value));
    if (value != i) {
      println(INFO "i=", i, " value=", value);
      flush();
    }
    assert(value == i);
  }

  assert(close(fd) == 0);
}
