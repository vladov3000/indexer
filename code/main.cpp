#include <arpa/inet.h>
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/sendfile.h>
#endif

#include "prelude.hpp"
#include "print.hpp"
#include "arena.hpp"

#define RESPONSE_400 "HTTP/1.1 400\r\nContent-Length: 0\r\n\r\n"
#define RESPONSE_404 "HTTP/1.1 404\r\nContent-Length: 0\r\n\r\n"

static I32 bind(I32 fd, struct sockaddr_in address) {
  return bind(fd, (struct sockaddr*) &address, sizeof(address));
}

static I32 accept(I32 fd, struct sockaddr_in* address) {
  socklen_t address_size = sizeof(*address);
  return accept(fd, (struct sockaddr*) address, &address_size);
}

static struct iovec to_iovec(String s) {
  return (struct iovec) { .iov_base = s.data, .iov_len = (U64) s.size };
}

static struct iovec to_iovec(I32* n) {
  return (struct iovec) { .iov_base = n, .iov_len = sizeof(I32) };
}

static void write_response(I32 connection_fd, String response) {
  I64 bytes_written = write(connection_fd, response.data, response.size);
  if (bytes_written == -1) {
    println(ERROR "Failed to write to connection: ", get_error(), '.');
  } else if (bytes_written < response.size) {
    println(WARN "Wrote less bytes than expected.");
  }
}

static String read_file(const char* path) {
  I32 fd = open(path, O_RDONLY);
  if (fd == -1) {
    println(ERROR "Failed to open \"", path, "\": ", get_error(), '.');
    exit(EXIT_FAILURE);
  }

  struct stat info = {};
  if (fstat(fd, &info) == -1) {
    println(ERROR "Failed to stat \"", path, "\": ", get_error(), '.');
    exit(EXIT_FAILURE);
  }

  String result = {};
  result.size   = info.st_size;
  result.data   = (U8*) mmap(NULL, result.size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (result.data == MAP_FAILED) {
    println(ERROR "Failed to mmap \"", path, "\": ", get_error(), '.');
    exit(EXIT_FAILURE);
  }

  assert(close(fd) == 0);
  return result;
}

static void close_file(String text) {
  assert(munmap(text.data, text.size) == 0);
}

struct Parameters {
  String query;
  String start;
  String end;
  I32    page;
};

static void parse_parameter(String* input, Parameters* parameters) {
  I64    ampersand = find(*input, '&');
  String pair      = prefix(*input, ampersand);
  *input           = suffix(*input, ampersand + 1);
  
  I64    equals    = find(pair, '=');
  String key       = prefix(pair, equals);
  String value     = suffix(pair, equals + 1);
  
  for (I64 i = 0; i < value.size; i++) {
    I64    escape = find(value, '%', i);
    String digits = slice(value, escape + 1, escape + 3);
    if (digits.size == 2) {
      U8 first  = to_lower(digits[0]);
      U8 second = to_lower(digits[1]);
      if (is_hex(first) && is_hex(second)) {
	value[escape] = (from_hex(first) << 4) + from_hex(second);
	String rest   = suffix(value, escape + 3);
	if (rest.size > 0) {
	  memmove(&value[escape + 1], rest.data, rest.size);
	  value.size -= 2;
	}
      }
    }
    i = escape;
  }
  
  if (key == "query") {
    parameters->query = value;
  }
  if (key == "start") {
    parameters->start = value;
  }
  if (key == "end") {
    parameters->end = value;
  }
  if (key == "page") {
    I32 page = 0;
    for (I64 i = 0; i < value.size; i++) {
      if (!is_digit(value[i])) {
	page = 0;
	break;
      }
      page = 10 * page + value[i] - '0';
    }
    parameters->page = page;
  }
}

static Parameters parse_parameters(String input) {
  Parameters parameters = {};
  while (input.size > 0) {
    parse_parameter(&input, &parameters);
  }
  return parameters;
}

struct Offset {
  I64     value;
  Offset* next;
};

static Offset* make_offset(Arena* arena, I64 value) {
  Offset* offset = allocate<Offset>(arena);
  offset->value  = value;
  return offset;
}

struct Node {
  U32     is_black;
  String  word;
  Offset* first_offset;
  Offset* last_offset;
  Node*   children[2];
};

static void print_tree(Node* node, I64 indents) {
  for (I64 i = 0; i < indents; i++) {
    print(' ');
  }

  if (node == nullptr) {
    println("nil");
  } else {
    String color = node->is_black == 0 ? "\x1b[31m" : "\x1b[30m\x1b[47m";
    String clear = "\x1b[0m";
    println(color, node->word, clear);
  
    for (I64 i = 0; i < length(node->children); i++) {
      print_tree(node->children[i], indents + 1);
    }
  }
}

struct CheckResult {
  I64 depth;
  I64 count;
};

static CheckResult check_node(Node* node) {
  if (node == nullptr) {
    return (CheckResult) {};
  }
  
  assert(node->word.size > 0);

  I64 max_depth = 0;
  I64 min_depth = 0;
  I64 count     = 0;
  for (I64 i = 0; i < length(node->children); i++) {
    Node* child = node->children[i];
    if (child != nullptr) {
      I32  comparison = compare(node->word, child->word);
      if (i == 0) {
	assert(comparison > 0);
      }
      if (i == 1) {
	assert(comparison < 0);
      }
    }
    
    CheckResult result = check_node(child);
    if (result.depth > max_depth) {
      max_depth = result.depth;
    }
    if (min_depth == 0 || result.depth < min_depth) {
      min_depth = result.depth;
    }
    count += result.count;
  }

  CheckResult result = {};
  result.depth       = max_depth + 1;
  result.count       = count + 1;
  return result;
}

static Node* make_node(Arena* arena, String word, I64 offset) {
  Node* node         = allocate<Node>(arena);
  node->word         = word;
  node->first_offset = make_offset(arena, offset);
  node->last_offset  = node->first_offset;
  return node;
}

static Node* balance(Node* grandparent) {
  if (grandparent->is_black) {
    for (I64 parent_direction = 0; parent_direction < 2; parent_direction++) {
      Node* parent = grandparent->children[parent_direction];
      if (parent != nullptr && !parent->is_black) {
	Node* child = parent->children[parent_direction];
	if (child != nullptr && !child->is_black) {
	  child->is_black                         = true;
	  grandparent->children[parent_direction] = parent->children[1 - parent_direction];
	  parent->children[1 - parent_direction]  = grandparent;
	  return parent;
	}
	Node* brother = parent->children[1 - parent_direction];
	if (brother != nullptr && !brother->is_black) {
	  parent->is_black                        = true;
	  parent->children[1 - parent_direction]  = brother->children[parent_direction];
	  grandparent->children[parent_direction] = brother->children[1 - parent_direction];
	  brother->children[parent_direction]     = parent;
	  brother->children[1 - parent_direction] = grandparent;
	  return brother;
	}
      }
    }
  }
  return grandparent;
}

static Node* insert(Arena* node_arena, Arena* word_arena, Node* node, String word, I64 offset) {
  if (node == nullptr) {
    String new_word = allocate_bytes(word_arena, word.size, 1);
    memcpy(new_word.data, word.data, word.size);
    return make_node(node_arena, new_word, offset);
  }
  I32 comparison = compare(word, node->word);
  if (comparison < 0) {
    node->children[0] = insert(node_arena, word_arena, node->children[0], word, offset);
  } else if (comparison > 0) {
    node->children[1] = insert(node_arena, word_arena, node->children[1], word, offset);
  } else if (comparison == 0) {
    Offset* last      = node->last_offset;
    last->next        = make_offset(node_arena, offset);
    node->last_offset = last->next;
  }
  return balance(node);
}

static Offset* lookup(Node* node, String word) {
  if (node == nullptr) {
    return 0;
  }
  I32 comparison = compare(word, node->word);
  if (comparison < 0) {
    return lookup(node->children[0], word);
  } else if (comparison > 0) {
    return lookup(node->children[1], word);
  } else {
    return node->first_offset;
  }
}

static Node* index_logs(Arena* node_arena, Arena* word_arena, String logs, Node* node_root) {
  I64 line_start = 0;
  for (I64 i = 0; i <= logs.size; i++) {
    if (i == logs.size || logs[i] == '\n') {
      if (line_start != i) {
	String line       = slice(logs, line_start, i);
	I64    word_start = 0;
	I64    last_qoute = -1;
	for (I64 j = 0; j <= line.size; j++) {
	  if (j == line.size || line[j] == ' ') {
	    if (word_start != j) {
	      String word         = slice(line, word_start, j);
	      node_root           = insert(node_arena, word_arena, node_root, word, line_start);
	      node_root->is_black = true;
	    }
	    word_start = j + 1;
	  }
	  if (j < line.size && line[j] == '"') {
	    if (last_qoute == -1) {
	      last_qoute = j;
	    } else {
	      String qouted_word = slice(line, last_qoute + 1, j);
	      if (qouted_word.size > 0) {
		node_root           = insert(node_arena, word_arena, node_root, qouted_word, line_start);
		node_root->is_black = true;
	      }
	      last_qoute = -1;
	    }
	  }
	}
      }
      line_start = i + 1;
    }
  }
  CheckResult result = check_node(node_root);
  println(INFO "Built index with tree_depth=", result.depth, " node_count=", result.count, '.');
  flush();
  return node_root;
}

static time_t parse_time(String input, const char* format) {
  struct tm time   = {};
  char*     result = strptime((char*) input.data, format, &time);
  return result == NULL ? -1 : mktime(&time);
}

struct Index {
  String path;
  Node*  root;
  Index* next;
};

struct Query {
  String value;
  Query* child;
  Query* next;
};

static Query* parse_query(Arena* arena, String query) {
  Query* root    = allocate<Query>(arena);
  Query* current = root;
  for (I64 i = 0; i < query.size; i++) {
    I64    word_end = find(query, ' ', i);
    String word     = slice(query, i, word_end);
    i               = word_end;

    if (word == "OR") {
      Query* next   = allocate<Query>(arena);
      current->next = next;
      current       = next;
    } else if (word.size > 0) {
      Query* query   = allocate<Query>(arena);
      query->value   = word;
      query->next    = current->child;
      current->child = query;
    }
  }
  return root;
}

static void write_histogram(I32 connection_fd, I32 bins, I32* histogram) {
  I32 histogram_tag = 2;

  I64    chunk_size        = sizeof(histogram_tag) + sizeof(bins) + sizeof(I32) * bins;
  U8     storage[16]       = {};
  String chunk_size_string = to_hex_string(chunk_size, storage);

  struct iovec headers[] = {
    to_iovec(chunk_size_string),
    to_iovec("\r\n"),
    to_iovec(&histogram_tag),
    to_iovec(&bins),
    { .iov_base = histogram, .iov_len = sizeof(I32) * bins },
    to_iovec("\r\n"),
  };

  I64 bytes_written = writev(connection_fd, headers, length(headers));
  if (bytes_written == -1) {
    println(ERROR "Failed to write to connection: ", get_error(), '.');
  }
}

static void write_logs(Arena* arena, I32 connection_fd, String logs) {
  I64 saved = save(arena);

  I32 logs_tag = 1;
	
  I64 padding = align(logs.size, 4) - logs.size;
  logs.size  += padding;
  allocate_bytes(arena, padding, 1);

  I32 logs_size = logs.size;
	  
  I64    chunk_size        = sizeof(logs_tag) + sizeof(logs_size) + logs.size;
  U8     storage[16]       = {};
  String chunk_size_string = to_hex_string(chunk_size, storage);

  struct iovec headers[] = {
    to_iovec(chunk_size_string),
    to_iovec("\r\n"),
    to_iovec(&logs_tag),
    to_iovec(&logs_size),
    to_iovec(logs),
    to_iovec("\r\n"),
  };

  I64 bytes_written = writev(connection_fd, headers, length(headers));
  if (bytes_written == -1) {
    println(ERROR "Failed to write to connection: ", get_error(), '.');
  }

  restore(arena, saved);
}

static void run_query(
  Arena*     query_arena,
  char*      log_time_format,
  I32        connection_fd,
  Index*     index,
  Parameters parameters,
  Query*     query,
  I32        bins,
  I32*       histogram,
  String*    result
) {
  const char* query_time_format = "%Y-%m-%dT%H:%M";
  
  time_t start_time = parse_time(parameters.start, query_time_format);
  time_t end_time   = parse_time(parameters.end, query_time_format);

  String logs = read_file((char*) index->path.data);

  for (Query* or_query = query; or_query != nullptr; or_query = or_query->next) {
    Offset* offsets    = nullptr;
    bool    first_word = true;
    
    for (Query* and_query = or_query->child; and_query != nullptr; and_query = and_query->next) {
      String  word        = and_query->value;
      Offset* new_offsets = lookup(index->root, word);
      if (first_word) {
	offsets    = new_offsets;
	first_word = false;
      } else {
	Offset* previous = nullptr;
	for (Offset* i = offsets; i != nullptr; i = i->next) {
	  bool found = false;
	  for (Offset* j = new_offsets; j != nullptr; j = j->next) {
	    if (i->value == j->value) {
	      found = true;
	      break;
	    }
	  }
	  if (!found) {
	    if (previous == nullptr) {
	      if (offsets != nullptr) {
		offsets = offsets->next;
	      }
	    } else {
	      previous->next = previous->next->next;
	    }
	  } else {
	    previous = i;
	  }
	}
      }
    }

    I32 page_size    = 64;
    I32 min_offset   = page_size * parameters.page;
    I32 max_offset   = min_offset + page_size;
    I32 offset_count = 0;
    I32 wrote_logs   = false;
    
    I64 last_histogram_write = time(NULL);

    Offset* offset = offsets;
    while (offset != nullptr) {
      String line     = suffix(logs, offset->value);
      I64    line_end = find(line, '\n');
      line            = prefix(line, line_end + 1);

      {
	time_t time = -1;
	for (I64 i = 0; time == -1 && i < line.size; i++) {
	  time = parse_time(suffix(line, i), log_time_format);
	}
	if (time == -1) {
	  print(WARN "Failed to parse time as ", log_time_format, " in this line: ", line);
	}
    
	if (start_time <= time && time <= end_time) {
	  if (min_offset <= offset_count && offset_count < max_offset) {
	    String query_result = allocate_bytes(query_arena, line.size, 1);
	    memcpy(query_result.data, line.data, line.size);
	    result->size += line.size;
	  }

	  F32 value = (F32) (time - start_time) / (end_time - start_time);
	  histogram[(I32) (bins * value)]++;
	}
      }
      
      offset = offset->next;
      offset_count++;
      
      if (offset_count == max_offset) {
	write_histogram(connection_fd, bins, histogram);
	write_logs(query_arena, connection_fd, *result);
	wrote_logs = true;
      }

      I64 now = time(NULL);
      if (now != last_histogram_write) {
	write_histogram(connection_fd, bins, histogram);
	last_histogram_write = now;
      }
    }

    if (offset_count > 0 && !wrote_logs) {
      write_logs(query_arena, connection_fd, *result);
    }
  }

  write_histogram(connection_fd, bins, histogram);
  close_file(logs);
}

I32 main(I32 argc, char** argv) {
  atexit(flush);

  Arena arenas[3] = {};
  for (I64 i = 0; i < length(arenas); i++) {
    arenas[i] = make_arena(1ll << 34);
  }

  Arena* index_arena  = &arenas[0];
  Arena* node_arena   = &arenas[1];
  Arena* word_arena   = &arenas[2];
  Arena* offset_arena = &arenas[0];
  Arena* query_arena  = &arenas[1];
  
  if (argc != 3) {
    println(ERROR "Expected exactly two arguments, the time format and the path to the log file.");
    println("Usage: indexer TIME_FORMAT LOGS_PATH");
    exit(EXIT_FAILURE);
  }

  char*       time_format = argv[1];
  char*       logs_path   = argv[2];
  struct stat info        = {};
  if (stat(logs_path, &info)) {
    println(ERROR "Failed to stat \"", logs_path, "\": ", get_error(), '.');
    exit(EXIT_FAILURE);    
  }

  Index* index = nullptr;

  if (S_ISREG(info.st_mode)) {
    println(INFO "Indexing ", logs_path, '.');
    flush();

    String logs = read_file(logs_path);
    Node*  root = index_logs(node_arena, word_arena, logs, nullptr);
    close_file(logs);

    index       = allocate<Index>(index_arena);
    index->path = logs_path;
    index->root = root;
  }

  if (S_ISDIR(info.st_mode)) {
    DIR* dir = opendir(logs_path);
    assert(dir != NULL);

    while (true) {
      dirent* entry = readdir(dir);
      if (entry == NULL) {
	break;
      }

      String log_path = entry->d_name;
      if (log_path == "." || log_path == "..") {
	continue;
      }

      log_path = concatonate_paths(index_arena, logs_path, log_path);
      
      println(INFO "Indexing \"", log_path, "\".");
      flush();
      
      String logs = read_file((char*) log_path.data);
      Node*  root = index_logs(node_arena, word_arena, logs, nullptr);

      Index* new_index = allocate<Index>(index_arena);
      new_index->path  = log_path;
      new_index->root  = root;
      new_index->next  = index;
      index            = new_index;
      
      close_file(logs);
    }
    
    assert(closedir(dir) == 0);
  }
  
  I64 port = 2000;
  
  I32 listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd == -1) {
    println(ERROR "Failed to create socket to listen on: ", get_error(), '.');
    exit(EXIT_FAILURE);
  }

  I32 reuse      = 1;
  I32 set_result = setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  if (set_result == -1) {
    println(WARN "Failed to make socket reuse address: ", get_error(), '.');
  }
  
  struct sockaddr_in server_address = {};
  server_address.sin_family         = AF_INET;
  server_address.sin_port           = htons(port);

  I32 result = bind(listen_fd, server_address);
  if (result == -1) {
    String error   = get_error();
    String address = inet_ntoa(server_address.sin_addr);
    println(ERROR "Failed to bind socket to ", address, ':', port," on: ", error, '.');
    exit(EXIT_FAILURE);
  }

  I32 listen_result = listen(listen_fd, 256);
  if (listen_result == -1) {
    println(ERROR "Failed to listen on socket: ", get_error(), '.');
    exit(EXIT_FAILURE);
  }

  println(INFO "Listening on port ", port, '.');
  flush();

  while (true) {
    struct sockaddr_in client_address = {};
    I32                connection_fd  = accept(listen_fd, &client_address);
    if (connection_fd == -1) {
      print(ERROR "Failed to accept connection: ", get_error(), '.');
    } else {
      println(INFO "New connection from ", inet_ntoa(server_address.sin_addr), '.');

      U8  buffer[4096];
      I64 bytes_read = read(connection_fd, buffer, sizeof(buffer));
      if (bytes_read == -1) {
	println(ERROR "Failed to read from connection: ", get_error(), '.');
      }
      if (bytes_read == 0) {
	println(ERROR "Empty request.");
      } else {
	String request(buffer, bytes_read);
	println(INFO "Dumping request.");
	print(request);

	String query_prefix = "GET /api/query?";

	if (starts_with(request, query_prefix)) {
	  I64 saved = save(query_arena);
	  
	  String     rest            = suffix(request, query_prefix.size);
	  String     parameters_line = prefix(rest, find(rest, ' '));
	  Parameters parameters      = parse_parameters(parameters_line);
	  Query*     query           = parse_query(query_arena, parameters.query);

	  String header =
	    "HTTP/1.1 200 OK\r\n"
	    "Transfer-Encoding: chunked\r\n"
	    "Content-Type: application/octet-stream\r\n"
	    "\r\n";

	  I64 bytes_written = write(connection_fd, header.data, header.size);
	  if (bytes_written == -1) {
	    println(ERROR "Failed to write to connection: ", get_error(), '.');
	  }
	  
	  I32 histogram[100] = {};
	  I32 bins           = length(histogram);
	  
	  String logs = allocate_bytes(query_arena, 0, 1);
	  for (Index* i = index; i != nullptr; i = i->next) {
	    run_query(query_arena, time_format, connection_fd, i, parameters, query, bins, histogram, &logs);
	  }

	  String trailer = "0\r\n\r\n";
	  bytes_written  = write(connection_fd, trailer.data, trailer.size);
	  if (bytes_written == -1) {
	    println(ERROR "Failed to write to connection: ", get_error(), '.');
	  }

	  restore(query_arena, saved);
	} else {
	  const char* file_path    = nullptr;
	  String      content_type = {};
	  if (starts_with(request, "GET / ")) {
	    file_path    = "assets/index.html";
	    content_type = "text/html; charset=utf-8";
	  } else if (starts_with(request, "GET /styles.css ")) {
	    file_path    = "assets/styles.css";
	    content_type = "text/css";
	  } else if (starts_with(request, "GET /script.js ")) {
	    file_path    = "assets/script.js";
	    content_type = "text/javascript";
	  } else if (starts_with(request, "GET /favicon.ico ")) {
	    file_path    = "assets/favicon.ico";
	    content_type = "image/ico";
	  }

	  if (file_path == nullptr) {
	    write_response(connection_fd, RESPONSE_404);
	    println(ERROR "Invalid file path.");
	  } else {
	    I32 file_fd = open(file_path, O_RDONLY);
	    assert(file_fd != -1);
	  
	    struct stat info = {};
	    assert(fstat(file_fd, &info) != -1);
	    I64 file_size    = info.st_size;

	    U8     storage[20]    = {};
	    String content_length = to_string(file_size, storage);
	  
	    struct iovec headers[] = {
	      to_iovec("HTTP/1.1 200 OK\r\nContent-Length: "),
	      to_iovec(content_length),
	      to_iovec("\r\nContent-Type: "),
	      to_iovec(content_type),
	      to_iovec("\r\n\r\n")
	    };

#ifdef __APPLE__
	    struct sf_hdtr hdtr = {};
	    hdtr.headers        = headers;
	    hdtr.hdr_cnt        = length(headers);

	    off_t length      = 0;
	    I32   send_result = sendfile(file_fd, connection_fd, 0, &length, &hdtr, 0);
	    if (send_result == -1) {
	      println(ERROR "Failed to send file: ", get_error(), '.');
	    }
#endif
	    
#ifdef __linux__
	    I64 bytes_written = writev(connection_fd, headers, length(headers));
	    if (bytes_written == -1) {
	      println(ERROR "Failed to write headers: ", get_error(), '.');
	    }

	    bytes_written = sendfile(connection_fd, file_fd, 0, file_size);
	    if (bytes_written == -1) {
	      println(ERROR "Failed to send file: ", get_error(), '.');
	    }
#endif

	    assert(close(file_fd) == 0);
	  }
	}
      }
      if (close(connection_fd) == -1) {
	println(WARN "Failed to close socket: ", get_error(), '.');
      }
    }

    /* Print memory usage.
    for (I64 i = 0; i < length(arenas); i++) {
      println(INFO "arenas[", i, "].used=", arenas[i].used);
    }
    */
    
    flush();
  }
}
