#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "prelude.hpp"
#include "print.hpp"

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

static U8 buffer[4096];

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
	  String response =
	    "HTTP/1.1 404\r\n"
	    "Content-Length: 0\r\n"
	    "\r\n";
	  I64 bytes_written = write(connection_fd, response.data, response.size);
	  if (bytes_written == -1) {
	    println(ERROR "Failed to write to connection: ", get_error(), '.');
	  } else if (bytes_written < response.size) {
	    I64 got      = bytes_written;
	    I64 expected = response.size;
	    println(WARN "Only wrote ", got, '/', expected, "bytes to connection: ", get_error(), '.');
	  }
	  println(ERROR "Bad request.");
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
      if (close(connection_fd) == -1) {
	println(WARN "Failed to close socket: ", get_error(), '.');
      }
    }
    flush();
  }
}
