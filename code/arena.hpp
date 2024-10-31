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

static String allocate_bytes(Arena* arena, I64 size, I64 alignment) {
  arena->used = align(arena->used, alignment);
  U8* result  = (U8*) &arena->memory[arena->used];
  memset(result, 0, size);
  arena->used += size;
  assert(arena->used <= arena->size);
  return String(result, size);
}

template <typename T>
static T* allocate(Arena* arena) {
  return (T*) allocate_bytes(arena, sizeof(T), alignof(T)).data;
}

template <typename T>
static T* allocate_array(Arena* arena, I64 count) {
  return (T*) allocate_bytes(arena, sizeof(T) * count, alignof(T)).data;
}

static void destroy(Arena* arena) {
  assert(munmap(arena->memory, arena->size) == 0);
}

static I64 save(Arena* arena) {
  return arena->used;
}

static void restore(Arena* arena, I64 saved) {
  arena->used = saved;
}

static String concatonate_paths(Arena* arena, String a, String b) {
  String result = allocate_bytes(arena, a.size + b.size + 2, 1);
  memcpy(result.data, a.data, a.size);
  result.data[a.size] = '/';
  memcpy(&result.data[a.size + 1], b.data, b.size);
  result.data[result.size - 1] = 0;
  return result;
}
