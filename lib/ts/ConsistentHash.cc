/** @file

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

#include "ConsistentHash.h"
#include <cstring>
#include <string>
#include <sstream>
#include <cmath>
#include <climits>
#include <cstdio>

std::ostream &
operator<<(std::ostream &os, ATSConsistentHashNode &thing)
{
  return os << thing.name;
}

ATSConsistentHash::ATSConsistentHash(int r, ATSHash64 *h) : replicas(r), hash(h)
{
}

void
ATSConsistentHash::insert(ATSConsistentHashNode *node, float weight, ATSHash64 *h)
{
  int i;
  char numstr[256];
  ATSHash64 *thash;
  std::ostringstream string_stream;
  std::string std_string;

  if (h) {
    thash = h;
  } else if (hash) {
    thash = hash;
  } else {
    return;
  }

  string_stream << *node;
  std_string = string_stream.str();

  for (i = 0; i < (int)roundf(replicas * weight); i++) {
    snprintf(numstr, 256, "%d-", i);
    thash->update(numstr, strlen(numstr));
    thash->update(std_string.c_str(), strlen(std_string.c_str()));
    thash->final();
    NodeMap.insert(std::pair<uint64_t, ATSConsistentHashNode *>(thash->get(), node));
    thash->clear();
  }
}

ATSConsistentHashNode *
ATSConsistentHash::lookup(const char *url, ATSConsistentHashIter *i, bool *w, ATSHash64 *h)
{
  uint64_t url_hash;
  ATSConsistentHashIter NodeMapIterUp, *iter;
  ATSHash64 *thash;
  bool *wptr, wrapped = false;

  if (h) {
    thash = h;
  } else if (hash) {
    thash = hash;
  } else {
    return NULL;
  }

  if (w) {
    wptr = w;
  } else {
    wptr = &wrapped;
  }

  if (i) {
    iter = i;
  } else {
    iter = &NodeMapIterUp;
  }

  if (url) {
    thash->update(url, strlen(url));
    thash->final();
    url_hash = thash->get();
    thash->clear();

    *iter = NodeMap.lower_bound(url_hash);

    if (*iter == NodeMap.end()) {
      *wptr = true;
      *iter = NodeMap.begin();
    }
  } else {
    (*iter)++;
  }

  if (!(*wptr) && *iter == NodeMap.end()) {
    *wptr = true;
    *iter = NodeMap.begin();
  }

  if (*wptr && *iter == NodeMap.end()) {
    return NULL;
  }

  return (*iter)->second;
}

ATSConsistentHashNode *
ATSConsistentHash::lookup_available(const char *url, ATSConsistentHashIter *i, bool *w, ATSHash64 *h)
{
  uint64_t url_hash;
  ATSConsistentHashIter NodeMapIterUp, *iter;
  ATSHash64 *thash;
  bool *wptr, wrapped = false;

  if (h) {
    thash = h;
  } else if (hash) {
    thash = hash;
  } else {
    return NULL;
  }

  if (w) {
    wptr = w;
  } else {
    wptr = &wrapped;
  }

  if (i) {
    iter = i;
  } else {
    iter = &NodeMapIterUp;
  }

  if (url) {
    thash->update(url, strlen(url));
    thash->final();
    url_hash = thash->get();
    thash->clear();

    *iter = NodeMap.lower_bound(url_hash);
  }

  if (*iter == NodeMap.end()) {
    *wptr = true;
    *iter = NodeMap.begin();
  }

  while (!(*iter)->second->available) {
    (*iter)++;

    if (!(*wptr) && *iter == NodeMap.end()) {
      *wptr = true;
      *iter = NodeMap.begin();
    } else if (*wptr && *iter == NodeMap.end()) {
      return NULL;
    }
  }

  return (*iter)->second;
}

ATSConsistentHashNode *
ATSConsistentHash::lookup_by_hashval(uint64_t hashval, ATSConsistentHashIter *i, bool *w)
{
  ATSConsistentHashIter NodeMapIterUp, *iter;
  bool *wptr, wrapped = false;

  if (w) {
    wptr = w;
  } else {
    wptr = &wrapped;
  }

  if (i) {
    iter = i;
  } else {
    iter = &NodeMapIterUp;
  }

  *iter = NodeMap.lower_bound(hashval);

  if (*iter == NodeMap.end()) {
    *wptr = true;
    *iter = NodeMap.begin();
  }

  return (*iter)->second;
}

ATSConsistentHash::~ATSConsistentHash()
{
  if (hash) {
    delete hash;
  }
}
