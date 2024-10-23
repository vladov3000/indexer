#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#include "prelude.hpp"
#include "print.hpp"

#define RESPONSE_400 "HTTP/1.1 400\r\nContent-Length: 0\r\n\r\n"
#define RESPONSE_404 "HTTP/1.1 404\r\nContent-Length: 0\r\n\r\n"

static String logs = R"END(
2024/10/21 19:46:41 INFO Listening port=8080
2024/10/21 19:46:49 INFO New signUp request requestId=6961230c-bc7c-4636-a88f-f1f42a9a631d
2024/10/21 19:46:49 ERROR Password mismatch requestId=6961230c-bc7c-4636-a88f-f1f42a9a631d error="crypto/bcrypt: hashedPassword is not the hash of the given password"
2024/10/21 19:46:50 INFO New signUp request requestId=cfc3a428-9bc3-42b4-878f-cf9aa5ab95cc
2024/10/21 19:46:50 ERROR Failed to insert user with unique username requestId=cfc3a428-9bc3-42b4-878f-cf9aa5ab95cc error="ERROR: duplicate key value violates unique constraint \"users_username_key\" (SQLSTATE 23505)"
2024/10/21 19:46:52 INFO New signUp request requestId=50c9655a-1a50-4480-84df-02d098531620
2024/10/21 19:46:52 ERROR Password mismatch requestId=50c9655a-1a50-4480-84df-02d098531620 error="crypto/bcrypt: hashedPassword is not the hash of the given password"
2024/10/21 19:46:53 INFO New signUp request requestId=f457dd4c-207b-4db3-8c52-afbfd97e636f
2024/10/21 19:46:53 ERROR Failed to insert user with unique username requestId=f457dd4c-207b-4db3-8c52-afbfd97e636f error="ERROR: duplicate key value violates unique constraint \"users_username_key\" (SQLSTATE 23505)"
2024/10/21 19:46:54 INFO New signUp request requestId=a6977bb0-feb9-46f5-b301-3ad8041a7b5d
2024/10/21 19:46:54 ERROR Password mismatch requestId=a6977bb0-feb9-46f5-b301-3ad8041a7b5d error="crypto/bcrypt: hashedPassword is not the hash of the given password"
2024/10/21 19:46:59 INFO New signUp request requestId=f01dcfee-1d3e-4fba-b9f1-ace395a7791b
2024/10/21 19:47:18 INFO New signUp request requestId=a825a67c-8e7d-428d-bbd0-570c69ea6343
2024/10/21 19:47:18 ERROR Failed to find user requestId=a825a67c-8e7d-428d-bbd0-570c69ea6343
2024/10/21 19:47:19 INFO New signUp request requestId=702d3c59-6166-451f-abd6-d93e02bd72e6
2024/10/21 19:47:19 ERROR Failed to find user requestId=702d3c59-6166-451f-abd6-d93e02bd72e6
2024/10/21 19:47:20 INFO New signUp request requestId=0cd5b6c2-3f6d-4dcc-a163-4e19b817a73a
2024/10/21 19:47:20 ERROR Failed to find user requestId=0cd5b6c2-3f6d-4dcc-a163-4e19b817a73a
  )END";


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

U8 query_buffer[4096];

static time_t parse_time(String input, const char* format) {
  struct tm time = {};
  strptime((char*) input.data, format, &time);
  return mktime(&time);
}

static String query(Parameters parameters, I32 bins, I32* histogram) {
  const char* query_time_format = "%Y-%m-%dT%H:%M";
  const char* log_time_format   = "%Y/%m/%d %H:%M:%S";
  
  time_t start_time = parse_time(parameters.start, query_time_format);
  time_t end_time   = parse_time(parameters.end, query_time_format);
  
  String result     = String(query_buffer, 0);
  I64    line_start = 0;
  for (I64 i = 0; i <= logs.size; i++) {
    if (i == logs.size || logs[i] == '\n') {
      if (line_start != i) {
	String line = slice(logs, line_start, i + 1);
	time_t time = parse_time(line, log_time_format);
	if (start_time <= time && time <= end_time && contains(line, parameters.query)) {
	  if (line.size > sizeof(query_buffer) - result.size) {
	    break;
	  }
	  memcpy(&result.data[result.size], line.data, line.size);
	  result.size += line.size;

	  F32 value = (F32) (time - start_time) / (end_time - start_time);
	  histogram[(I32) (bins * value)]++;
	}
      }
      line_start = i + 1;
    }
  }
  return result;
}

int main() {
  atexit(flush);

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

	  String filtered_logs = query(parameters, bins, histogram);

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
