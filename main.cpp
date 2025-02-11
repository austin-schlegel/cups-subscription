#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cctype>
#include <cstring>
#include <iostream>
#include <string>

void printBuffer(const unsigned char *buffer, const size_t bytes)
{
  std::cout << "bytes: " << bytes << '\n';
  for (int i = 0; i < bytes; ++i) {
    if (std::isprint(buffer[i])) {
      std::cout << buffer[i];
    }
    else if (buffer[i] == '\r') {
      std::cout << "\\r";
    }
    else if (buffer[i] == '\n') {
      std::cout << "\\n" << buffer[i];
    }
    else {
      std::cout << '\'' << static_cast<int>(buffer[i]) << '\'';
    }
  }
  std::cout.flush();
}

void sendData(const std::string& answer, const int fd)
{
  const ssize_t res = send(fd, answer.c_str(), answer.size(), 0);
  if (res >= 0) {
    std::cout << "send size = " << res << '\n' << answer;
    std::cout.flush();
  }
  else {
    perror("send");
  }
}

int main()
{
  /***** server *****/
  struct addrinfo hints;
  struct addrinfo *result, *rp;
  int listen_socket_fd;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_flags = AI_PASSIVE;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = 0;
  hints.ai_addr = nullptr;
  hints.ai_canonname = nullptr;
  hints.ai_next = nullptr;

  {
    std::string service = std::to_string(9884);
    int gai_return_code = getaddrinfo(nullptr, service.c_str(), &hints, &result);
    if (gai_return_code != 0) {
      std::cerr << "getaddrinfo: " << gai_strerror(gai_return_code) << std::endl;
      return EXIT_FAILURE;
    }
  }

  for (rp = result; rp != nullptr; rp = rp->ai_next) {
    listen_socket_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (listen_socket_fd == -1) {
      perror("socket");
      continue;
    }

    int optval = 1;
    if (setsockopt(listen_socket_fd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval)) == -1) {
      perror("setsockopt");
      return EXIT_FAILURE;
    }

    if (bind(listen_socket_fd, rp->ai_addr, rp->ai_addrlen) == -1) {
      perror("bind");
      close(listen_socket_fd);
      continue;
    }
    else {
      std::cout << "Success bind!" << std::endl;
      break;
    }
  }

  if (rp == nullptr) {
    std::cerr << "Could not bind!" << std::endl;
    return EXIT_FAILURE;
  }

  freeaddrinfo(result);

  if (listen(listen_socket_fd, SOMAXCONN) == -1) {
    perror("listen");
    return EXIT_FAILURE;
  }
  /***** server *****/

  /***** epoll *****/
  int epoll_fd = epoll_create1(0);
  if (epoll_fd == -1) {
    perror("epoll_create1");
    return EXIT_FAILURE;
  }

  struct epoll_event server_event;
  server_event.events = EPOLLIN;
  server_event.data.fd = listen_socket_fd;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_socket_fd, &server_event) == -1) {
    perror("epoll_ctl");
    return EXIT_FAILURE;
  }

  struct epoll_event stdin_event;
  stdin_event.events = EPOLLIN;
  stdin_event.data.fd = STDIN_FILENO;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, STDIN_FILENO, &stdin_event) == -1) {
    perror("epoll_ctl");
    return EXIT_FAILURE;
  }

  const int maxEvents = 10;
  struct epoll_event events[maxEvents];
  bool is_work = true;

  while (is_work) {
    int nfds = epoll_wait(epoll_fd, events, maxEvents, -1);
    if (nfds == -1) {
      perror("epoll_wait");
      return EXIT_FAILURE;
    }
    else {
      std::cout << "nfds: " << nfds << std::endl;
    }

    for (int fd_idx = 0; fd_idx < nfds; ++fd_idx) {
      if (events[fd_idx].data.fd == listen_socket_fd) {
        struct sockaddr client_address;
        socklen_t client_address_len = sizeof(struct sockaddr);
        int client_socket_fd = accept(listen_socket_fd, &client_address, &client_address_len);
        if (client_socket_fd == -1) {
          perror("accept");
          return EXIT_FAILURE;
        }
        else {
          std::cout << "Connection established\n{\n";
          char hbuf[NI_MAXHOST];
          char sbuf[NI_MAXSERV];
          int gni_error_code = getnameinfo(&client_address,
                                           client_address_len,
                                           hbuf,
                                           sizeof(hbuf),
                                           sbuf,
                                           sizeof(sbuf),
                                           NI_NUMERICHOST | NI_NUMERICSERV);
          if (gni_error_code == 0) {
            std::cout << "host: " << hbuf << '\n';
            std::cout << "serv: " << sbuf << '\n';
          }
          else {
            std::cout << "Could not resolve hostname!\n";
          }
          std::cout << '}' << std::endl;
        }

        struct epoll_event client_event;
        client_event.events = EPOLLIN;
        client_event.data.fd = client_socket_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket_fd, &client_event) == -1) {
          perror("epoll_ctl");
          return EXIT_FAILURE;
        }
      }
      else if (events[fd_idx].data.fd == STDIN_FILENO) {
        char buffer[128];
        ssize_t bytes = read(events[fd_idx].data.fd, buffer, sizeof(buffer));
        if (bytes > 0) {
          if (std::isspace(buffer[bytes - 1])) {
            --bytes;
          }
          std::string msg(buffer, bytes);
          if (msg == "quit") {
            is_work = false;
          }
        }
      }
      else {
        unsigned char buffer[2048];
        ssize_t bytes = recv(events[fd_idx].data.fd, buffer, sizeof(buffer), 0);
        if (bytes > 0) {
          printBuffer(buffer, bytes);
          std::string req(reinterpret_cast<char*>(buffer), bytes);

          // GET request from CUPS
          // Or end of Transfer-Encoding
          if (req.find("GET") != std::string::npos || req.find("0\r\n\r\n") != std::string::npos) {
            std::string answer = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
            sendData(answer, events[fd_idx].data.fd);
          }
          // PUT request from CUPS with 100 Expect
          else if (req.find("100-continue") != std::string::npos) {
            std::string answer = "HTTP/1.1 100 Continue\r\nContent-Length: 0\r\n\r\n";
            sendData(answer, events[fd_idx].data.fd);
          }
        }
        else {
          if (bytes == 0) std::cout << "Close connection!" << std::endl;
          else if (bytes == -1) perror("recv");

          if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[fd_idx].data.fd, nullptr) == -1) {
            perror("epoll_ctl");
          }
          close(events[fd_idx].data.fd);
        }
      }
    }
  }
  close(listen_socket_fd);
  close(epoll_fd);
  std::cout << "Close server!" << std::endl;
  /***** epoll *****/

  return EXIT_SUCCESS;
}
