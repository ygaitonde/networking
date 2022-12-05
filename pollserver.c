#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define PORT "9034" // Port we're listening on

// Get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in *)sa)->sin_addr);
  }

  return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

// Return a listening socket
int get_listener_socket(void) {
  int listener = 1;
  int yes = 1;
  int rv;

  struct addrinfo hints, *ai, *p;

  // Get us a socket and bind int
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  if ((rv = getaddrinfo(NULL, PORT, &hints, &ai)) != 0) {
    fprintf(stderr, "selectserver: %s\n", gai_strerror(rv));
    exit(1);
  }

  for (p = ai; p != NULL; p = p->ai_next) {
    listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (listener < 0) {
      continue;
    }

    // lose the "address already in use" error message
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

    if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
      close(listener);
      continue;
    }

    break;
  }

  freeaddrinfo(ai);

  // if we get here we didn't bind to anything
  if (p == NULL) {
    return -1;
  }

  // Listen
  if (listen(listener, 10) == -1) {
    return -1;
  }

  return listener;
}

// add a file descriptor to the set
void add_to_pfds(struct pollfd *pfds[], int newfd, int *fd_count,
                 int *fd_size) {
  // If we don't have room, add more space to the pfds array
  if (*fd_count == *fd_size) {
    *fd_size *= 2;
    *pfds = realloc(*pfds, sizeof(**pfds) * (*fd_size));
  }

  (*pfds)[*fd_count].fd = newfd;
  (*pfds)[*fd_count].events = POLLIN;

  (*fd_count)++;
}

// Remove an index from the set
void del_from_pfds(struct pollfd pfds[], int i, int *fd_count) {
  // copy one from the end over this one
  pfds[i] = pfds[*fd_count - 1];

  (*fd_count)--;
}

// Main
int main(void) {
  int listener; // Listening socket descriptor

  int newfd;                          // newly accepted socket description
  struct sockaddr_storage remoteaddr; // client address
  socklen_t addrlen;

  char buf[256]; // buffer for client data

  char remoteIP[INET6_ADDRSTRLEN];

  // Start off with room for 5 connections
  // (we realloc as needed)
  int fd_count = 0;
  int fd_size = 5;
  struct pollfd *pfds = malloc(sizeof *pfds * fd_size);

  // Set up and get a listening socket
  listener = get_listener_socket();

  if (listener == -1) {
    fprintf(stderr, "error getting listening socket\n");
    exit(1);
  }

  // add the listener to set
  pfds[0].fd = listener;
  pfds[0].events = POLLIN; // Report ready to read on incoming connection

  fd_count = 1; // For the listener

  // Main loop
  for (;;) {
    int poll_count = poll(pfds, fd_count, -1);

    if (poll_count == -1) {
      perror("poll");
      exit(1);
    }

    // Run through existing connections looking for data to read
    for (int i = 0; i < fd_count; i++) {

      // Check if someone's ready to read
      if (pfds[i].revents & POLLIN) { // We got one
        if (pfds[i].fd == listener) {
          // If listener is ready to read, handle new connection

          addrlen = sizeof remoteaddr;
          newfd = accept(listener, (struct sockaddr *)&remoteaddr, &addrlen);

          if (newfd == -1) {
            perror("accept");
          } else {
            add_to_pfds(&pfds, newfd, &fd_count, &fd_size);

            printf("pollserver: new connection from %s on "
                   "socket %d\n",
                   inet_ntop(remoteaddr.ss_family,
                             get_in_addr((struct sockaddr *)&remoteaddr),
                             remoteIP, INET6_ADDRSTRLEN),
                   newfd);
          }
        } else {
          // If not the listener, we're just a regular client.
          int nbytes = recv(pfds[i].fd, buf, sizeof buf, 0);
          int sender_fd = pfds[i].fd;

          if (nbytes <= 0) {
            // error or connection closed
            if (nbytes == 0) {
              printf("pollserver: socket %d hung up \n", sender_fd);
            } else {
              perror("recv");
            }
            close(pfds[i].fd); // Bye!

            del_from_pfds(pfds, i, &fd_count);
          } else {
            // We got some good data from a client

            for (int j = 0; j < fd_count; j++) {
              // send to everyone!
              int dest_fd = pfds[j].fd;

              // except the listener and ourselves
              if (dest_fd != listener && dest_fd != sender_fd) {
                if (send(dest_fd, buf, nbytes, 0) == -1) {
                  perror("send");
                }
              }
            }
          }
        } // END handle data from client
      } // END got read-to-read from poll()
    } // END looping through fds
  } // End (for ;;)
  return 0;
}
