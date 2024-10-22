#define INFO  "\x1b[32;1mINFO \x1b[0m "
#define WARN  "\x1b[33;1mWARN \x1b[0m "
#define ERROR "\x1b[31;1mERROR\x1b[0m "

static U8  print_buffer[4096];
static I64 print_buffered;

static void flush() {
  if (print_buffered > 0) {
    write(STDOUT_FILENO, print_buffer, print_buffered);
    print_buffered = 0;
  }
}

static void print(char c) {
  if (print_buffered == sizeof(print_buffer)) {
    flush();
  }
  print_buffer[print_buffered] = c;
  print_buffered++;
}

static void print(String message) {
  if (message.size > sizeof(print_buffer)) {
    flush();
    write(STDOUT_FILENO, message.data, message.size);
  } else {
    if (message.size > sizeof(print_buffer) - print_buffered) {
      flush();
    }
    memcpy(&print_buffer[print_buffered], message.data, message.size);
    print_buffered += message.size;
  }
}

static void print(I64 n) {
  U8 storage[20] = {};
  print(to_string(n, storage));
}

static void print(auto first, auto second, auto... rest) {
  print(first);
  print(second);
  (print(rest), ...);
}

static void println(auto... arguments) {
  (print(arguments), ...);
  print('\n');
}

static String get_error() {
  return strerror(errno);
}
