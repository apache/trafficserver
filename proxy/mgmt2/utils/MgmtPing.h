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
 * MgmtPing.h
 *   Ping wrapper for mgmt class.
 *
 * $Date: 2007-10-05 16:56:46 $
 *
 *
 */

#ifndef _MGMT_PING_H
#define _MGMT_PING_H

#include "ink_platform.h"
#include "ink_bool.h"

#define	ICMP_HEADER_SIZE	   8    /* 8-byte ICMP header */
#define	MGMT_ICMP_DATALEN	   56   /* How many data bytes to have in the packet */

#include "P_RecCore.h"


class MgmtPing
{

public:

  MgmtPing();
  ~MgmtPing();

  int init();
  bool pingAddress(char *address);
  void sendER(struct sockaddr_in *address, int seqn);
  u_short in_cksum(u_short * ptr, int nbytes);

private:

  int icmp_fd;
  int pid;
  RecInt npacks_to_trans;
  RecInt timeout_sec;

  enum
  { packet_size = ICMP_HEADER_SIZE + MGMT_ICMP_DATALEN };

};                              /* End class BaseManager */


#endif /* _MGMT_PING_H */
