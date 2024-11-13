#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "rdma_conn.h"
#include "rdma_server.h"
#include "try.h"

struct rdma_server* rdma_server_create(struct sockaddr* addr, size_t mem_size) {
  struct rdma_server* s = try2_p(calloc(1, sizeof(*s)));
  s->events = try3_p(rdma_create_event_channel(), "failed to create RDMA event channel");
  try3(rdma_create_id(s->events, &s->listen_id, NULL, RDMA_PS_TCP), "failed to create listen ID");

  // Bind and listen for client to connect
  try3(rdma_bind_addr(s->listen_id, addr), "failed to bind address");
  try3(rdma_listen(s->listen_id, 8), "failed to listen");

  s->mem = try3_p(mmap(NULL, mem_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0),
                  "failed to mmap");
  s->mem_size = mem_size;
  return s;

error:
  rdma_server_free(s);
  return NULL;
}

void rdma_server_free(struct rdma_server* s) {
  if (s->conns) {
    for (int i = 0; i < s->conn_count; i++) rdma_conn_free(s->conns[i]);
    free(s->conns);
  }
  if (s->mrs) {
    for (int i = 0; i < s->conn_count; i++) ibv_dereg_mr(s->mrs[i]);
    free(s->mrs);
  }
  if (s->mem) munmap(s->mem, s->mem_size);
  if (s->listen_id) rdma_destroy_id(s->listen_id);
  if (s->events) rdma_destroy_event_channel(s->events);
}

int rdma_server_handle(struct rdma_server* s) {
  struct rdma_cm_event* ev;
  try(rdma_get_cm_event(s->events, &ev), "failed to get RDMA event");
  switch (ev->event) {
    case RDMA_CM_EVENT_CONNECT_REQUEST:;
      const unsigned int MR_FLAGS = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
      struct rdma_conn* new_conn = NULL;
      struct ibv_mr* new_mr = NULL;
      new_conn = try3_p(rdma_conn_create(ev->id, true, 10), "failed to create connection");
      new_mr = try3_p(ibv_reg_mr(new_conn->pd, s->mem, s->mem_size, MR_FLAGS), "cannot register MR");

      // Accept parameters that compute side had proposed, plus memory-side handshake data (MR info,
      // etc.)
      struct memory_info hs = {
        .addr = (uintptr_t)new_mr->addr,
        .size = s->mem_size,
        .rkey = new_mr->rkey,
      };
      ev->param.conn.private_data = &hs;
      ev->param.conn.private_data_len = sizeof(hs);

      try3(rdma_accept(new_conn->id, &ev->param.conn), "failed to accept");
      try3(expect_established(s->events, NULL));

      s->conns = reallocarray(s->conns, s->conn_count + 1, sizeof(void*));
      s->mrs = reallocarray(s->mrs, s->conn_count + 1, sizeof(void*));
      s->conns[s->conn_count] = new_conn;
      s->mrs[s->conn_count] = new_mr;
      s->conn_count++;
      s->conn_remaining++;
      break;

    error:
      if (new_conn) rdma_conn_free(new_conn);
      if (new_mr) ibv_dereg_mr(new_mr);
      break;

    case RDMA_CM_EVENT_DISCONNECTED:
      fprintf(stderr, "compute side disconnected\n");
      s->conn_remaining--;
      if (s->conn_remaining == 0) {
        fprintf(stderr, "no conn remaining, cleaning\n");
        rdma_ack_cm_event(ev);
        for (int i = 0; i < s->conn_count; i++) rdma_conn_free(s->conns[i]);
        free(s->conns);
        for (int i = 0; i < s->conn_count; i++) ibv_dereg_mr(s->mrs[i]);
        free(s->mrs);
        s->mrs = NULL;
        s->conns = NULL;
        s->conn_count = 0;
        fprintf(stderr, "done\n");
        return 0;
      }
      break;
    default:
      fprintf(stderr, "received new RDMA event %s\n", rdma_event_str(ev->event));
      break;
  }
  rdma_ack_cm_event(ev);
  return 0;
}
