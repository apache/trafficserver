/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

/*
  This is the standalone program to receive the UDP packet from the parent
  and stream them to TS on local host

  Right now, if an out of order packet arrives, we just neglect it.
*/

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define Debug(print) \
  do {               \
    if (debug_on)    \
      printf print;  \
  } while (0)
int debug_on = 0;

#define UDP_BUF_SIZE (64 * 1024)
#define TSPORT 39679

#define STREAM_TIMEOUT_SECS 6000
typedef unsigned int uint32;

/*taken from Prefetch.cc */
struct prefetch_udp_header {
  // uint32 response_flag:1, last_pkt:1, pkt_no:30;
  uint32_t pkt;
  uint32_t md5[4];
};

#define RESPONSE_FLAG (1 << 31)
#define LAST_PKT_FLAG (1 << 30)
#define PKT_NUM_MASK ((1 << 30) - 1)

#define PACKET_HDR_SIZE 20

/* statistics */
static int number_of_packets_received  = 0;
static int number_of_packets_dropped   = 0;
static int number_of_connections_to_ts = 0;
static int number_of_timeouts          = 0;

/* TODO: this functions should be a signal handler ... */
int
stufferUdpStatShow()
{
  printf("no of packets received\t:\t%d\n"
         "no of packets dropped\t:\t%d\n"
         "no of connections to TS\t:\t%d\n"
         "no of timeouts\t\t:\t%d\n",
         number_of_packets_received, number_of_packets_dropped, number_of_connections_to_ts, number_of_timeouts);

  return 0;
}

struct Stream {
  time_t last_activity_time;
  prefetch_udp_header hdr;
  int fd; // tcp connection

  Stream *next;
};

class StreamHashTable
{
  Stream **array;
  int size;

public:
  StreamHashTable(int sz)
  {
    size  = sz;
    array = new Stream *[size];
    memset(array, 0, size * sizeof(Stream *));
  }
  ~StreamHashTable() { delete[] array; }
  int
  index(prefetch_udp_header *hdr)
  {
    return hdr->md5[3] % size;
  }
  Stream **position(prefetch_udp_header *hdr);
  Stream **
  position(Stream *s)
  {
    return position(&s->hdr);
  }
  Stream *
  lookup(prefetch_udp_header *hdr)
  {
    return *position(hdr);
  }
  void add(Stream *s);
  void remove(Stream *s);

  int deleteStaleStreams(time_t now);
};
StreamHashTable *stream_hash_table;

Stream **
StreamHashTable::position(prefetch_udp_header *hdr)
{
  Stream **e = &array[index(hdr)];

  while (*e) {
    prefetch_udp_header *h = &((*e)->hdr);
    if (hdr->md5[0] == h->md5[0] && hdr->md5[1] == h->md5[1] && hdr->md5[2] == h->md5[2] && hdr->md5[3] == h->md5[3])
      return e;
    e = &(*e)->next;
  }
  return e;
}

void
StreamHashTable::add(Stream *s)
{
  Stream **e = position(s);
  assert(!*e);
  *e = s;
}

void
StreamHashTable::remove(Stream *s)
{
  Stream **e = position(s);
  assert(s == *e);
  *e = s->next;
}

int
StreamHashTable::deleteStaleStreams(time_t now)
{
  int nremoved = 0;
  for (int i = 0; i < size; i++) {
    Stream *&e = array[i];
    while (e) {
      if (e->last_activity_time < now - STREAM_TIMEOUT_SECS) {
        close(e->fd);
        number_of_timeouts++;

        Stream *temp = e;
        e            = e->next;
        delete temp;
        nremoved++;
      } else
        e = e->next;
    }
  }
  return nremoved;
}

int
openTSConn()
{
  int fd = socket(PF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    // perror("socket()");
    return -1;
  }

  struct sockaddr_in saddr;
  saddr.sin_family = AF_INET;
  saddr.sin_port   = htons(TSPORT);
  //#define INADDR_LOOPBACK ((209<<24)|(131<<16)|(52<<8)|48)
  saddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (connect(fd, (sockaddr *)&saddr, sizeof(saddr)) < 0) {
    perror("connect(TS)");
    close(fd);
    return -1;
  }

  number_of_connections_to_ts++;
  return fd;
}

int
processPacket(const char *packet, int pkt_sz)
{
  prefetch_udp_header *hdr = (prefetch_udp_header *)packet;
  uint32_t flags           = ntohl(hdr->pkt);

  int close_socket = 1;
  int sock_fd      = -1;

  number_of_packets_received++;

  Debug(("Received packet. response_flag : %d last_pkt: %d pkt_no: %d"
         " (%d)\n",
         (flags & RESPONSE_FLAG) ? 1 : 0, (flags & RESPONSE_FLAG) && (flags & LAST_PKT_FLAG),
         (flags & RESPONSE_FLAG) ? flags & PKT_NUM_MASK : 0, ntohl(hdr->pkt)));

  if (flags & RESPONSE_FLAG) {
    Stream *s       = stream_hash_table->lookup(hdr);
    uint32_t pkt_no = flags & PKT_NUM_MASK;

    if (pkt_no == 0 && !(flags & LAST_PKT_FLAG)) {
      if (s || !(s = new Stream)) {
        number_of_packets_dropped++;
        return -1;
      }
      s->hdr                = *hdr;
      s->hdr.pkt            = pkt_no;
      s->last_activity_time = time(NULL);
      s->next               = 0;
      s->fd                 = openTSConn();
      if (s->fd < 0) {
        delete s;
        return -1;
      } else
        sock_fd = s->fd;

      close_socket = 0;
      stream_hash_table->add(s);

    } else if (pkt_no > 0) {
      if (!s)
        return -1;

      s->last_activity_time = time(0);
      sock_fd               = s->fd;

      s->hdr.pkt++;

      if (s->hdr.pkt != pkt_no || flags & LAST_PKT_FLAG) {
        stream_hash_table->remove(s);
        delete s;
      } else
        close_socket = 0;

      if (s->hdr.pkt != pkt_no) {
        Debug(("Received an out of order packet dropping the "
               "connection expected %d but got %d\n",
               s->hdr.pkt, pkt_no));
        number_of_packets_dropped++;
        pkt_sz = 0; // we dont want to send anything.
      }
    }
    packet += PACKET_HDR_SIZE;
    pkt_sz -= PACKET_HDR_SIZE;
  }

  if (pkt_sz > 0) {
    if (sock_fd < 0) {
      sock_fd = openTSConn();
      if (sock_fd < 0)
        return -1;
    }

    Debug(("Writing %d bytes on socket %d\n", pkt_sz, sock_fd));
    while (pkt_sz > 0) {
      int nsent = write(sock_fd, (char *)packet, pkt_sz);
      if (nsent < 0)
        break;
      packet += nsent;
      pkt_sz -= nsent;
    }
  }

  if (close_socket && sock_fd >= 0)
    close(sock_fd);

  return 0;
}

int
main(int argc, char *argv[])
{
  int port = TSPORT;

  if (argc > 1)
    debug_on = 1;

  stream_hash_table = new StreamHashTable(257);

  char *pkt_buf = (char *)ats_malloc(UDP_BUF_SIZE);
  int fd        = socket(PF_INET, SOCK_DGRAM, 0);

  struct sockaddr_in saddr;
  saddr.sin_family      = AF_INET;
  saddr.sin_port        = htons(port);
  saddr.sin_addr.s_addr = INADDR_ANY;

  if ((bind(fd, (struct sockaddr *)&saddr, sizeof(saddr))) < 0) {
    perror("bind(udp_fd)");
    ats_free(pkt_buf);
    return 0;
  }

  time_t last_clean_up = time(0);

  while (1) {
    int pkt_size = read(fd, pkt_buf, UDP_BUF_SIZE);
    if (pkt_size < 0)
      return 0;

    Debug(("Processing udp packet (size = %d)\n", pkt_size));
    processPacket(pkt_buf, pkt_size);

    time_t now = time(0);
    if (now > last_clean_up + STREAM_TIMEOUT_SECS)
      stream_hash_table->deleteStaleStreams(now);
  }

  ats_free(pkt_buf);
}
