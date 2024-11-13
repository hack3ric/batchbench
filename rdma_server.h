#ifndef RDMA_SERVER_H
#define RDMA_SERVER_H

#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>

#include "rdma_conn.h"

struct rdma_server {
  struct rdma_event_channel* events;
  struct rdma_cm_id* listen_id;
  struct rdma_conn* conn;
  struct ibv_mr* mr;

  struct message* recv_buf;
  struct ibv_mr* recv_mr;
};

struct rdma_server* rdma_server_create(struct sockaddr* addr, void* mem, size_t mem_size);
void rdma_server_free(struct rdma_server* server);

#endif  // RDMA_SERVER_H
