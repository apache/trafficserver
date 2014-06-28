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

/****************************************************************************

  ClusterLib.h


****************************************************************************/

#ifndef _P_ClusterLib_h
#define _P_ClusterLib_h

#include "P_Cluster.h"

extern void cluster_set_priority(ClusterHandler *, ClusterVConnState *, int);
extern void cluster_lower_priority(ClusterHandler *, ClusterVConnState *);
extern void cluster_raise_priority(ClusterHandler *, ClusterVConnState *);
extern void cluster_schedule(ClusterHandler *, ClusterVConnection *, ClusterVConnState *);
extern void cluster_reschedule(ClusterHandler *, ClusterVConnection *, ClusterVConnState *);
extern void cluster_disable(ClusterHandler *, ClusterVConnection *, ClusterVConnState *);
extern void cluster_update_priority(ClusterHandler *, ClusterVConnection *, ClusterVConnState *, int64_t, int64_t);
extern void cluster_bump(ClusterHandler *, ClusterVConnectionBase *, ClusterVConnState *, int);

#if TEST_PARTIAL_READS
extern int partial_readv(int, IOVec *, int, int);
#endif

#if TEST_PARTIAL_WRITES
extern int partial_writev(int, IOVec *, int, int);
#endif

extern void dump_time_buckets();

struct GlobalClusterPeriodicEvent;
typedef int (GlobalClusterPeriodicEvent::*GClusterPEHandler) (int, void *);

struct GlobalClusterPeriodicEvent:public Continuation
{
  GlobalClusterPeriodicEvent();
  ~GlobalClusterPeriodicEvent();
  void init();
  int calloutEvent(Event * e, void *data);

  // Private data
  Event *_thisCallout;
};

#endif /* _ClusterLib_h */

// End of ClusterLib.h
