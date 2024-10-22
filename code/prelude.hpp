#define length(array) (sizeof(array) / sizeof((array)[0]))

typedef int                I32;
typedef long long          I64;
typedef unsigned char      U8;
typedef unsigned long long U64;

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
};

static bool starts_with(String base, String prefix) {
  return prefix.size <= base.size && memcmp(base.data, prefix.data, prefix.size) == 0;
}

static String to_string(I64 n, U8 storage[20]) {
  U8* end   = &storage[20];
  U8* start = end;
  do {
    start  = start - 1;
    *start = n % 10 + '0';
    n      = n / 10;
  } while (n > 0);
  return String(start, end - start);
}
