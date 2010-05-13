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

/**************************************
 *
 * MgmtPing.cc
 *   icmp ping class
 *
 *
 */

#include "ink_config.h"
#include "ink_platform.h"
#include "ink_unused.h"        /* MAGIC_EDITING_TAG */

#include "MgmtPing.h"
#include "Main.h"

MgmtPing::MgmtPing()
{
  struct protoent *proto;
  icmp_fd = -1;
  pid = -1;
  npacks_to_trans = -1;
  timeout_sec = -1;

  if ((proto = getprotobyname("icmp")) == NULL) {
    mgmt_elog(stderr, "[MgmtPing::MgmtPing] Unable to get icmp proto\n");
    return;
  }

  if ((icmp_fd = socket(AF_INET, SOCK_RAW, proto->p_proto)) < 0) {
    mgmt_elog(stderr, "[MgmtPing::MgmtPing] Unable to open raw icmp socket\n");
    return;
  }
#ifndef _WIN32
  if (fcntl(icmp_fd, F_SETFD, 1) < 0) {
    mgmt_elog(stderr, "[MgmtPing::MgmtPing] Unable to set close-on-exec\n");
    close(icmp_fd);
    icmp_fd = -1;
    return;
  }
#endif
  pid = getpid() & 0xffff;
  return;
}                               /* End MgmtPing::MgmtPing */


MgmtPing::~MgmtPing()
{
  if (icmp_fd > 0) {
    close(icmp_fd);
  }
  return;
}                               /* End MgmtPing::~MgmtPing */


int
MgmtPing::init()
{
  if (icmp_fd > 0) {
    RecGetRecordInt("proxy.config.ping.npacks_to_trans", &npacks_to_trans);
    RecGetRecordInt("proxy.config.ping.timeout_sec", &timeout_sec);
  } else {
    return 0;
  }
  return 1;
}                               /* End MgmtPing::init */


/*
 * in_cksum(...)
 *   Checksum routine for Internet Protocol family headers (C Version).
 * Refer to "Computing the Internet Checksum" by R. Braden, D. Borman and
 * C. Partridge, Computer Communication Review, Vol. 19, No. 2, April 1989,
 * pp. 86-101, for additional details on computing this checksum.
 */
unsigned short
MgmtPing::in_cksum(unsigned short *ptr, int nbytes)
{
  long sum = 0;
  unsigned short oddbyte;
  unsigned short answer;

  /*
   * Our algorithm is simple, using a 32-bit accumulator (sum),
   * we add sequential 16-bit words to it, and at the end, fold back
   * all the carry bits from the top 16 bits into the lower 16 bits.
   */
  while (nbytes > 1) {
    sum += *ptr++;
    nbytes -= 2;
  }

  if (nbytes == 1) {            /* mop up an odd byte, if necessary */
    oddbyte = 0;                /* make sure top half is zero */
    *((unsigned char *) &oddbyte) = *(unsigned char *) ptr;     /* one byte only */
    sum += oddbyte;
  }

  /* Add back carry outs from top 16 bits to low 16 bits. */
  sum = (sum >> 16) + (sum & 0xffff);   /* add high-16 to low-16 */
  sum += (sum >> 16);           /* add carry */
  answer = ~sum;                /* ones-complement, then truncate to 16 bits */
  return (answer);              /* return checksum in low-order 16 bits */
}                               /* End MgmtPing::in_cksum */

bool
MgmtPing::pingAddress(char *addr)
{
#ifndef _WIN32
  int n, iphdrlen, len;
  unsigned char recvpack[packet_size];
  struct timeval timeout = { timeout_sec, 0 };
  struct sockaddr_in address;
  struct ip *ip;
  struct icmp *icp;
  ink_hrtime end_time;
  ink_hrtime time_left;

  memset(&address, '\0', sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = inet_addr(addr);
  for (int i = 0; i < (int) npacks_to_trans; i++) {
    fd_set fdlist;

    sendER(&address, i);        /* Send the ICMP ER packet */
    Debug("ping", "Sending ping packet to %s, attmept %d", addr, i + 1);
    time_left = ink_hrtime_from_sec(timeout_sec);
    end_time = ink_get_hrtime() + time_left;

    while (1) {

      /* We break out of this loop when we get a successful ping
         or time runs out */

      FD_ZERO(&fdlist);
      FD_SET(icmp_fd, &fdlist);
      ink_hrtime_to_timeval2(time_left, &timeout);
      Debug("ping_timeout", "Entring select with %d sec and %ld us", timeout.tv_sec, timeout.tv_usec);
      n = select(FD_SETSIZE, &fdlist, NULL, NULL, &timeout);

      if (n == 0) {             /* Timeout */
        mgmt_elog(stderr, "[MgmtPing::pingAddress] Timeout on ping to %s\n", addr);
        break;
      }
      len = sizeof(address);

      if ((n = recvfrom(icmp_fd, (char *) recvpack, (int) sizeof(recvpack), 0, (struct sockaddr *) &address,
                        (socklen_t *) & len
           )) < 0) {
        mgmt_elog("[MgmtPing::pingAddress] Failed to received packet\n");
        break;
      }

      recvpack[sizeof(recvpack) - 1] = '\0';
      ip = (struct ip *) recvpack;
      iphdrlen = ip->ip_hl << 2;        /* convert # 16-bit words to #bytes */
      icp = (struct icmp *) (recvpack + iphdrlen);

      // FIX: This check does not make sure that the ICMP_ECHOREPLY is
      //   coming from address we are pinging to us.  It is possible that
      //   we see our own reply to another ping query.  However, I'm
      //   already chaning alot of code in this EBF so I'm leaving this
      //   issue alone for now.  I would have thought the icmp_id field
      //   would solve this problem but if you are externally pinging an
      //   up address on the machine you will never find the down
      //   address at least on Solaris
      if ((n < iphdrlen + ICMP_MINLEN) || (icp->icmp_type != ICMP_ECHOREPLY) || (icp->icmp_id != pid)) {

        Debug("ping", "Dectected cruft on ICMP socket while pinging %s."
              "  Length '%d', Type '%d', Id '%d'", addr, n, (int) icp->icmp_type, (int) icp->icmp_id);
      } else {
        Debug("ping", "Successful ping of %s. Return packet from %s, type '%d', id '%d'",
              addr, inet_ntoa(ip->ip_src), (int) icp->icmp_type, (int) icp->icmp_id);

        return true;
      }

      time_left = end_time - ink_get_hrtime();
      if (time_left < 0) {
        time_left = (ink_hrtime) 0;
      }
    }
  }
#endif // !_WIN32
  return false;
}                               /* End MgmtPing::pingAddress */


void
MgmtPing::sendER(struct sockaddr_in *address, int seqn)
{
#ifndef _WIN32
  int i;
  unsigned char sendpack[packet_size];
  struct icmp *icp;

  /* Fill in the ICMP header */
  icp = (struct icmp *) sendpack;       /* pointer to ICMP header */
  icp->icmp_type = ICMP_ECHO;
  icp->icmp_code = 0;
  icp->icmp_cksum = 0;          /* init to 0, then call in_cksum() below */
  icp->icmp_id = pid;           /* our pid, to identify on return */
  icp->icmp_seq = seqn;         /* sequence number */

  icp->icmp_cksum = in_cksum((unsigned short *) icp, packet_size);      /* Checksum the header */
  if (((i = sendto(icmp_fd, (char *) sendpack, packet_size, 0, (struct sockaddr *) address,
                   sizeof(*address))) < 0) || (i != packet_size)) {
    mgmt_elog(stderr, "[MgmtPing::sendER] Failed in packet send\n");
  }
#endif // !_WIN32
  return;
}                               /* End MgmtPing::sendER */
