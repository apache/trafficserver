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

#ifndef __CONSISTENT_HASH_H__
#define __CONSISTENT_HASH_H__

#include "Hash.h"
#include <stdint.h>
#include <iostream>
#include <map>

/*
  Helper class to be extended to make ring nodes.
 */

struct ATSConsistentHashNode {
  bool available;
  char *name;
};

std::ostream &operator<<(std::ostream &os, ATSConsistentHashNode &thing);

typedef std::map<uint64_t, ATSConsistentHashNode *>::iterator ATSConsistentHashIter;

/*
  TSConsistentHash requires a TSHash64 object

  Caller is responsible for freeing ring node memory.
 */

struct ATSConsistentHash {
  ATSConsistentHash(int r = 1024, ATSHash64 *h = NULL);
  void insert(ATSConsistentHashNode *node, float weight = 1.0, ATSHash64 *h = NULL);
  ATSConsistentHashNode *lookup(const char *url = NULL, ATSConsistentHashIter *i = NULL, bool *w = NULL, ATSHash64 *h = NULL);
  ATSConsistentHashNode *lookup_available(const char *url = NULL, ATSConsistentHashIter *i = NULL, bool *w = NULL,
                                          ATSHash64 *h = NULL);
  ATSConsistentHashNode *lookup_by_hashval(uint64_t hashval, ATSConsistentHashIter *i = NULL, bool *w = NULL);
  ~ATSConsistentHash();

private:
  int replicas;
  ATSHash64 *hash;
  std::map<uint64_t, ATSConsistentHashNode *> NodeMap;
};

#endif
