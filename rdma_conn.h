#ifndef RDMA_CONN_H
#define RDMA_CONN_H

#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#include <stdbool.h>

// RDMA connection, usually resides in a server or client instance.
struct rdma_conn {
  struct rdma_cm_id* id;  // RDMA communication manager ID, like socket
  struct ibv_pd* pd;      // protection domain

  struct ibv_comp_channel* cc;       // receive completion channel, for event notification
  struct ibv_cq *send_cq, *recv_cq;  // completion queues
};

// Create RDMA connection from connection manager.
struct rdma_conn* rdma_conn_create(struct rdma_cm_id* id, bool use_event);

// Frees the RDMA connection.
void rdma_conn_free(struct rdma_conn* conn);

int expect_event(struct rdma_event_channel* events, enum rdma_cm_event_type type);
int expect_connect_request(struct rdma_event_channel* events, struct rdma_cm_id** conn_id,
                           struct rdma_conn_param* param);
int expect_established(struct rdma_event_channel* events, struct rdma_conn_param* param);

struct memory_info {
  uintptr_t addr;
  size_t size;
  uint32_t rkey;
};

#endif  // RDMA_CONN_H
