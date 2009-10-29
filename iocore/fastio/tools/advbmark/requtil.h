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
 * Benchmark for fastIO and userIO
 * 
 * 
 *
 *
 */


#include <stdio.h>
#include "libfastio.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <curses.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>


/* Maximum number of open sockets */

#define MAX_SOCKETS 900


struct bmark_options
{

  /* options for userIO and fastIO tests */

  int duration;                 /* number of seconds to run the test */
  struct sockaddr_in destsa;    /* destination port and IP */
  uint16_t srcPort;

  int bitrate;                  /* per stream */
  int streamCount;              /* number of streams */
  int multicast;                /* Same data, multiple destinations */
  int packetSize;               /* bytes per packet */
  int testType;                 /* 0: userIO, 1: fastIO */

  /* options for fastIO tests */
  int delay;                    /* interpacket delay */
  int blkcount;                 /* number of shared blocks */


  /* options for userIO tests */
  int datablks;                 /* number of different mem regions to send from */


  /* runtime structures */
  struct fastIO_state *cookie;
  struct fastIO_session *session[MAX_SOCKETS];
  struct fastIO_session *vsession;
  int fd[MAX_SOCKETS];

  char *pktbuf;                 /* store data to be sent by userIO */
  int nextbuf;                  /* which part of pktbuf to use, loops from 0 to datablks-1 */
};

/* global var stores parameters of the benchmark */
extern struct bmark_options bmark;

/*
 *  Build a fastIO request
 *  Allocates required packet and data blocks, 
 *  returns the ID of the request block
 */
uint32_t bmark_build_request();


/*
 *  Run one second's workload for fastIO
 *
 */
void bmark_fast_run();


/*
 * Run one second's workload for userIO
 *
 *
 */
void bmark_user_run();


/*
 * Setup the fastIO bench
 *
 */
void bmark_fast_setup();


/*
 * Setup the userIO bench
 *
 */
void bmark_user_setup();
