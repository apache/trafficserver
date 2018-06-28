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

/*****************************************************************************
 *
 *  ParentConsistentHash.h - Interface to Parent Consistent Hash.
 *
 ****************************************************************************/

#pragma once

#include "ts/HashSip.h"
#include "ParentSelection.h"

//
//  Implementation of round robin based upon consistent hash of the URL,
//  ParentRR_t = P_CONSISTENT_HASH.
//
class ParentConsistentHash : public ParentSelectionStrategy
{
  // there are two hashes PRIMARY parents
  // and SECONDARY parents.
  ATSHash64Sip24 hash[2];
  ATSConsistentHash *chash[2];
  pRecord *parents[2];
  bool foundParents[2][MAX_PARENTS];
  bool ignore_query;
  int secondary_mode;

public:
  static const int PRIMARY   = 0;
  static const int SECONDARY = 1;
  ParentConsistentHash(ParentRecord *_parent_record);
  ~ParentConsistentHash() override;
  pRecord *
  getParents(ParentResult *result) override
  {
    return parents[result->last_lookup];
  }
  uint64_t getPathHash(HttpRequestData *hrdata, ATSHash64 *h);
  void selectParent(bool firstCall, ParentResult *result, RequestData *rdata, unsigned int fail_threshold,
                    unsigned int retry_time) override;
  void markParentDown(ParentResult *result, unsigned int fail_threshold, unsigned int retry_time);
  uint32_t numParents(ParentResult *result) const override;
  void markParentUp(ParentResult *result);
};
