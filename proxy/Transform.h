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


#define TRANSFORM_READ_READY   (TRANSFORM_EVENTS_START + 0)


class TransformProcessor
{
public:
  void start();

public:
  VConnection * open(Continuation * cont, APIHook * hooks);
  INKVConnInternal *null_transform(ProxyMutex * mutex);
  INKVConnInternal *range_transform(ProxyMutex * mutex, MIMEField * range_field, HTTPInfo * cache_obj,
                                    HTTPHdr * transform_resp, bool & b);
};


#ifdef TS_HAS_TESTS
class TransformTest
{
public:
  static void run();
};
#endif


extern TransformProcessor transformProcessor;


#endif /* __TRANSFORM_H__ */

