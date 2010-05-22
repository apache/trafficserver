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
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/stat.h>

#if !defined(freebsd)
#include <stropts.h>
#endif

#define SENDER		0
#define RECEIVER	1
#define N_PACKETS	(1024 * 1024)
/* #define PKT_SIZE	250 */
int PKT_SIZE;

int ngot, bytesgot;

int sockFd;
struct sockaddr_in sa;
int numClients;

void DoStream(int fd, struct sockaddr_in *to, int datarate);
void CreateSocket(int *fd, int portNum);
void DoSend(int fd, struct sockaddr_in *to);
void DoReceive(int fd, struct sockaddr_in *from);
void ComputeTimeDiff(double *diff, const struct timeval *t1, const struct timeval *t2);

void
done(int nuttin)
{

  close(sockFd);
  printf("Shut down (SIGINT).\n");
  exit(1);



}

void
alarmsig(int nuttin)
{
  int i;
  struct timeval tv1, tv2;
  double delta;
  int dataRate = numClients;
  time_t thetime;

  printf("Woke up . . .\n");
  signal(SIGALRM, alarmsig);
  alarm(1);


  gettimeofday(&tv1, 0);

  DoStream(sockFd, &sa, dataRate);
  gettimeofday(&tv2, 0);

  ComputeTimeDiff(&delta, &tv1, &tv2);


  printf("Time: %0f, %0fMbps\n", delta, (double) dataRate / delta / (1024 * 1024));

}

void
recv_alarm(int nuttin)
{

  signal(SIGALRM, recv_alarm);
  alarm(1);
  printf("Received %d packets, %d bytes (%8.4f Mbps).\n", ngot, bytesgot, (float) bytesgot * 8.0 / (1024.0 * 1024.0));
  bytesgot = 0;
  ngot = 0;
}


int
main(int argc, char **argv)
{
  int selfPort, otherPort;
  int ptype;
  char *otherHost;
  unsigned char *p;

  struct hostent *hostentry;

  if (argc <= 6) {
    printf("Usage: %s <sender(0)/receiver(1)> <my port> <other host> <other port> <pkt size> <client count>\n",
           argv[0]);
    exit(0);
  }
  /* 0 == sender; 1 == receiver */
  ptype = atoi(argv[1]);
  selfPort = htons(atoi(argv[2]));
  otherHost = argv[3];
  otherPort = htons(atoi(argv[4]));
  PKT_SIZE = atoi(argv[5]);
  numClients = atoi(argv[6]);

  CreateSocket(&sockFd, selfPort);
  if ((hostentry = gethostbyname(otherHost)) == NULL) {
    perror("hostentry:");
    close(sockFd);
    exit(1);
  }

  printf("SockFD: %d\n (A)", sockFd);
  bzero((char *) &sa, sizeof(struct sockaddr_in));
  printf("SockFD: %d\n (B)", sockFd);
  sa.sin_family = AF_INET;
  printf("SockFD: %d (C)\n", sockFd);
  memcpy(&sa.sin_addr.s_addr, *(hostentry->h_addr_list), sizeof(struct sockaddr_in));
  printf("SockFD: %d(L)", sockFd);
  p = (unsigned char *) &(sa.sin_addr);
  sa.sin_port = otherPort;
  printf("SockFD: %d (M)", sockFd);
  printf("Other Socket address: %d.%d.%d.%d port = %d \n", p[0], p[1], p[2], p[3], sa.sin_port);

  printf("SockFD: %d (Q)", sockFd);
  if (ptype == SENDER) {
    signal(SIGALRM, alarmsig);
    signal(SIGINT, done);
    alarm(1);

    printf("SockFD: %d (Z)", sockFd);

    while (1) {
      sleep(1);
    }
  } else if (ptype == RECEIVER)
    DoReceive(sockFd, &sa);

  close(sockFd);

}


void
DoStream(int fd, struct sockaddr_in *to, int datarate)
{
  char *buffer;
  int flags = 0, nfailed = 0, nsent = 0;
  int i, err, pktcount;
  double timeSpent;
  double mbps;
  struct timeval startTime, endTime;


  buffer = valloc(PKT_SIZE);

  pktcount = datarate / 8 / PKT_SIZE;
/*  printf("Sending %d packets . . . (%d) \n", pktcount, fd);*/
  for (i = 0; i < pktcount; i++) {
    err = 0;
    err = sendto(fd, buffer, PKT_SIZE, flags, (struct sockaddr *) to, sizeof(struct sockaddr_in));
    if (err != PKT_SIZE) {
      nfailed++;
      perror("Xmit failure:");
    } else
      nsent++;

  }

  nsent -= nfailed;
  if (timeSpent)
    mbps = ((((nsent / timeSpent) * PKT_SIZE) / (1024.0 * 1024.0))) * 8.0;
  else
    mbps = -1;
  //printf("Performance: %lf Mb/s\n", mbps);
  free(buffer);
}


void
DoReceive(int fd, struct sockaddr_in *from)
{
  char *buffer;
  int flags = 0, fromLen;
  int i, err;
  double timeSpent;
  struct timeval startTime, endTime;

  ngot = 0;
  bytesgot = 0;


  signal(SIGALRM, recv_alarm);
  alarm(1);

  err = setsockopt(fd, SOL_SOCKET, SO_RCVLOWAT, &PKT_SIZE, sizeof(int));
  if (err) {
    perror("setsockopt--rvclowat:");
    /*exit(1); */
  }

  buffer = valloc(PKT_SIZE);
  gettimeofday(&startTime, NULL);

  while (1) {

    err = recvfrom(fd, buffer, PKT_SIZE, flags, NULL, 0);
    if (err > 0) {
      int j;
      ngot++;
      bytesgot += err;

    } else {
      printf("\n recvfrom returned: %d \n", err);
      perror("recvfrom:");
    }

  }

  free(buffer);
}


void
ComputeTimeDiff(double *diff, const struct timeval *t1, const struct timeval *t2)
{
  if (t2->tv_usec > t1->tv_usec)
    *diff = (t2->tv_sec - t1->tv_sec) + ((t2->tv_usec - t1->tv_usec) * 1e-6);
  else
    *diff = (t2->tv_sec - t1->tv_sec - 1) + ((1e6 + t2->tv_usec - t1->tv_usec) * 1e-6);
}

void
CreateSocket(int *fd, int portNum)
{
  int sockFd, namelen;
  int optVal, optLen;
  struct sockaddr_in sa;        /* Compute t2 - t1 */
  unsigned char *p;

  if ((sockFd = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("socket:");
    exit(1);
  }

  memset(&sa, 0, sizeof(struct sockaddr_in));
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = INADDR_ANY;
  /* If this is 0, the OS will pick a port number */
  sa.sin_port = portNum;
  if ((bind(sockFd, (struct sockaddr *) &sa, sizeof(struct sockaddr_in))) < 0) {
    perror("bind:");
    exit(1);
  }
  namelen = sizeof(struct sockaddr_in);
  getsockname(sockFd, (struct sockaddr *) &sa, &namelen);
  p = (unsigned char *) &(sa.sin_addr);

  optVal = 65536;
  if (setsockopt(sockFd, SOL_SOCKET, SO_SNDBUF, &optVal, sizeof(int))) {
    perror("set sock opt: snd buf");
    exit(1);
  }

  if (setsockopt(sockFd, SOL_SOCKET, SO_RCVBUF, &optVal, sizeof(int))) {
    perror("set sock opt: recv buf");
    exit(1);
  }

  printf("Socket address: %d.%d.%d.%d port = %d \n", p[0], p[1], p[2], p[3], sa.sin_port);
  *fd = sockFd;

}
