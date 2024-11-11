#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#include <stdio.h>
#include <stdlib.h>

#include "rdma_client.h"
#include "rdma_conn.h"
#include "try.h"

#define RESOLVE_TIMEOUT_MS 500

struct rdma_client* rdma_client_connect(struct sockaddr* addr, int batch_size) {
  struct rdma_client* client = try2_p(calloc(1, sizeof(*client)));
  struct rdma_cm_id *id = NULL, *id2;

  client->rdma_events = try3_p(rdma_create_event_channel(), "failed to create RDMA event channel");
  try3(rdma_create_id(client->rdma_events, &id, NULL, RDMA_PS_TCP), "failed to create RDMA ID");

  // Resolve remote address and route
  try3(rdma_resolve_addr(id, NULL, addr, RESOLVE_TIMEOUT_MS), "failed to resolve address");
  try3(expect_event(client->rdma_events, RDMA_CM_EVENT_ADDR_RESOLVED));
  try3(rdma_resolve_route(id, RESOLVE_TIMEOUT_MS), "failed to resolve route");
  try3(expect_event(client->rdma_events, RDMA_CM_EVENT_ROUTE_RESOLVED));

  // prevent double-free using macros
  id2 = id;
  id = NULL;
  client->conn = try3_p(rdma_conn_create(id2, false, batch_size), "failed to create RDMA connection");

  struct rdma_conn_param memory_param = {};
  try3(rdma_connect(client->conn->id, NULL), "failed to connect");
  try3(expect_established(client->rdma_events, &memory_param));

  struct memory_info* mem = (typeof(mem))memory_param.private_data;
  if (mem) {
    client->mem = *mem;
  } else {
    fprintf(stderr, "memory server memory info is null!\n");
    goto error;
  }

  return client;

error:
  if (id) rdma_destroy_id(id);
  rdma_client_free(client);
  return NULL;
}

void rdma_client_free(struct rdma_client* client) {
  if (client->conn) rdma_conn_free(client->conn);
  if (client->rdma_events) rdma_destroy_event_channel(client->rdma_events);
  free(client);
}
