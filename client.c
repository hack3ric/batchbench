#include <arpa/inet.h>
#include <bits/time.h>
#include <infiniband/verbs.h>
#include <rdma/rdma_verbs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>

#include "rdma_client.h"
#include "try.h"

int main() {
  const char* ip = "172.23.45.67";
  struct sockaddr_in addr = {
    .sin_family = AF_INET,
    .sin_port = htons(22222),
    .sin_addr = {},
  };
  inet_pton(AF_INET, ip, &addr.sin_addr);

  struct rdma_client* rdma = try_p(rdma_client_connect((struct sockaddr*)&addr), "failed to connect to RDMA server");

  int batch_size = 50;
  int chunk_size = 8192;
  int buf_size = chunk_size * batch_size;
  int work_count = batch_size * 1000;

  void* local_buf = malloc(buf_size);
  struct ibv_mr* local_mem =
    try3_p(ibv_reg_mr(rdma->conn->pd, local_buf, buf_size,
                      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE),
           "failed to register memory region");

  double avg = 0;

  for (int i = 0; i < work_count / batch_size; i++) {
    struct timespec tp1, tp2;
    struct ibv_sge sg[batch_size];
    struct ibv_send_wr wr[batch_size];
    // struct ibv_sge* sg = try3_p(calloc(batch_size, sizeof(*sg)), "nooo!");
    // struct ibv_send_wr* wr = calloc(batch_size, sizeof(*wr));
    for (int j = 0; j < batch_size; j++) {
      sg[j] = (typeof(sg[j])){
        .addr = (uintptr_t)local_buf + j * chunk_size,
        .length = chunk_size,
        .lkey = local_mem->lkey,
      };
      wr[j] = (typeof(wr[j])){
        .sg_list = &sg[j],
        .num_sge = 1,
        .send_flags = IBV_SEND_SIGNALED,
        .wr.rdma = {.remote_addr = rdma->mem.addr + (rand() % (1 << 29)), .rkey = rdma->mem.rkey},
      };
    }
    for (int j = 1; j < batch_size; j++) wr[j - 1].next = &wr[j];
    // wr[batch_size - 1].send_flags = IBV_SEND_SIGNALED;

    struct ibv_send_wr* bad;
    try3(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tp1), "failed to get 1st time");
    try3(ibv_post_send(rdma->conn->id->qp, &wr[0], &bad), "failed to post send");
    if (bad) printf("bad = %lu\n", (uintptr_t)(bad - wr) / sizeof(*bad));

    struct ibv_wc wc[batch_size];
    int cnt = 0;
    while ((cnt += try3(ibv_poll_cq(rdma->conn->send_cq, batch_size - cnt, wc), "failed to poll completion queue")) <
           batch_size);
    try3(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tp2), "failed to get 2nd time");
    double delta = (double)(tp2.tv_sec - tp1.tv_sec) * 1.0e9 + (double)(tp2.tv_nsec - tp1.tv_nsec);
    printf("done reading! cnt = %d, time = %lfns\n", cnt, delta);
    avg += delta / work_count;
  }

  printf("avg = %lf\n", avg);
  struct ibv_wc wc[1];
  while ((try3(ibv_poll_cq(rdma->conn->send_cq, 1, wc), "failed to poll completion queue")) != 0) printf("!\n");

  return 0;

error:
  rdma_client_free(rdma);
  return -errno;
}
