#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#include <stdio.h>
#include <stdlib.h>

#include "rdma_conn.h"
#include "rdma_server.h"
#include "try.h"

void rdma_server_free(struct rdma_server* s) {
  if (s->mr) ibv_dereg_mr(s->mr);
  if (s->listen_id) rdma_destroy_id(s->listen_id);
  if (s->events) rdma_destroy_event_channel(s->events);
  if (s->conn) rdma_conn_free(s->conn);
}

// TODO: pass MR information to compute side
struct rdma_server* rdma_server_create(struct sockaddr* addr, void* mem, size_t mem_size) {
  struct rdma_server* s = try2_p(calloc(1, sizeof(*s)));
  s->events = try3_p(rdma_create_event_channel(), "failed to create RDMA event channel");
  try3(rdma_create_id(s->events, &s->listen_id, NULL, RDMA_PS_TCP), "failed to create listen ID");

  // Bind and listen for client to connect
  try3(rdma_bind_addr(s->listen_id, addr), "failed to bind address");
  fprintf(stderr, "Waiting for connection from compute...\n");
  try3(rdma_listen(s->listen_id, 8), "failed to listen");

  // Get CM ID for the connection
  struct rdma_cm_id* conn_id = NULL;
  struct rdma_conn_param param = {};
  try3(expect_connect_request(s->events, &conn_id, &param), "failed");
  s->conn = try3_p(rdma_conn_create(conn_id, true, 10), "failed to create connection");

  const unsigned int MR_FLAGS = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
  s->mr = try3_p(ibv_reg_mr(s->conn->pd, mem, mem_size, MR_FLAGS), "cannot register memory region");

  // Accept parameters that compute side had proposed, plus memory-side handshake data (MR info,
  // etc.)
  struct memory_info hs = {
    .addr = (uintptr_t)s->mr->addr,
    .size = mem_size,
    .rkey = s->mr->rkey,
  };
  param.private_data = &hs;
  param.private_data_len = sizeof(hs);

  try3(rdma_accept(s->conn->id, &param), "failed to accept");
  try3(expect_established(s->events, NULL));

  return s;

error:
  rdma_server_free(s);
  return NULL;
}
