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

#ifndef _COMPLETION_UTIL_H_
#define _COMPLETION_UTIL_H_
// interface
class completionUtil
{
public:
  static Event *create();
  static void destroy(Event *e);
  static void setThread(Event *e, EThread *t);
  static void setContinuation(Event *e, Continuation *c);
  static void *getHandle(Event *e);
  static void setHandle(Event *e, void *handle);
  static void setInfo(Event *e, int fd, const Ptr<IOBufferBlock> &buf, int actual, int errno_);
  static void setInfo(Event *e, int fd, struct msghdr *msg, int actual, int errno_);
  static int getBytesTransferred(Event *e);
  static IOBufferBlock *getIOBufferBlock(Event *e);
  static Continuation *getContinuation(Event *e);
  static int getError(Event *e);
  static void releaseReferences(Event *e);
};

#include "P_UnixCompletionUtil.h"

#endif
