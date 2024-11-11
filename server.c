#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "rdma_server.h"
#include "try.h"

struct server_state {
  void* mem;
  size_t size;
  struct rdma_server* rdma;
};

static void server_state_free(struct server_state* state) {
  if (state->rdma) rdma_server_free(state->rdma);
  if (state->mem) munmap(state->mem, state->size);
}

static struct server_state* server_state_create(struct sockaddr* addr, size_t mem_size) {
  struct server_state* state = calloc(1, sizeof(*state));
  state->mem = try3_p(mmap(NULL, mem_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0),
                      "failed to mmap");
  state->size = mem_size;
  state->rdma = try3_p(rdma_server_create(addr, state->mem, mem_size), "failed to create RDMA server");
  return state;
error:
  server_state_free(state);
  return NULL;
}

int run() {
  // const char* ip = "172.23.45.67";
  struct sockaddr_in addr = {
    .sin_family = AF_INET,
    .sin_port = htons(22222),
    .sin_addr = {},
  };
  // inet_pton(AF_INET, ip, &addr.sin_addr);
  struct server_state* state =
    try_p(server_state_create((struct sockaddr*)&addr, 1 << 30), "failed to create server state");

  // TODO: handle
  while (true) {
    struct rdma_cm_event* rdma_event;
    try(rdma_get_cm_event(state->rdma->events, &rdma_event), "failed to get RDMA event");
    switch (rdma_event->event) {
      case RDMA_CM_EVENT_DISCONNECTED:
        fprintf(stderr, "compute side disconnected, exiting\n");
        goto disconnect;
      default:
        fprintf(stderr, "received new RDMA event %s\n", rdma_event_str(rdma_event->event));
        break;
    }
    rdma_ack_cm_event(rdma_event);
  }

disconnect:
  server_state_free(state);
  return 0;

error:
  server_state_free(state);
  return -errno;
}

int main() {
  while (true) {
    run();
    // sleep(1);
  }
  return 0;
}
