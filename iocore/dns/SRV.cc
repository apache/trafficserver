/** @file

  Support for SRV records

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

  @section description
        Support for SRV records

        http://www.faqs.org/rfcs/rfc2782.html
        http://www.nongnu.org/ruli/
        http://libsrv.cvs.sourceforge.net/libsrv/libsrv/src/libsrv.c
 */


#include "P_DNS.h"

struct HostDBRoundRobin;

ClassAllocator<SRV> SRVAllocator("SRVAllocator");

/*
To select a target to be contacted next, arrange all SRV RRs
(that have not been ordered yet) in any order, except that all
those with weight 0 are placed at the beginning of the list.

Compute the sum of the weights of those RRs, and with each RR
associate the running sum in the selected order. Then choose a
uniform random number between 0 and the sum computed
(inclusive), and select the RR whose running sum value is the
first in the selected order which is greater than or equal to
the random number selected. The target host specified in the
selected SRV RR is the next one to be contacted by the client.
Remove this SRV RR from the set of the unordered SRV RRs and
apply the described algorithm to the unordered SRV RRs to select
the next target host.  Continue the ordering process until there
are no unordered SRV RRs.  This process is repeated for each
Priority.
*/

static InkRand SRVRand(55378008);

void
SRVHosts::getWeightedHost(char *ret_val)
{
  int a_prev;
  int k = 0;
  int accum = 0;
  unsigned int pri = 0;
  SRV *i;
  //InkRand x(time(NULL));
  int tmp[1024];
  int j = 0;
  int v;
  uint32_t xx;

  if (hosts.empty() || getCount() == 0) {
    goto err;
  }

  /* Step 1/2 Sort based on 'priority': handled by operator<
   */

  hosts.sort();

  /*
   * Step 2/2: Select SRV RRs by random weighted order
   */

  //get lowest priority (now sorted)
  i = hosts.head;

  if (!i) {
    goto err;
  }
  //Get current priority
  pri = i->getPriority();
  //Accumulate weight sum for priority

  while (i != NULL && pri == i->getPriority()) {
    a_prev = accum;
    accum += i->getWeight();
    for (j = a_prev; j < accum; j++) {
      tmp[j] = k;
    }
    i = i->link.next;
    k++;
  }

  Debug("dns_srv", "accum=%d for priority=%d", accum, pri);

  if (!accum) {
    Debug("dns_srv", "Accumulator was 0. eek.");
    goto err;
  }
  //Pick random number: 0..accum
  xx = SRVRand.random() % accum;

  Debug("dns_srv", "picked %d as a random number", xx);

  i = hosts.head;
  v = tmp[xx];
  j = 0;
  while (j < v) {
    i = i->link.next;
    j++;
  }
  Debug("dns_srv", "using SRV record of: pri: %d, wei: %d, port: %d, host: %s",
        i->getPriority(), i->getWeight(), i->getPort(), i->getHost());

  ink_strlcpy(ret_val, i->getHost(), MAXDNAME);
  if (strcmp(ret_val, "") == 0 || strcmp(ret_val, ".") == 0) {
    goto err;
  }
  return;
err:
  Debug("dns_srv", "there was a problem figuring out getWeightedHost() -- we are returning a blank SRV host");
  ret_val[0] = '\0';
  return;
}

SRVHosts::SRVHosts(HostDBInfo * info)
{
  hosts.clear();
  srv_host_count = 0;
  if (!info)
    return;
  HostDBRoundRobin *rr_data = info->rr();

  if (!rr_data) {
    return;
  }

  for (unsigned int i = 0; i < info->srv_count; i++) {
    /* get the RR data */
    SRV *s = SRVAllocator.alloc();
    HostDBInfo nfo = rr_data->info[i];
    s->setPort(nfo.srv_port);
    s->setPriority(nfo.srv_priority);
    s->setWeight(nfo.srv_weight);
    s->setHost(&rr_data->rr_srv_hosts[i][0]);
    insert(s);
  }
  return;
}
