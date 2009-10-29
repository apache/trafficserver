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


#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <curses.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include "../include/fastio.h"
#include "../libfastIO/libfastio.h"

#include "requtil.h"

#define FAST_SPREAD 20

/*
 *  Build a fastIO request
 *  Allocates required packet and data blocks, 
 *  returns the ID of the request block
 *
 *
 */
uint32_t
bmark_build_request()
{

  int pktcount = bmark.bitrate / 8 / bmark.packetSize;
  int i;
  struct fastIO_block *fioblocks[151];
  struct fastIO_request *req;
  struct fastIO_pkt *pkt;

  /* allocate blocks for the request and packets */
  fastIO_balloc(bmark.cookie, pktcount + 1, fioblocks, 0);

  /* setup the request header */
  req = (struct fastIO_request *) fioblocks[0]->ptr;
  req->destIP = bmark.destsa.sin_addr.s_addr;
  req->destPort = bmark.destsa.sin_port;
  req->pktCount = pktcount;
  bzero(&req->startTime, sizeof(long long));
  /*memcpy(&req->startTime, &startTime, sizeof(long long)); */

  req++;
  pkt = (struct fastIO_pkt *) req;


  /* record the ID of each packet block we are sending */
  for (i = 1; i <= pktcount; i++) {

    char databuf[4000];

    pkt->pktsize = bmark.packetSize;
    pkt->blockID = fioblocks[i]->id;
    pkt->delaydelta = bmark.delay;
    pkt++;

#if 0
    /* write some data into the packet */
    sprintf(databuf, "This is packet %d.  Ain't that just dandy?\n", i);
    memcpy(fioblocks[i]->ptr, databuf, strlen(databuf));
#endif

  }

  pkt->pktsize = 0xffffffff;
  pkt->blockID = 0xffffffff;


  /* return the ID of the request block */
  return fioblocks[0]->id;


}


/*
 *  Run one second's workload for fastIO
 *
 */
void
bmark_fast_run()
{
  struct fastIO_block *metareq;
  uint32_t *meta;
  int i, j;
  struct ink_fio_stats stats;


  if (bmark.multicast) {
    uint32_t req;

    /* allocate the metarequest block */
    fastIO_balloc(bmark.cookie, 1, &metareq, 0);
    meta = (uint32_t *) metareq->ptr;

    /* send on a multicast'ed virtual session */
    req = bmark_build_request();
    fastIO_sendto(bmark.vsession, req);

  } else {
    /* send to "different" destinations */
    for (j = 0; j < FAST_SPREAD; j++) {

      /* allocate the metarequest block */
      fastIO_balloc(bmark.cookie, 1, &metareq, 0);
      meta = (uint32_t *) metareq->ptr;

      for (i = 0; i < bmark.streamCount / FAST_SPREAD; i++) {


        /* build a request */
        meta[i] = bmark_build_request();

        /* specify the destination */
        fastIO_metarequest_setup(bmark.session[i], meta[i]);
      }

      /* metarequest terminator */
      meta[i] = 0xffffffff;

      /* send the metarequest */
      fastIO_metarequest_send(bmark.cookie, metareq->id);
    }

  }

  /* gather and report statistics */
  fastIO_get_stats(bmark.cookie, &stats);



}


/*
 * Run one second's workload for userIO
 *
 *
 */
void
bmark_user_run()
{
  int i, j;
  int pktcount = bmark.bitrate / 8 / bmark.packetSize;
  for (i = 0; i < bmark.streamCount; i++) {

    for (j = 0; j < pktcount; j++) {

      if (sendto(bmark.fd[i],
                 bmark.pktbuf + (bmark.packetSize * bmark.nextbuf), bmark.packetSize,
                 0, (struct sockaddr *) &bmark.destsa, sizeof(struct sockaddr_in))
          != bmark.packetSize)
        perror("sendto");
      bmark.nextbuf = (bmark.nextbuf + 1) % bmark.datablks;


    }

  }





}




/*
 * Setup the fastIO bench
 *
 */
void
bmark_fast_setup()
{
  int i;
  struct sockaddr_in sa;

  /* create a fastIO instance */
  bmark.cookie = fastIO_init(bmark.blkcount);


  /* create all the sockets and sessions for them */
  for (i = 0; i < bmark.streamCount; i++) {

    bmark.fd[i] = socket(PF_INET, SOCK_DGRAM, 0);
    if (bmark.fd[i] < 0) {
      perror("socket");
      exit(1);
    }
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port = htons(ntohs(bmark.srcPort) + i);
    if ((bind(bmark.fd[i], (struct sockaddr *) &sa, sizeof(struct sockaddr_in))) < 0) {
      perror("bind:");
      exit(1);
    }

    /* create a fastIO session on this socket */
    bmark.session[i] = fastIO_udpsession_create(bmark.cookie, bmark.fd[i]);
    if (!bmark.session[i]) {
      printf("Error creating fastIO session\n");
      exit(0);
    }
  }

  if (bmark.multicast) {
    /* if multicast, then create a vsession and plumb redirect rules */
    struct fastIO_split_rule rule;

    bmark.vsession = fastIO_vsession_create(bmark.cookie);

    rule.dstIP = bmark.destsa.sin_addr.s_addr;
    rule.dstPort = bmark.destsa.sin_port;
    rule.flags = 0;

    if (!bmark.vsession) {
      printf("Error creating fastIO vsession.\n");
      exit(0);
    }
    printf("Created vsession, plumbing rules...\n");

    /* plumb a split rule from the vsession to each of the destination sessions */
    for (i = 0; i < bmark.streamCount; i++) {
      rule.splitTo = bmark.session[i];
      fastIO_add_split_rule(bmark.vsession, &rule);

    }

  }
}


/*
 * Setup the userIO bench
 *
 */
void
bmark_user_setup()
{
  int i;
  struct sockaddr_in sa;


  /* create all the sockets and sessions for them */
  for (i = 0; i < bmark.streamCount; i++) {

    bmark.fd[i] = socket(PF_INET, SOCK_DGRAM, 0);
    if (bmark.fd[i] < 0) {
      perror("socket");
      exit(1);
    }
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port = htons(ntohs(bmark.srcPort) + i);
    if ((bind(bmark.fd[i], (struct sockaddr *) &sa, sizeof(struct sockaddr_in))) < 0) {
      perror("bind:");
      exit(1);
    }

  }

  /* allocate the packet buffer */

  bmark.pktbuf = (char *) malloc(bmark.datablks * bmark.packetSize);
  bmark.nextbuf = 0;

  if (!bmark.pktbuf) {
    printf("Unable to allocate packet buffer.");
    exit(0);
  }

}
