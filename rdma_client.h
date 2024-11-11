#ifndef RDMA_CLIENT_H
#define RDMA_CLIENT_H

#include <rdma/rdma_cma.h>

#include "rdma_conn.h"

struct rdma_client {
  struct rdma_event_channel* rdma_events;
  struct rdma_conn* conn;
  struct memory_info mem;
};

struct rdma_client* rdma_client_connect(struct sockaddr* addr, int batch_size);
void rdma_client_free(struct rdma_client* client);

#endif  // RDMA_CLIENT_H
