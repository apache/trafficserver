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

#ifndef _SRV_h_
#define _SRV_h_

#include "libts.h"
#include "I_HostDBProcessor.h"

struct HostDBInfo;

#define RAND_INV_RANGE(r) ((int) ((RAND_MAX + 1) / (r)))

class SRV
{
private:
  unsigned int weight;
  unsigned int port;
  unsigned int priority;
  unsigned int ttl;
  char host[MAXDNAME];

public:
  LINK(SRV, link);
  SRV():weight(0), port(0), priority(0), ttl(0)
  {
    memset(host, 0, MAXDNAME);
  }

  unsigned int getWeight()
  {
    return weight;
  }
  unsigned int getPriority() const
  {
    return priority;
  }
  unsigned int getPort()
  {
    return port;
  }
  unsigned int getTTL()
  {
    return ttl;
  }
  char *getHost()
  {
    return &host[0];
  }

  void setWeight(int w)
  {
    weight = w;
  }
  void setTTL(int t)
  {
    ttl = t;
  }
  void setPort(int p)
  {
    port = p;
  }
  void setPriority(int p)
  {
    priority = p;
  }
  void setHost(const char *h)
  {
    if (!h) {
      Debug("dns_srv", "SRV::setHost() was passed a NULL host -- better check your code)");
      host[0] = '\0';
      return;
    }
    if (*h == '\0') {
      Debug("dns_srv", "SRV::setHost() was passed a blank host -- better check what might have happened.");
      host[0] = '\0';
      return;
    }
    ink_strlcpy(host, h, sizeof(host));
  }

};

TS_INLINE bool
operator<(const SRV & left, const SRV & right)
{
  return (left.getPriority() < right.getPriority());    /* lower priorities first :) */
}

extern ClassAllocator<SRV> SRVAllocator;

class SRVHosts
{
private:
  SortableQueue<SRV> hosts;
  int srv_host_count;

public:
   ~SRVHosts()
  {
    SRV *i;
    while ((i = hosts.dequeue())) {
      Debug("dns_srv", "freeing srv entry inside SRVHosts::~SRVHosts");
      SRVAllocator.free(i);
    }
  }

  SRVHosts():srv_host_count(0)
  {
    hosts.clear();
  }

  SortableQueue<SRV> *getHosts() {
    return &hosts;
  }

  void getWeightedHost(char *);

  bool insert(SRV * rec)
  {
    hosts.enqueue(rec);
    srv_host_count++;
    return true;
  }

  int getCount()
  {
    return srv_host_count;
  }

  /* convert this HostDBInfo to an SRVHosts */
  SRVHosts(HostDBInfo * info);

};

#endif
