server_objs := server.o rdma_server.o
server_headers := try.h rdma_conn.h rdma_server.h

client_objs := client.o rdma_client.o
client_headers := try.h rdma_conn.h rdma_client.h

common_objs := rdma_conn.o
common_headers := try.h rdma_conn.h

CFLAGS += -g -O3 -march=native
LDFLAGS += -lrdmacm -libverbs

all: server client

$(server_objs): $(server_headers)
$(client_objs): $(client_headers)
$(common_objs): $(common_headers)

server: $(server_objs) $(common_objs)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

client: $(client_objs) $(common_objs)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

.PHONY: clean
clean:
	rm -f server client *.o
