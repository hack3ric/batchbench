#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

#include "rdma_server.h"
#include "try.h"

int run() {
  // const char* ip = "172.23.45.67";
  struct sockaddr_in addr = {
    .sin_family = AF_INET,
    .sin_port = htons(22222),
    .sin_addr = {},
  };
  // inet_pton(AF_INET, ip, &addr.sin_addr);
  struct rdma_server* state =
    try_p(rdma_server_create((struct sockaddr*)&addr, 1 << 30), "failed to create server state");

  fprintf(stderr, "Waiting for connection from compute...\n");

  while (true) {
    rdma_server_handle(state);
  }

// disconnect:
  rdma_server_free(state);
  return 0;

error:
  rdma_server_free(state);
  return -errno;
}

int main() {
  while (true) {
    run();
    // sleep(1);
  }
  return 0;
}
