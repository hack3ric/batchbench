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
#include "try.h"

void* poll(void* args) {
  struct rdma_client* rdma = args;
  struct ibv_wc wc[1];
  int ret = 0;
  while ((ret = try2(ibv_poll_cq(rdma->conn->send_cq, 1, wc), "failed to poll completion queue"))) {
    if (ret != 0) printf("ret = %d\n", ret);
  };
  return NULL;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "not enough arguments\n");
    return 1;
  }
  int segment_count = strtol(argv[1], NULL, 0);
  // int chunk_size = strtol(argv[2], NULL, 0);
  // int signal_batched = strtol(argv[2], NULL, 0);
  int signal_batched = 0;
  printf("segment count = %d, signal_batched = %d\n", segment_count, signal_batched);

  const char* ip = "192.168.122.1";
  struct sockaddr_in addr = {
    .sin_family = AF_INET,
    .sin_port = htons(22222),
    .sin_addr = {},
  };
  inet_pton(AF_INET, ip, &addr.sin_addr);

  int buf_size = 4096;
  int work_count = 1024;
  int mem_size = buf_size * work_count;

  struct rdma_client* rdma =
    try_p(rdma_client_connect((struct sockaddr*)&addr, 10240), "failed to connect to RDMA server");

  void* local_buf = malloc(mem_size);
  struct ibv_mr* local_mem =
    try3_p(ibv_reg_mr(rdma->conn->pd, local_buf, mem_size,
                      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE),
           "failed to register memory region");

  double total = 0;

  // pthread_t poll_thread;
  // pthread_create(&poll_thread, NULL, poll, rdma);

  struct timespec tp1, tp2;

  try3(clock_gettime(CLOCK_REALTIME, &tp1), "failed to get 1st time");

  for (int i = 0; i < work_count; i++) {
    struct ibv_sge sg[segment_count];
    struct ibv_send_wr wr[segment_count];
    // struct ibv_sge* sg = try3_p(calloc(batch_size, sizeof(*sg)), "nooo!");
    // struct ibv_send_wr* wr = calloc(batch_size, sizeof(*wr));
    for (int j = 0; j < segment_count; j++) {
      sg[j] = (typeof(sg[j])){
        .addr = (uintptr_t)local_buf + j * (buf_size / segment_count) + i * buf_size,
        .length = (buf_size / segment_count),
        .lkey = local_mem->lkey,
      };
      wr[j] = (typeof(wr[j])){
        .sg_list = &sg[j],
        .num_sge = 1,
        .send_flags = signal_batched ? 0 : IBV_SEND_SIGNALED,
        .wr.rdma = {.remote_addr = rdma->mem.addr + (rand() % (1 << 29)), .rkey = rdma->mem.rkey},
      };
    }
    for (int j = 1; j < segment_count; j++) wr[j - 1].next = &wr[j];
    if (signal_batched && i >= work_count - 1) wr[segment_count - 1].send_flags = IBV_SEND_SIGNALED;

    // printf("i = %d\n", i);

    struct ibv_send_wr* bad;
    try3(ibv_post_send(rdma->conn->id->qp, &wr[0], &bad), "failed to post send");
    if (bad) {
      fprintf(stderr, "bad wr pointer non null\n");
      return 1;
    }

    // int wc_cnt = signal_batched ? 1 : batch_size;
    // struct ibv_wc wc[wc_cnt];
    // int cnt = 0;
    // while ((cnt += try3(ibv_poll_cq(rdma->conn->send_cq, wc_cnt - cnt, wc + cnt), "failed to poll completion queue"))
    // <
    //        wc_cnt) {
    //   // printf("cnt = %d\n", cnt);
    // };

    // double delta = (double)(tp2.tv_sec - tp1.tv_sec) * 1.0e9 + (double)(tp2.tv_nsec - tp1.tv_nsec);

    // for (int i = 0; i < wc_cnt; i++) {
    //   if (wc[i].status != IBV_WC_SUCCESS) {
    //     fprintf(stderr, "work completion error: %s %d\n", ibv_wc_status_str(wc[i].status), wc[i].status);
    //     return 1;
    //   }
    // }
    // printf("done reading! cnt = %d, time = %lfns\n", cnt, delta);
    // avg += delta;
  }

  try3(clock_gettime(CLOCK_REALTIME, &tp2), "failed to get 2nd time");

  total = (double)(tp2.tv_sec - tp1.tv_sec) * 1.0e9 + (double)(tp2.tv_nsec - tp1.tv_nsec);

  // avg /= work_count;
  printf("total = %lf, post_send throughput = %lf calls/sec\n", total, work_count / (total / 1.0e9));
  // struct ibv_wc wc[1];
  // while ((try3(ibv_poll_cq(rdma->conn->send_cq, 1, wc), "failed to poll completion queue")) == 0);
  // pthread_cancel(poll_thread);
  FILE* file = fopen("result.csv", "a");
  fprintf(file, "%d,%d,%lf\n", segment_count, signal_batched, total);

  return 0;

error:
  rdma_client_free(rdma);
  return -errno;
}
