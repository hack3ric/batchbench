#include <rdma/rdma_cma.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "rdma_conn.h"
#include "try.h"

struct rdma_conn* rdma_conn_create(struct rdma_cm_id* id, bool use_event, int batch_size) {
  struct rdma_conn* c = calloc(1, sizeof(*c));
  c->id = id;
  c->pd = try3_p(ibv_alloc_pd(c->id->verbs), "cannot allocate protection domain");

  int cq_size = batch_size + 10;

  c->send_cq = try3_p(ibv_create_cq(id->verbs, cq_size, NULL, NULL, 0), "cannot create completion queue");
  if (use_event) {
    c->cc = try3_p(ibv_create_comp_channel(id->verbs), "cannot create completion channel");
    c->recv_cq = try3_p(ibv_create_cq(id->verbs, cq_size, NULL, c->cc, 0), "cannot create completion queue");
    try3(ibv_req_notify_cq(c->recv_cq, false), "cannot arm completion channel");
  } else {
    c->recv_cq = try3_p(ibv_create_cq(id->verbs, cq_size, NULL, NULL, 0), "cannot create completion queue");
  }

  // Create queue pair inside CM ID
  struct ibv_qp_init_attr attr = {
    .qp_type = IBV_QPT_RC,
    .send_cq = c->send_cq,
    .recv_cq = c->recv_cq,
    .cap = {.max_send_wr = cq_size, .max_recv_wr = 4, .max_send_sge = 1, .max_recv_sge = 1},
  };
  try3(rdma_create_qp(id, c->pd, &attr), "cannot create queue pair");

  return c;

error:
  rdma_conn_free(c);
  return NULL;
}

void rdma_conn_free(struct rdma_conn* conn) {
  if (!conn) return;
  if (conn->id) {
    rdma_destroy_qp(conn->id);
    rdma_destroy_id(conn->id);
  }
  if (conn->send_cq) ibv_destroy_cq(conn->send_cq);
  if (conn->recv_cq) ibv_destroy_cq(conn->recv_cq);
  if (conn->cc) ibv_destroy_comp_channel(conn->cc);
  if (conn->pd) ibv_dealloc_pd(conn->pd);
  free(conn);
}

// Wait for a certain type of CM event, regarding others as error.
static inline int _expect_event(struct rdma_event_channel* events, enum rdma_cm_event_type type,
                                struct rdma_cm_id** conn_id, struct rdma_conn_param* param) {
  struct rdma_cm_event* ev;
  try(rdma_get_cm_event(events, &ev), "cannot get CM event");
  if (ev->event != type) {
    fprintf(stderr, "expected %s, got %s\n", rdma_event_str(type), rdma_event_str(ev->event));
    rdma_ack_cm_event(ev);
    return -1;
  }
  // fprintf(stderr, "ayy!! %s\n", rdma_event_str(type));
  if (conn_id) *conn_id = ev->id;
  if (param) *param = ev->param.conn;
  rdma_ack_cm_event(ev);
  return 0;
}

int expect_event(struct rdma_event_channel* events, enum rdma_cm_event_type type) {
  return _expect_event(events, type, NULL, NULL);
}

int expect_connect_request(struct rdma_event_channel* events, struct rdma_cm_id** conn_id,
                           struct rdma_conn_param* param) {
  return _expect_event(events, RDMA_CM_EVENT_CONNECT_REQUEST, conn_id, param);
}

int expect_established(struct rdma_event_channel* events, struct rdma_conn_param* param) {
  return _expect_event(events, RDMA_CM_EVENT_ESTABLISHED, NULL, param);
}
