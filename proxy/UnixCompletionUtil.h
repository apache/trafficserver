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

#ifndef _UNIX_COMPLETION_UTIL_H_
#define _UNIX_COMPLETION_UTIL_H_

// platform specific wrappers for dealing with I/O completion events
// passed into and back from the I/O core.
#include "UDPIOEvent.h"

inline Event *
completionUtil::create()
{
  UDPIOEvent *u = UDPIOEventAllocator.alloc();
  return u;
};
inline void
completionUtil::destroy(Event *e)
{
  ink_assert(e != NULL);
  UDPIOEvent *u = (UDPIOEvent *)e;
  UDPIOEvent::free(u);
};
inline void
completionUtil::setThread(Event *e, EThread *t)
{
  UDPIOEvent *u = (UDPIOEvent *)e;
  u->ethread    = t;
}
inline void
completionUtil::setContinuation(Event *e, Continuation *c)
{
  UDPIOEvent *u = (UDPIOEvent *)e;
  *(Action *)u  = c;
}
inline void *
completionUtil::getHandle(Event *e)
{
  UDPIOEvent *u = (UDPIOEvent *)e;
  return u->getHandle();
}
inline void
completionUtil::setHandle(Event *e, void *handle)
{
  UDPIOEvent *u = (UDPIOEvent *)e;
  u->setHandle(handle);
}
inline void
completionUtil::setInfo(Event *e, int fd, IOBufferBlock *buf, int actual, int errno_)
{
  UDPIOEvent *u = (UDPIOEvent *)e;
  u->setInfo(fd, buf, actual, errno_);
}
inline void
completionUtil::setInfo(Event *e, int fd, struct msghdr *msg, int actual, int errno_)
{
  UDPIOEvent *u = (UDPIOEvent *)e;
  u->setInfo(fd, msg, actual, errno_);
}
inline int
completionUtil::getBytesTransferred(Event *e)
{
  UDPIOEvent *u = (UDPIOEvent *)e;
  return u->getBytesTransferred();
}
inline IOBufferBlock *
completionUtil::getIOBufferBlock(Event *e)
{
  UDPIOEvent *u = (UDPIOEvent *)e;
  return u->getIOBufferBlock();
}
inline Continuation *
completionUtil::getContinuation(Event *e)
{
  UDPIOEvent *u = (UDPIOEvent *)e;
  return u->getContinuation();
}
inline int
completionUtil::getError(Event *e)
{
  UDPIOEvent *u = (UDPIOEvent *)e;
  return u->getError();
}
#endif
