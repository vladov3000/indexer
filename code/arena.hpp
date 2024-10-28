struct Arena {
  U8* memory;
  I64 used;
  I64 size;
};

static Arena make_arena(I64 size) {
  U8* memory = (U8*) mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
  assert(memory != MAP_FAILED);

  Arena arena  = {};
  arena.memory = memory;
  arena.size   = size;
  return arena;
}

static I64 align(I64 address, I64 alignment) {
  return (address + alignment - 1) & ~(alignment - 1);
}

template <typename T>
static T* allocate(Arena* arena) {
  arena->used = align(arena->used, alignof(T));
  T* result   = (T*) &arena->memory[arena->used];
  memset(result, 0, sizeof(T));
  arena->used += sizeof(T);
  assert(arena->used <= arena->size);
  return result;
}
