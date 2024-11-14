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

struct Entry {
  String word;
  I64    line_start;
};


#define BTREE_SIZE (2 * 1024 * 1024)
#define MAX_ARITY  ((BTREE_SIZE - 2 * sizeof(I64)) / (3 * sizeof(I64)))
#define PADDING    ((BTREE_SIZE - 2 * sizeof(I64)) % (3 * sizeof(I64)))

struct BTree {
  I64 arity;
  I64 word_offsets [MAX_ARITY];
  I64 word_sizes   [MAX_ARITY];
  I64 child_offsets[MAX_ARITY + 1];
  U8  padding      [PADDING];
};

static_assert(sizeof(BTree) == BTREE_SIZE);

I32 main() {
  atexit(flush);
  println(INFO "Running tests.");

  Arena arenas[2] = {};
  for (I64 i = 0; i < length(arenas); i++) {
    arenas[i] = make_arena(1ll << 32);
  }

  I32 raw_fd = open("examples/slog", O_RDONLY);
  assert(raw_fd != -1);

  I32 words_fd     = open("build/words", O_RDWR | O_CREAT | O_TRUNC, 0777);
  I64 words_fd_end = 0;
  assert(words_fd != -1);

  I32 btree_fd = open("build/btree", O_RDWR | O_CREAT | O_TRUNC, 0777);
  assert(btree_fd != -1);

  I64    saved = save(&arenas[0]);
  BTree* btree = allocate<BTree>(&arenas[0]);
  assert(write(btree_fd, btree, sizeof(BTree)) == sizeof(BTree));
  restore(&arenas[0], saved);

  I64    already_read = 0;
  String raw          = allocate_bytes(&arenas[0], 512, 1);

  println((I64) MAX_ARITY);
  
  while (1) {
    I64 saved = save(&arenas[0]);
    
    I64 bytes_read = read(raw_fd, &raw.data[already_read], raw.size);
    assert(bytes_read != -1);
    bytes_read += already_read;
    if (bytes_read == 0) {
      break;
    }

    Arena* entry_arena = &arenas[0];
    Entry* entries     = end<Entry>(entry_arena);
    I64    entry_count = 0;

    I64 raw_index  = 0;
    I64 line_start = 0;
    I64 word_start = 0;
    while (raw_index < bytes_read) {
      if (raw[raw_index] == '\n') {
	line_start = raw_index + 1;
      }
    
      if (raw[raw_index] == ' ' || raw[raw_index] == '\n') {
	if (raw_index != word_start) {
	  Entry* entry      = allocate<Entry>(entry_arena);
	  entry->word       = String(&raw[word_start], raw_index - word_start);
	  entry->line_start = line_start;
	  entry_count++;
	}
      
	word_start = raw_index + 1;
      }

      raw_index++;
    }

    for (I64 entry_index = 0; entry_index < entry_count; entry_index++) {
      Entry  entry      = entries[entry_index];
      String word       = entry.word;
      I64    line_start = entry.line_start;

      assert(word.size > 0);

      I64    final_word_offset = -1;
      I64    btree_offset      = 0;
      BTree* btree             = NULL;
      I64    btree_index       = 0;

      do {
	btree = allocate<BTree>(&arenas[0]);
	assert(pread(btree_fd, btree, sizeof(BTree), btree_offset) == sizeof(BTree));

	String test_word  = allocate_bytes(&arenas[0], word.size, 1);
	I64    comparison = 0;
	btree_index       = 0;

	while (btree_index < btree->arity) {
	  I64 word_size   = btree->word_sizes[btree_index];
	  I64 word_offset = btree->word_offsets[btree_index];
	
	  if (word_size == word.size) {
	    assert(pread(words_fd, test_word.data, word_size, word_offset) == word_size);
	    comparison = memcmp(word.data, test_word.data, word_size);
	    if (comparison <= 0) {
	      break;
	    }
	  }

	  btree_index++;
	}

	if (comparison == 0) {
	  final_word_offset = btree->word_offsets[btree_index];
	  break;
	}

	btree_offset = btree->child_offsets[btree_index];
      } while (btree_offset > 0);

      if (final_word_offset != -1) {
	break;
      }
      
      assert(write(words_fd, word.data, word.size) == word.size);
      final_word_offset = words_fd_end;
      words_fd_end     += word.size;

      assert(btree->arity < MAX_ARITY);

      I64 to_move = btree->arity - btree_index;
      if (to_move > 0) {
	memmove(&btree->word_offsets [btree_index + 1], &btree->word_offsets [btree_index], to_move);
	memmove(&btree->word_sizes   [btree_index + 1], &btree->word_sizes   [btree_index], to_move);
	memmove(&btree->child_offsets[btree_index + 1], &btree->child_offsets[btree_index], to_move);
      }

      btree->word_offsets[btree_index] = final_word_offset;
      btree->word_sizes  [btree_index] = word.size;
      btree->arity++;
    }

    already_read = bytes_read - word_start;
    if (already_read > 0) {
      memmove(raw.data, &raw.data[word_start], already_read);
    }

    restore(&arenas[0], saved);
  }
}
