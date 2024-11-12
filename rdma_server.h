#ifndef RDMA_SERVER_H
#define RDMA_SERVER_H

#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>

struct rdma_server {
  struct rdma_event_channel* events;
  struct rdma_cm_id* listen_id;

  void* mem;
  size_t mem_size;
  struct rdma_conn** conns;
  struct ibv_mr** mrs;
  size_t conn_count;
  size_t conn_remaining;
};

struct rdma_server* rdma_server_create(struct sockaddr* addr, size_t mem_size);
void rdma_server_free(struct rdma_server* server);

int rdma_server_handle(struct rdma_server* s);

#endif  // RDMA_SERVER_H
