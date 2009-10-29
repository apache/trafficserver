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
 *
 * libfastio tests
 * 
 * 
 *
 *
 */


#include <stdio.h>


#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>

#include "libfastio.h"
#include "../include/fastio.h"



void
main()
{
  int i, j;
  struct fastIO_state *cookie;
  struct fastIO_session *session, *vsession;

  struct fastIO_block *block;
  struct fastIO_request *req;
  struct fastIO_pkt *pkt;
  int fd;
  struct sockaddr_in sa;
  int optVal;
  int portNum = 5000;
  int err;
  struct fastIO_split_rule rule;
  struct fastIO_block *blocks[15];
  fd = socket(PF_INET, SOCK_DGRAM, 0);
  printf("FD:%d\n", fd);

  memset(&sa, 0, sizeof(struct sockaddr_in));
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = INADDR_ANY;

  /* If this is 0, the OS will pick a port number */
  sa.sin_port = htons(portNum);
  if ((bind(fd, (struct sockaddr *) &sa, sizeof(struct sockaddr_in))) < 0) {
    perror("bind:");
    exit(1);
  }
  cookie = fastIO_init(1000);
  if (!cookie) {
    printf("FastIO initialization fails!\n");
    exit(-1);
  }
  session = fastIO_udpsession_create(cookie, fd);
  if (!session) {
    printf("fiotest: Error creating fastIO UDP session!\n");
    exit(-1);
  }
  vsession = fastIO_vsession_create(cookie);
  if (!vsession) {
    printf("fiotest: Error creating fastIO virtual session!\n");
    exit(-1);
  }


  printf("fiotest: Initialized fastIO and created session.\n");



  /* allocate 11 blocks */
  fastIO_balloc(cookie, 11, blocks, 0);

  /*printf("fiotest: got block %d at 0x%x\n", block->id, (int) block->ptr); */

  sa.sin_addr.s_addr = inet_addr("209.131.54.105");
  sa.sin_port = ntohs(5000);


  /*record a sendto */
  /* err = sendto(fd, blocks[0]->ptr, 1466, 0, (struct sockaddr *) &sa, 
     sizeof(struct sockaddr_in)); */

  if (err < 0)
    perror("sendto");


  req = (struct fastIO_request *) blocks[10]->ptr;
  req->destIP = inet_addr("209.131.54.105");
  req->destPort = ntohs(5000);
  req->pktCount = 10;
  bzero(&req->startTime, sizeof(hrtime_t));     /* start now */
  req++;

  printf("Now: %lld.\n", gethrtime());
  pkt = (struct fastIO_pkt *) req;
  pkt->pktsize = 1466;
  pkt->blockID = blocks[0]->id;
  pkt->delaydelta = 0;
  pkt++;

  pkt->pktsize = 1466;
  pkt->blockID = blocks[1]->id;
  pkt->delaydelta = 200;
  pkt++;


  pkt->pktsize = 1466;
  pkt->blockID = blocks[2]->id;
  pkt->delaydelta = 200;
  pkt++;


  pkt->pktsize = 1466;
  pkt->blockID = blocks[3]->id;
  pkt->delaydelta = 200;
  pkt++;


  pkt->pktsize = 1466;
  pkt->blockID = blocks[4]->id;
  pkt->delaydelta = 200;
  pkt++;


  pkt->pktsize = 1466;
  pkt->blockID = blocks[5]->id;
  pkt->delaydelta = 200;
  pkt++;


  pkt->pktsize = 1466;
  pkt->blockID = blocks[6]->id;
  pkt->delaydelta = 200;
  pkt++;


  pkt->pktsize = 1466;
  pkt->blockID = blocks[7]->id;
  pkt->delaydelta = 200;
  pkt++;


  pkt->pktsize = 1466;
  pkt->blockID = blocks[8]->id;
  pkt->delaydelta = 200;
  pkt++;


  pkt->pktsize = 1466;
  pkt->blockID = blocks[9]->id;
  pkt->delaydelta = 200;
  pkt++;

  pkt->pktsize = 0xff;
  pkt->blockID = 0xff;
  pkt->delaydelta = 0xff;




  printf("***************Plumbing rules on vsession***************\n");

  printf("Plumbing a rule to port 4000.\n");
  rule.splitTo = session;
  rule.dstIP = inet_addr("209.131.54.105");
  rule.dstPort = htons(4000);
  rule.flags = 0;

  fastIO_add_split_rule(vsession, &rule);

  printf("Plumbing a rule to port 3000.\n");
  rule.splitTo = session;
  rule.dstIP = inet_addr("209.131.54.105");
  rule.dstPort = htons(3000);
  rule.flags = 0;
  fastIO_add_split_rule(vsession, &rule);



  printf("Plumbing a rule to port 3001.\n");
  rule.splitTo = session;
  rule.dstIP = inet_addr("209.131.54.105");
  rule.dstPort = htons(3001);
  rule.flags = 0;
  fastIO_add_split_rule(vsession, &rule);



  printf("Plumbing a rule to port 3002.\n");
  rule.splitTo = session;
  rule.dstIP = inet_addr("209.131.54.105");
  rule.dstPort = htons(3002);
  rule.flags = 0;
  fastIO_add_split_rule(vsession, &rule);


  sleep(3);
  printf("**************Sending a bunch of stuff to the vsession*************\n");

  fastIO_sendto(vsession, blocks[10]->id);

  printf("Sent to vsession.\n");


  sleep(5);



  fastIO_session_destroy(session);
  fastIO_session_destroy(vsession);
  fastIO_fini(cookie);

}
