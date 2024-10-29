#include <arpa/inet.h>
#include <assert.h>
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
  return result;
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

struct Offset {
  I64 value;
  I64 next;
};

static Arena   offset_arena;
static Offset* offsets;
static I64     offset_count = 1;

static I64 make_offset(I64 value) {
  Offset* offset = &offsets[offset_count];
  offset->value  = value;
  return offset_count++;
}

struct Node {
  U32    is_black;
  String word;
  I64    first_offset;
  I64    last_offset;
  I64    children[2];
};

static Node* nodes;
static Arena node_arena;
static I64   node_count = 1;
static I64   node_root  = 1;

static void print_tree(I64 node_index, I64 indents) {
  for (I64 i = 0; i < indents; i++) {
    print(' ');
  }

  if (node_index == 0) {
    println("nil");
  } else {
    Node   node  = nodes[node_index];
    String color = node.is_black == 0 ? "\x1b[31m" : "\x1b[30m\x1b[47m";
    String clear = "\x1b[0m";
    println(color, node.word, clear);
  
    for (I64 i = 0; i < length(node.children); i++) {
      print_tree(node.children[i], indents + 1);
    }
  }
}

static I64 check_node(I64 node_index) {
  if (node_index == 0) {
    return 0;
  }
  
  assert(0 < node_index && node_index < node_count);

  Node node = nodes[node_index];
  assert(node.word.size > 0);
  // assert(0 < node.first_offset && node.first_offset < offset_count);
  // assert(0 < node.last_offset  && node.last_offset  < offset_count);

  I64 max_depth = 0;
  I64 min_depth = 0;
  for (I64 i = 0; i < length(node.children); i++) {
    I64 child_index = node.children[i];
    if (child_index != 0) {
      Node child      = nodes[child_index];
      I32  comparison = compare(node.word, child.word);
      if (i == 0) {
	assert(comparison > 0);
      }
      if (i == 1) {
	assert(comparison < 0);
      }
    }
    
    I64 depth = check_node(child_index);
    if (depth > max_depth) {
      max_depth = depth;
    }
    if (min_depth == 0 || depth < min_depth) {
      min_depth = depth;
    }
  }
  
  I64 inbalance = abs(max_depth - min_depth);
  if (inbalance > 1) {
    //println(INFO "max_depth=", max_depth, " min_depth=", min_depth);
  }
  // assert(inbalance <= 1);
  return max_depth + 1;
}

static I64 make_node(String word, I64 value) {
  Node* node         = allocate<Node>(&node_arena);
  node->word         = word;
  node->first_offset = make_offset(value);
  node->last_offset  = node->first_offset;
  return node_count++;
}

static I64 balance(I64 grandparent_index) {
  Node* grandparent = &nodes[grandparent_index];
  if (grandparent->is_black) {
    for (I64 parent_direction = 0; parent_direction < 2; parent_direction++) {
      I64 parent_index = grandparent->children[parent_direction];
      if (parent_index != 0) {
	Node* parent = &nodes[parent_index];
	if (!parent->is_black) {
	  I64 child_index = parent->children[parent_direction];
	  if (child_index != 0) {
	    Node* child = &nodes[child_index];
	    if (!child->is_black) {
	      child->is_black                         = true;
	      grandparent->children[parent_direction] = parent->children[1 - parent_direction];
	      parent->children[1 - parent_direction]  = grandparent_index;
	      return parent_index;
	    }
	  }
	  I64 brother_index = parent->children[1 - parent_direction];
	  if (brother_index != 0) {
	    Node* brother = &nodes[brother_index];
	    if (!brother->is_black) {
	      parent->is_black                        = true;
	      parent->children[1 - parent_direction]  = brother->children[parent_direction];
	      grandparent->children[parent_direction] = brother->children[1 - parent_direction];
	      brother->children[parent_direction]     = parent_index;
	      brother->children[1 - parent_direction] = grandparent_index;
	      return brother_index;
	    }
	  }
	}
      }
    }
  }
  return grandparent_index;
}

static I64 insert(I64 node_index, String word, I64 offset) {
  if (node_count == 1) {
    I64 new_node = make_node(word, offset);
    nodes[new_node].is_black = true;
    return new_node;
  }
  Node* node       = &nodes[node_index];
  I32   comparison = compare(word, node->word);
  if (comparison < 0) {
    if (node->children[0] == 0) {
      node->children[0] = make_node(word, offset);
    } else {
      node->children[0] = insert(node->children[0], word, offset);
    }
  } else if (comparison > 0) {
    if (node->children[1] == 0) {
      node->children[1] = make_node(word, offset);
    } else {
      node->children[1] = insert(node->children[1], word, offset);
    }
  } else if (comparison == 0) {
    Offset* last      = &offsets[node->last_offset];
    last->next        = make_offset(offset);
    node->last_offset = last->next;
  }
  return balance(node_index);
}

static I64 lookup(I64 node_index, String word) {
  Node* node       = &nodes[node_index];
  I32   comparison = compare(word, node->word);
  if (comparison < 0) {
    return node->children[0] == 0 ? 0 : lookup(node->children[0], word);
  } else if (comparison > 0) {
    return node->children[0] == 0 ? 0 : lookup(node->children[1], word);
  } else {
    return node->first_offset;
  }
}

static void index(String logs) {  
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
	      String word               = slice(line, word_start, j);
	      // println(INFO "word=", word);
	      node_root                 = insert(node_root, word, line_start);
	      nodes[node_root].is_black = true;
	      // check_node(node_root);
	      // println(INFO "Inserting word=\"", word, "\" tree_depth=", check_node(node_root), " node_count=", node_count - 1, '.');
	      // print_tree(node_root, 0);
	    }
	    word_start = j + 1;
	  }
	  if (j < line.size && line[j] == '"') {
	    if (last_qoute == -1) {
	      last_qoute = j;
	    } else {
	      String qouted_word = slice(line, last_qoute + 1, j);
	      if (qouted_word.size > 0) {
		node_root                 = insert(node_root, qouted_word, line_start);
		nodes[node_root].is_black = true;
	      }
	      last_qoute = -1;
	    }
	  }
	}
      }
      line_start = i + 1;
    }
  }
  println(INFO "Built index with tree_depth=", check_node(node_root), " node_count=", node_count - 1, '.');
  flush();
}


static Arena query_arena;

static time_t parse_time(String input, const char* format) {
  struct tm time   = {};
  char*     result = strptime((char*) input.data, format, &time);
  return result == NULL ? -1 : mktime(&time);
}

static String query(String logs, Parameters parameters, I32 bins, I32* histogram) {
  const char* query_time_format = "%Y-%m-%dT%H:%M";

  // @Feature make this a parameter.
  // const char* log_time_format = "%Y/%m/%d %H:%M:%S";
  const char* log_time_format = "%Y-%m-%dT%H:%M:%S";
  
  time_t start_time = parse_time(parameters.start, query_time_format);
  time_t end_time   = parse_time(parameters.end, query_time_format);

  String result       = String(query_arena.memory, 0);
  I64    offset_index = lookup(node_root, parameters.query);
  I64    line_count   = 0;
  while (offset_index != 0) {
    Offset offset = offsets[offset_index];
    // println(INFO "offset=", offset.value);

    String line     = suffix(logs, offset.value);
    I64    line_end = find(line, '\n');
    line            = prefix(line, line_end + 1);

    time_t time = -1;
    for (I64 i = 0; time == -1 && i < line.size; i++) {
      // println("suffix=", suffix(line, i), " format=", log_time_format);
      time = parse_time(suffix(line, i), log_time_format);
    }
    assert(time != -1);
    assert(contains(line, parameters.query));
    
    if (start_time <= time && time <= end_time) {
      if (line_count < 256) {
	String query_result = allocate_bytes(&query_arena, line.size, 1);
	memcpy(query_result.data, line.data, line.size);
	result.size += line.size;
      }

      F32 value = (F32) (time - start_time) / (end_time - start_time);
      histogram[(I32) (bins * value)]++;      
    }

    offset_index = offset.next;
    line_count++;
  }
  query_arena.used = 0;
  return result;
}

I32 main(I32 argc, char** argv) {
  atexit(flush);

  if (argc != 2) {
    print(ERROR "Expected exactly one argument, the path to the log file.\n");
    exit(EXIT_FAILURE);
  }

  offset_arena = make_arena(1ll << 32);
  offsets      = (Offset*) offset_arena.memory;
  allocate<Offset>(&offset_arena);

  node_arena = make_arena(1ll << 32);
  nodes      = (Node*) node_arena.memory;
  allocate<Node>(&node_arena);

  query_arena = make_arena(1ll << 32);

  char*  logs_path = argv[1];
  String logs      = read_file(logs_path);
  println(INFO "Indexing ", logs_path, '.');
  flush();
  index(logs);
  
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

	  String filtered_logs = query(logs,parameters, bins, histogram);

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
