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
 *  ParentRoundRobin.h - Implementation of various round robin strategies.
 *
 *****************************************************************************/

#ifndef _PARENT_ROUND_ROBIN_H
#define _PARENT_ROUND_ROBIN_H

#include "ParentSelection.h"

class ParentRoundRobin : public ParentSelectionStrategy
{
  ParentRR_t round_robin_type;
  int latched_parent;
  pRecord *parents;
  int num_parents;

public:
  ParentRoundRobin(ParentRecord *_parent_record, ParentRR_t _round_robin_type);
  ~ParentRoundRobin();
  pRecord *
  getParents(ParentResult *result)
  {
    return parents;
  }
  void selectParent(bool firstCall, ParentResult *result, RequestData *rdata, unsigned int fail_threshold, unsigned int retry_time);
  uint32_t numParents(ParentResult *result) const;
};

#endif
