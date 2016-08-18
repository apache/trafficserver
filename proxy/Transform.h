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

#ifndef __TRANSFORM_H__
#define __TRANSFORM_H__

#include "P_EventSystem.h"
#include "HTTP.h"
#include "InkAPIInternal.h"

#define TRANSFORM_READ_READY (TRANSFORM_EVENTS_START + 0)

typedef struct _RangeRecord {
  _RangeRecord() : _start(-1), _end(-1), _done_byte(-1) {}
  int64_t _start;
  int64_t _end;
  int64_t _done_byte;
} RangeRecord;

class TransformProcessor
{
public:
  void start();

public:
  VConnection *open(Continuation *cont, APIHook *hooks);
  INKVConnInternal *null_transform(ProxyMutex *mutex);
  INKVConnInternal *range_transform(ProxyMutex *mutex, RangeRecord *ranges, int, HTTPHdr *, const char *content_type,
                                    int content_type_len, int64_t content_length);
};

#ifdef TS_HAS_TESTS
class TransformTest
{
public:
  static void run();
};
#endif

/** A protocol class.
    This provides transform VC specific methods for external access
    without exposing internals or requiring extra includes.
*/
class TransformVCChain : public VConnection
{
protected:
  /// Required constructor
  TransformVCChain(ProxyMutex *m);

public:
  /** Compute the backlog.  This is the amount of data ready to read
      for each element of the chain.  If @a limit is non-negative then
      the method will return as soon as the computed backlog is at
      least that large. This provides for more efficient checking if
      the caller is interested only in whether the backlog is at least
      @a limit. The default is to accurately compute the backlog.
  */
  virtual uint64_t backlog(uint64_t limit = UINT64_MAX ///< Maximum value of interest
                           ) = 0;
};

inline TransformVCChain::TransformVCChain(ProxyMutex *m) : VConnection(m)
{
}

///////////////////////////////////////////////////////////////////
/// RangeTransform implementation
/// handling Range requests from clients
///////////////////////////////////////////////////////////////////

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline static int
num_chars_for_int(int64_t i)
{
  int k = 1;

  if (i < 0)
    return 0;

  while ((i /= 10) != 0)
    ++k;

  return k;
}

extern TransformProcessor transformProcessor;

#endif /* __TRANSFORM_H__ */
