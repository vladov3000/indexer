#define length(array) (sizeof(array) / sizeof((array)[0]))

typedef int                I32;
typedef long long          I64;
typedef unsigned char      U8;
typedef unsigned int       U32;
typedef unsigned long long U64;
typedef float              F32;

template <typename A>
static A min(A a, A b) {
  return a < b ? a : b;
}

template <typename A>
static A max(A a, A b) {
  return a < b ? a : b;
}

static U8 to_lower(U8 c) {
  return 'A' <= c && c <= 'Z' ? (c - 'A' + 'a') : c;
}

static bool is_hex(U8 c) {
  return ('0' <= c && c <= '9') || ('a' <= c && c <= 'z');
}

static U8 from_hex(U8 c) {
  return '0' <= c && c <= '9' ? (c - '0') : (c - 'a' + 10);
}

struct String {
  U8* data;
  I64 size;

  String() {
    data = nullptr;
    size = 0;
  }

  String(const char* string) {
    data = (U8*) string;
    size = strlen(string);
  }

  String(U8* data, I64 size) {
    this->data = data;
    this->size = size;
  }

  U8& operator[](I64 index) {
    return data[index];
  }
};

static bool operator==(String a, String b) {
  return a.size == b.size && memcmp(a.data, b.data, a.size) == 0;
}

static I32 compare(String a, String b) {
  I32 comparison = memcmp(a.data, b.data, min(a.size, b.size));
  if (comparison == 0) {
    if (a.size == b.size) {
      return 0;
    } else {
      return a.size < b.size ? -1 : 1;
    }
  } else {
    return comparison;
  }
}

static bool starts_with(String base, String prefix) {
  return prefix.size <= base.size && memcmp(base.data, prefix.data, prefix.size) == 0;
}

static String to_string(I64 n, U8 storage[20]) {
  U8*  end      = &storage[20];
  U8*  start    = end;
  bool negative = false;
  if (n < 0) {
    negative = true;
    n        = -n;
  }
  do {
    start  = start - 1;
    *start = n % 10 + '0';
    n      = n / 10;
  } while (n > 0);
  if (negative) {
    start  = start - 1;
    *start = '-';
  }
  return String(start, end - start);
}

static String suffix(String base, I64 start) {
  start      = min(start, base.size);
  base.data += start;
  base.size -= start;
  return base;
}

static String prefix(String base, I64 end) {
  base.size = min(end, base.size);
  return base;
}

static String slice(String base, I64 start, I64 end) {
  return prefix(suffix(base, start), end - start);
}

static I64 find(String base, char c, I64 start = 0) {
  start      = min(start, base.size);
  U8* result = (U8*) memchr(&base.data[start], c, base.size);
  return result == NULL ? base.size : (result - base.data);
}

static bool contains(String base, String target) {
  for (I64 i = 0; i < base.size; i++) {
    if (starts_with(suffix(base, i), target)) {
      return true;
    }
  }
  return false;
}
