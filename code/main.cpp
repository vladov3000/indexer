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
};

static void parse_parameter(String* input, Parameters* parameters) {
  I64    ampersand = find(*input, '&');
  String pair      = prefix(*input, ampersand);
  *input           = suffix(*input, ampersand + 1);
  
  I64    equals    = find(pair, '=');
  String key       = prefix(pair, equals);
  String value     = suffix(pair, equals + 1);
  if (key == "query") {
    parameters->query = value;
  }
  if (key == "start") {
    parameters->start = value;
  }
  if (key == "end") {
    parameters->end = value;
  }
}

static Parameters parse_parameters(String input) {
  Parameters parameters = {};
  while (input.size > 0) {
    parse_parameter(&input, &parameters);
  }
  return parameters;
}

struct Range {
  I64 start;
  I64 size;
};

struct Offset {
  Range   range;
  Offset* next;
};

static Offset* make_offset(Arena* arena, Range range) {
  Offset* offset = allocate<Offset>(arena);
  offset->range  = range;
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

static Node* make_node(Arena* arena, String word, Range range) {
  Node* node         = allocate<Node>(arena);
  node->word         = word;
  node->first_offset = make_offset(arena, range);
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

static Node* insert(Arena* node_arena, Arena* word_arena, Node* node, String word, Range range) {
  if (node == nullptr) {
    String new_word = allocate_bytes(word_arena, word.size, 1);
    memcpy(new_word.data, word.data, word.size);
    return make_node(node_arena, new_word, range);
  }
  I32 comparison = compare(word, node->word);
  if (comparison < 0) {
    node->children[0] = insert(node_arena, word_arena, node->children[0], word, range);
  } else if (comparison > 0) {
    node->children[1] = insert(node_arena, word_arena, node->children[1], word, range);
  } else if (comparison == 0) {
    Offset* last      = node->last_offset;
    last->next        = make_offset(node_arena, range);
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
	      Range  range        = { line_start, line.size };
	      node_root           = insert(node_arena, word_arena, node_root, word, range);
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
		Range  range        = { line_start, line.size };
		node_root           = insert(node_arena, word_arena, node_root, qouted_word, range);
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

static String query(
  Arena* arena, Node* node_root, const char* logs_path, Parameters parameters, I32 bins, I32* histogram
) {
  const char* query_time_format = "%Y-%m-%dT%H:%M";

  // @Feature make this a parameter.
  // const char* log_time_format = "%Y/%m/%d %H:%M:%S"; <-- format for slogs
  const char* log_time_format = "%Y-%m-%dT%H:%M:%S";
  
  time_t start_time = parse_time(parameters.start, query_time_format);
  time_t end_time   = parse_time(parameters.end, query_time_format);

  String  logs       = read_file(logs_path);
  String  result     = allocate_bytes(arena, 0, 1);
  Offset* offset     = lookup(node_root, parameters.query);
  I64     line_count = 0;
  while (offset != nullptr) {
    String line     = suffix(logs, offset->range.start);
    I64    line_end = find(line, '\n');
    line            = prefix(line, line_end + 1);

    time_t time = -1;
    for (I64 i = 0; time == -1 && i < line.size; i++) {
      time = parse_time(suffix(line, i), log_time_format);
    }
    if (time == -1) {
      print(WARN "Failed to parse time as ", log_time_format, " in this line: ", line);
    }
    assert(contains(line, parameters.query));
    
    if (start_time <= time && time <= end_time) {
      String query_result = allocate_bytes(arena, line.size, 1);
      memcpy(query_result.data, line.data, line.size);
      result.size += line.size;

      F32 value = (F32) (time - start_time) / (end_time - start_time);
      histogram[(I32) (bins * value)]++;      
    }

    offset = offset->next;
    line_count++;
  }

  close_file(logs);
  return result;
}

struct Index {
  String path;
  Node*  root;
  Index* next;
};

I32 main(I32 argc, char** argv) {
  atexit(flush);

  Arena arenas[3] = {};
  for (I64 i = 0; i < length(arenas); i++) {
    arenas[i] = make_arena(1ll << 34);
  }

  Arena* index_arena = &arenas[0];
  Arena* node_arena  = &arenas[1];
  Arena* word_arena  = &arenas[2];
  Arena* query_arena = &arenas[1];
  
  if (argc != 2) {
    print(ERROR "Expected exactly one argument, the path to the log file.\n");
    exit(EXIT_FAILURE);
  }

  char*       logs_path = argv[1];
  struct stat info      = {};
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
	  String     rest            = suffix(request, query_prefix.size);
	  String     parameters_line = prefix(rest, find(rest, ' '));
	  Parameters parameters      = parse_parameters(parameters_line);

	  I32 histogram[100] = {};
	  I32 bins           = length(histogram);

	  String filtered_logs = allocate_bytes(query_arena, 0, 1);
	  for (Index* i = index; i != nullptr; i = i->next) {
	    char* log_path = (char*) i->path.data;
	    filtered_logs.size += query(query_arena, i->root, log_path, parameters, bins, histogram).size;
	  }

	  I64 content_length = sizeof(bins) + sizeof(histogram) + filtered_logs.size;
	  U8  storage[20]    = {};
		
	  struct iovec headers[] = {
	    to_iovec("HTTP/1.1 200 OK\r\nContent-Length: "),
	    to_iovec(to_string(content_length, storage)),
	    to_iovec("\r\nContent-Type: application/octet-stream\r\n\r\n"),
	    { .iov_base = &bins,     .iov_len = sizeof(bins)      },
	    { .iov_base = histogram, .iov_len = sizeof(histogram) },
	    to_iovec(filtered_logs),
	  };

	  I64 bytes_written = writev(connection_fd, headers, length(headers));
	  if (bytes_written == -1) {
	    println(ERROR "Failed to write to connection: ", get_error(), '.');
	  }

	  // query_arena->used = 0;
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

	    U8     storage[20]    = {};
	    String content_length = to_string(info.st_size, storage);
	  
	    struct iovec headers[] = {
	      to_iovec("HTTP/1.1 200 OK\r\nContent-Length: "),
	      to_iovec(content_length),
	      to_iovec("\r\nContent-Type: "),
	      to_iovec(content_type),
	      to_iovec("\r\n\r\n")
	    };
	
	    struct sf_hdtr hdtr = {};
	    hdtr.headers        = headers;
	    hdtr.hdr_cnt        = length(headers);

	    off_t length      = 0;
	    I32   send_result = sendfile(file_fd, connection_fd, 0, &length, &hdtr, 0);
	    if (send_result == -1) {
	      println(ERROR "Failed to send file: ", get_error(), '.');
	    }

	    assert(close(file_fd) == 0);
	  }
	}
      }
      if (close(connection_fd) == -1) {
	println(WARN "Failed to close socket: ", get_error(), '.');
      }
    }
    flush();
  }
}
