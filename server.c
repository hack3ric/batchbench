#include <arpa/inet.h>
#include <bits/pthreadtypes.h>
#include <infiniband/verbs.h>
#include <netinet/in.h>
#include <pthread.h>
#include <rdma/rdma_verbs.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "rdma_conn.h"
#include "rdma_server.h"
#include "try.h"

#define MAX_POLL 16

struct server_state {
  void* mem;
  size_t size;
  struct rdma_server* rdma;
};

static void server_state_free(struct server_state* state) {
  if (state->rdma) rdma_server_free(state->rdma);
  if (state->mem) munmap(state->mem, state->size);
}

void* poll(void* args) {
  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
  struct rdma_server* s = args;
  struct ibv_wc wc;
  int c = 0;
  while (true) {
    int count = try2(ibv_poll_cq(s->conn->recv_cq, 1, &wc), "failed to poll recv cq");
    if (count == 0) {
      // printf("no\n");
      continue;
    }

    if (wc.status != IBV_WC_SUCCESS) {
      fprintf(stderr, "work completion error: %s %d\n", ibv_wc_status_str(wc.status), wc.status);
      return NULL;
    }

    // printf("process %d, count = %ld\n", c++, s->recv_buf->count);
    struct ibv_sge* sgs = calloc(s->recv_buf->count, sizeof(*sgs));
    for (int i = 0; i < s->recv_buf->count; i++) {
      // printf("addr = %lu, len = %lu\n", s->recv_buf->segs[i].addr, s->recv_buf->segs[i].len);
      sgs[i] =
        (typeof(sgs[0])){.addr = s->recv_buf->segs[i].addr, .length = s->recv_buf->segs[i].len, .lkey = s->mr->lkey};
    }

    // printf("qp num = %d\n", s->conn->id->qp->qp_num);

    try2(rdma_post_recv(s->conn->id, NULL, s->recv_buf, sizeof(struct message), s->recv_mr), "failed to RDMA recv");

    try2(rdma_post_sendv(s->conn->id, NULL, sgs, s->recv_buf->count, IBV_SEND_SIGNALED), "failed to post send 2");
    struct ibv_wc wc2;
    while (try2(ibv_poll_cq(s->conn->send_cq, 1, &wc2), "failed to poll recv cq") == 0);
    if (wc2.status != IBV_WC_SUCCESS) {
      fprintf(stderr, "work completion error 2: %s %d\n", ibv_wc_status_str(wc2.status), wc2.status);
      return NULL;
    }
    // printf("done\n");
  }
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

  int epfd = try(epoll_create1(0), "failed to create epoll fd");
  struct epoll_event ev, events[4];

  int rdma_events_fd = state->rdma->events->fd;
  ev = (typeof(ev)){.events = EPOLLIN | EPOLLET, .data.fd = rdma_events_fd};
  try(epoll_ctl(epfd, EPOLL_CTL_ADD, rdma_events_fd, &ev), "failed to add fd to epoll");

  // int ccfd = state->rdma->conn->cc->fd;
  // ev = (typeof(ev)){.events = EPOLLIN | EPOLLET, .data.fd = ccfd};
  // try(epoll_ctl(epfd, EPOLL_CTL_ADD, ccfd, &ev), "failed to add fd to epoll");
  pthread_t poll_thread = 0;
  try3(pthread_create(&poll_thread, NULL, poll, state->rdma), "failed to create pthread");

  while (true) {
    int nfds = try(epoll_wait(epfd, events, 4, -1), "failed to epoll");

    for (int i = 0; i < nfds; i++) {
      if (events[i].data.fd == rdma_events_fd) {
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

        // } else if (events[i].data.fd == ccfd) {
        //   printf("process\n");
        //   struct rdma_conn* c = state->rdma->conn;
        //   struct ibv_wc wcs[MAX_POLL];
        //   int polled = try(rdma_conn_poll_ev(c, wcs, MAX_POLL), "failed to poll");

        //   bool errored = false;
        //   for (int i = 0; i < polled; i++) {
        //     if (wcs[i].status != IBV_WC_SUCCESS) {
        //       fprintf(stderr, "recv completion queue received error: %s\n", ibv_wc_status_str(wcs[i].status));
        //       errored = true;
        //     }

        //     // TODO: process

        //     if (!errored) {
        //       // refill recv queue
        //       // currently we only have 1 slot of recv buffer; will use wr_id to indicate which slot
        //       // to refill.
        //       try(rdma_post_recv(c->id, NULL, state->rdma->recv_buf, sizeof(struct message), state->rdma->recv_mr),
        //           "failed to RDMA recv");
        //     }
        //   }
        //   if (errored) return -1;
      }
    }
  }

disconnect:
  if (poll_thread) pthread_cancel(poll_thread);
  pthread_join(poll_thread, NULL);
  server_state_free(state);
  return 0;

error:
  if (poll_thread) pthread_cancel(poll_thread);
  pthread_join(poll_thread, NULL);
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
