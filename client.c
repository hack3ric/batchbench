#include <arpa/inet.h>
#include <bits/time.h>
#include <infiniband/verbs.h>
#include <rdma/rdma_verbs.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>

#include "rdma_client.h"
#include "try.h"

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "not enough arguments\n");
    return 1;
  }
  int batch_size = strtol(argv[1], NULL, 0);
  // int chunk_size = strtol(argv[2], NULL, 0);
  int signal_batched = strtol(argv[2], NULL, 0);
  printf("batch size = %d, signal_batched = %d\n", batch_size, signal_batched);

  const char* ip = "192.168.122.1";
  struct sockaddr_in addr = {
    .sin_family = AF_INET,
    .sin_port = htons(22222),
    .sin_addr = {},
  };
  inet_pton(AF_INET, ip, &addr.sin_addr);

  struct rdma_client* rdma =
    try_p(rdma_client_connect((struct sockaddr*)&addr, batch_size), "failed to connect to RDMA server");

  int buf_size = 4096;
  int work_count = 102400;

  void* local_buf = malloc(buf_size);
  struct ibv_mr* local_mem =
    try3_p(ibv_reg_mr(rdma->conn->pd, local_buf, buf_size,
                      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE),
           "failed to register memory region");

  double avg = 0;

  for (int i = 0; i < work_count; i++) {
    struct timespec tp1, tp2;
    struct ibv_sge sg[batch_size];
    struct ibv_send_wr wr[batch_size];
    // struct ibv_sge* sg = try3_p(calloc(batch_size, sizeof(*sg)), "nooo!");
    // struct ibv_send_wr* wr = calloc(batch_size, sizeof(*wr));
    for (int j = 0; j < batch_size; j++) {
      sg[j] = (typeof(sg[j])){
        .addr = (uintptr_t)local_buf + j * (buf_size / batch_size),
        .length = (buf_size / batch_size),
        .lkey = local_mem->lkey,
      };
      wr[j] = (typeof(wr[j])){
        .sg_list = &sg[j],
        .num_sge = 1,
        .send_flags = signal_batched ? 0 : IBV_SEND_SIGNALED,
        .wr.rdma = {.remote_addr = rdma->mem.addr + (rand() % (1 << 29)), .rkey = rdma->mem.rkey},
      };
    }
    for (int j = 1; j < batch_size; j++) wr[j - 1].next = &wr[j];
    if (signal_batched) wr[batch_size - 1].send_flags = IBV_SEND_SIGNALED;

    struct ibv_send_wr* bad;
    try3(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tp1), "failed to get 1st time");
    try3(ibv_post_send(rdma->conn->id->qp, &wr[0], &bad), "failed to post send");
    if (bad) {
      fprintf(stderr, "bad wr pointer non null\n");
      return 1;
    }

    int wc_cnt = signal_batched ? 1 : batch_size;
    struct ibv_wc wc[wc_cnt];
    int cnt = 0;
    while ((cnt += try3(ibv_poll_cq(rdma->conn->send_cq, wc_cnt - cnt, wc + cnt), "failed to poll completion queue")) <
           wc_cnt) {
      // printf("cnt = %d\n", cnt);
    };

    try3(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tp2), "failed to get 2nd time");
    double delta = (double)(tp2.tv_sec - tp1.tv_sec) * 1.0e9 + (double)(tp2.tv_nsec - tp1.tv_nsec);

    for (int i = 0; i < wc_cnt; i++) {
      if (wc[i].status != IBV_WC_SUCCESS) {
        fprintf(stderr, "work completion error: %s %d\n", ibv_wc_status_str(wc[i].status), wc[i].status);
        return 1;
      }
    }
    // printf("done reading! cnt = %d, time = %lfns\n", cnt, delta);
    avg += delta;
  }

  avg /= work_count;
  printf("avg = %lf\n", avg);
  struct ibv_wc wc[1];
  while ((try3(ibv_poll_cq(rdma->conn->send_cq, 1, wc), "failed to poll completion queue")) != 0) printf("!\n");
  FILE* file = fopen("result.csv", "a");
  fprintf(file, "%d,%d,%lf\n", batch_size, signal_batched, avg);

  return 0;

error:
  rdma_client_free(rdma);
  return -errno;
}
