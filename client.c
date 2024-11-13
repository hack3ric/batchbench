#include <arpa/inet.h>
#include <bits/time.h>
#include <infiniband/verbs.h>
#include <pthread.h>
#include <rdma/rdma_verbs.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>

#include "rdma_client.h"
#include "rdma_conn.h"
#include "try.h"

struct thread_params {
  uint64_t* addrs;
  size_t addrs_count;
  uint32_t seg_size;
  uint32_t lkey;
  struct rdma_client* c;
};

void* thread_entry(void* args) {
  struct thread_params* p = args;
  if (p->addrs_count <= 0) return NULL;

  struct ibv_sge sg[p->addrs_count];
  struct ibv_send_wr wr[p->addrs_count];

  struct timespec t1, t2;

  // printf("using rdma client %p\n", p->c->conn->id->context);

  for (int i = 0; i < p->addrs_count; i++) {
    sg[i] = (typeof(sg[i])){
      .addr = p->addrs[i],
      .length = p->seg_size,
      .lkey = p->lkey,
    };
    wr[i] = (typeof(wr[i])){
      .sg_list = &sg[i],
      .num_sge = 1,
      .send_flags = IBV_SEND_SIGNALED,
      .wr.rdma = {.remote_addr = p->c->mem.addr + (rand() % (1 << 29)), .rkey = p->c->mem.rkey},
    };
  }
  for (int i = 1; i < p->addrs_count; i++) {
    wr[i - 1].next = &wr[i];
  }
  // wr[p->addrs_count - 1].send_flags = IBV_SEND_SIGNALED;

  struct ibv_send_wr* bad;
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t1);
  try2(ibv_post_send(p->c->conn->id->qp, wr, &bad), "failed to post send");
  if (bad) {
    fprintf(stderr, "bad wr pointer non null\n");
    return NULL;
  }
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t2);

  int wc_cnt = p->addrs_count;
  struct ibv_wc wc[wc_cnt];
  int cnt = 0;
  while ((cnt += try2(ibv_poll_cq(p->c->conn->send_cq, wc_cnt - cnt, wc + cnt), "failed to poll cq")) < wc_cnt);

  for (int i = 0; i < wc_cnt; i++) {
    if (wc[i].status != IBV_WC_SUCCESS) {
      fprintf(stderr, "work completion error: %s %d\n", ibv_wc_status_str(wc[i].status), wc[i].status);
      return NULL;
    }
  }

  double* delta = calloc(1, sizeof(double));
  *delta = (double)(t2.tv_sec - t1.tv_sec) * 1.0e9 + (double)(t2.tv_nsec - t1.tv_nsec);
  // printf("delta = %lf\n", *delta);
  return delta;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "not enough arguments\n");
    return 1;
  }
  int seg_count = strtol(argv[1], NULL, 0);
  int thread_count = strtol(argv[2], NULL, 0);
  printf("segment count = %d, thread count = %d\n", seg_count, thread_count);

  const char* ip = "192.168.122.1";
  struct sockaddr_in addr = {
    .sin_family = AF_INET,
    .sin_port = htons(22222),
    .sin_addr = {},
  };
  inet_pton(AF_INET, ip, &addr.sin_addr);

  int buf_size = 4096;
  int seg_size = buf_size / seg_count;
  int work_count = 102400;

  double avg = 0;

  // TODO: multiple clients and mr for each thread

  // void* buf = malloc(buf_size);
  struct rdma_client* clients[thread_count];
  void* bufs[thread_count];
  struct ibv_mr* mrs[thread_count];
  for (int i = 0; i < thread_count; i++) {
    clients[i] = try_p(rdma_client_connect((struct sockaddr*)&addr, seg_count), "failed to connect to RDMA server");
    bufs[i] = malloc(buf_size);
    mrs[i] = try_p(ibv_reg_mr(clients[i]->conn->pd, bufs[i], buf_size,
                              IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE),
                   "failed to register memory region");
    // printf("i = %d\n", i);
  }

  // struct rdma_client* rdma =
  //   try_p(rdma_client_connect((struct sockaddr*)&addr, seg_count), "failed to connect to RDMA server");

  // void* local_buf = malloc(buf_size);
  // struct ibv_mr* local_mem =
  //   try3_p(ibv_reg_mr(rdma->conn->pd, local_buf, buf_size,
  //                     IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE),
  //          "failed to register memory region");

  int a = seg_count / thread_count;
  for (int i = 0; i < work_count; i++) {
    pthread_t threads[thread_count];
    struct thread_params params[thread_count];

    for (int n = 0; n < thread_count; n++) {
      int b = (n + 1) > (seg_count % thread_count) ? 0 : 1;
      int total_work_count = a + b;
      // printf("twc = %d\n", total_work_count);
      uint64_t* addrs = calloc(total_work_count, sizeof(uint64_t));
      for (int j = 0; j < total_work_count; j++) {
        addrs[j] = (uintptr_t)bufs[n] + n * seg_size + j * thread_count * seg_size;
        // printf("addrs[%d] = %lu\n", j, addrs[j]);
      }
      params[n] = (typeof(params[n])){
        .addrs = addrs,
        .addrs_count = total_work_count,
        .lkey = mrs[n]->lkey,
        .seg_size = seg_size,
        .c = clients[n],
      };
    }
    for (int n = 0; n < thread_count; n++) {
      try3(pthread_create(&threads[n], NULL, thread_entry, &params[n]), "failed to spawn thread");
    }
    for (int n = 0; n < thread_count; n++) {
      double* delta;
      pthread_join(threads[n], (void**)&delta);
      if (!delta) continue;
      // printf("delta = %lf\n", *delta);
      avg += *delta;
    }
  }

  avg /= work_count;
  printf("avg = %lf\n", avg);
  // struct ibv_wc wc[1];
  // while ((try3(ibv_poll_cq(rdma->conn->send_cq, 1, wc), "failed to poll completion queue")) != 0) printf("!\n");
  FILE* file = fopen("result.csv", "a");
  fprintf(file, "%d,%d,%lf\n", seg_count, thread_count, avg);

  return 0;

error:
  // rdma_client_free(rdma);
  return -errno;
}
