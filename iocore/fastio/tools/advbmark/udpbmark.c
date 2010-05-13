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
 * Solaris UDP FastIO bench
 *
 *
 *
 *
 */


#include <stdio.h>
#include "../libfastIO/libfastio.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <curses.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <thread.h>
#include <synch.h>
#include "requtil.h"





struct bmark_options bmark;
pthread_t tid;
pthread_attr_t attr;
cond_t cond_go;
mutex_t mx_go;


/* prototypes */
void ComputeTimeDiff(double *diff, const struct timeval *t1, const struct timeval *t2);
void alarmsig(int nuttin);
int main(int argc, char **argv);

/*
 * Signal Handler for SIGINT
 */
void
intsig(int nuttin)
{
  int i;

  printf("bmark: SIGINT received. Exiting.\n");
  fflush(stdout);
  if (bmark.testType == 1) {
    sleep(5);
    for (i = 0; i < bmark.streamCount; i++) {
      fastIO_session_destroy(bmark.session[i]);
      close(bmark.fd[i]);
    }
    fastIO_fini(bmark.cookie);

    if (bmark.multicast) {
      fastIO_flush_split_rules(bmark.vsession);
      fastIO_session_destroy(bmark.vsession);
    }
  } else {
    for (i = 0; i < bmark.streamCount; i++)
      close(bmark.fd[i]);
    free(bmark.pktbuf);
  }

}

/*
 * Signal handler for SIGUSR1.
 * Required for memory management
 */
void
siguser(int nuttin)
{

  printf("SIGUSER!\n");
  signal(SIGUSR1, siguser);

}

/*
 *  Handle segmentation faults gracefully,
 *  if such a thing is possible
 */
void
segsig(int nuttin)
{


  printf("shutting down (SEGMENTATION FAULT - YOU BOZO!)....\n");
  sleep(10);
  exit(0);

}


/*
 * Signal Handler for SIGALRM
 * Each time the alarm goes off, send the required streams
 *
 */
void
alarmsig(int nuttin)
{

}


void
thread_main(void *nuttin)
{
  struct timeval tv1, tv2;
  double delay;
  printf("Starting benchmark: \n");
  fflush(stdout);


  /* Install Signal Handlers */
  signal(SIGALRM, alarmsig);
  signal(SIGINT, intsig);
  signal(SIGSEGV, segsig);
  signal(SIGUSR1, siguser);

  while (1) {
    mutex_lock(&mx_go);
    cond_wait(&cond_go, &mx_go);
    mutex_unlock(&mx_go);

    gettimeofday(&tv1, 0);
    if (bmark.testType == 0)
      bmark_user_run();
    else
      bmark_fast_run();
    gettimeofday(&tv2, 0);

    ComputeTimeDiff(&delay, &tv1, &tv2);
    printf("Time: %6.2f.\n", delay);
    fflush(stdout);
  }


}


int
main(int argc, char **argv)
{

  int i;

  if (argc <= 10) {
    printf
      ("Usage: %s <duration> <destIP> <destPort> <srcPort> <bitrate> <streamCount> <pktsize> <multicast> <userIO(0)/fastIO(1)> <interpacket delay> <shared block count>e\n",
       argv[0]);
    exit(0);
  }

  bmark.duration = atoi(argv[1]);
  bmark.destsa.sin_family = AF_INET;
  bmark.destsa.sin_addr.s_addr = inet_addr(argv[2]);
  bmark.destsa.sin_port = htons(atoi(argv[3]));
  bmark.srcPort = htons(atoi(argv[4]));
  bmark.bitrate = atoi(argv[5]);
  bmark.streamCount = atoi(argv[6]);
  bmark.packetSize = atoi(argv[7]);
  bmark.multicast = atoi(argv[8]);
  bmark.testType = atoi(argv[9]);
  bmark.delay = atoi(argv[10]);
  if (bmark.testType == 1)
    bmark.blkcount = atoi(argv[11]);
  bmark.datablks = atoi(argv[10]);


  /* Install Signal Handlers */
  signal(SIGALRM, alarmsig);
  signal(SIGINT, intsig);
  signal(SIGSEGV, segsig);



  /* print some information about the test we're running */
  if (bmark.testType == 0) {
    printf("Test Type:\tUserIO\n");

    printf("Duration:\t%d Seconds.\n", bmark.duration);
    printf("DestIP:\t\t%s\n", inet_ntoa(bmark.destsa.sin_addr));
    printf("DestPort:\t%d\n", ntohs(bmark.destsa.sin_port));
    printf("SrcPort:\t%d\n", ntohs(bmark.srcPort));
    printf("Bitrate:\t%d\n", bmark.bitrate);
    printf("Streams:\t%d\n", bmark.streamCount);
    if (bmark.packetSize > 1466) {
      printf("Error: Packet size must be <= 1466 bytes.\n");
      exit(0);
    }
    printf("Pkt size:\t%d\n", bmark.packetSize);
    printf("Multicast:\t%s\n", bmark.multicast ? "yes" : "no");
    printf("Buf Pkts:\t%d\n", bmark.datablks);

    /* now initialize the test */
    bmark_user_setup();



  } else if (bmark.testType != 1) {
    printf("Invalid test type %d.\n", bmark.testType);
  } else if (bmark.testType == 1) {
    printf("Test Type:\tFastIO\n");

    printf("Duration:\t%d Seconds.\n", bmark.duration);
    printf("DestIP:\t\t%s\n", inet_ntoa(bmark.destsa.sin_addr));
    printf("DestPort:\t%d\n", ntohs(bmark.destsa.sin_port));
    printf("SrcPort:\t%d\n", ntohs(bmark.srcPort));
    printf("Bitrate:\t%d\n", bmark.bitrate);
    printf("Streams:\t%d\n", bmark.streamCount);
    if (bmark.packetSize > 1466) {
      printf("Error: Packet size must be <= 1466 bytes.\n");
      exit(0);
    }
    printf("Pkt size:\t%d\n", bmark.packetSize);
    printf("Multicast:\t%s\n", bmark.multicast ? "yes" : "no");
    printf("Buf Blocks:\t%d\n", bmark.blkcount);
    printf("Interpkt Delay:\t%d\n", bmark.delay);


    /* now initialize the test */
    bmark_fast_setup();



  }

  /* initialize synchronization stuff */
  cond_init(&cond_go, NULL, NULL);
  mutex_init(&mx_go, NULL, NULL);



  /* sleep for a while, giving the thread a chance to get ready */

  printf("Starting tests (main).\n");
  fflush(stdout);
  sleep(1);



  /* create the thread to handle doing the work */
  pthread_attr_init(&attr);     /* initialize attr with default attributes */
  if ((i = pthread_create(&tid, NULL, thread_main, 0))) {
    perror("pthread_create:");
    exit(0);
  }



  for (i = 0; i < bmark.duration; i++) {
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    mutex_lock(&mx_go);

    cond_signal(&cond_go);
    mutex_unlock(&mx_go);
    select(0, 0, 0, 0, &tv);

  }

  printf("Shutting down.\n");

  if (bmark.testType == 1) {
    sleep(5);
    for (i = 0; i < bmark.streamCount; i++) {
      fastIO_session_destroy(bmark.session[i]);
      close(bmark.fd[i]);
    }
    fastIO_fini(bmark.cookie);

  } else {
    for (i = 0; i < bmark.streamCount; i++)
      close(bmark.fd[i]);
    free(bmark.pktbuf);
  }
  printf("\n\nTests complete.\n");

}


/* Compute t2 - t1 */
void
ComputeTimeDiff(double *diff, const struct timeval *t1, const struct timeval *t2)
{
  if (t2->tv_usec > t1->tv_usec)
    *diff = (t2->tv_sec - t1->tv_sec) + ((t2->tv_usec - t1->tv_usec) * 1e-6);
  else
    *diff = (t2->tv_sec - t1->tv_sec - 1) + ((1e6 + t2->tv_usec - t1->tv_usec) * 1e-6);
}
