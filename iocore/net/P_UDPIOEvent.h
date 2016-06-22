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

#ifndef _UDP_IO_EVENT_H_
#define _UDP_IO_EVENT_H_
// ugly -- just encapsulate the I/O result so that it can be passed
// back to the caller via continuation handler.

class UDPIOEvent : public Event
{
public:
  UDPIOEvent() : fd(-1), err(0), m(0), handle(0), b(0), bytesTransferred(0){};
  ~UDPIOEvent(){};
  void
  setInfo(int fd_, IOBufferBlock *b_, int bytesTransferred_, int errno_)
  {
    fd               = fd_;
    b                = b_;
    bytesTransferred = bytesTransferred_;
    err              = errno_;
  };
  void
  setInfo(int fd_, struct msghdr *m_, int bytesTransferred_, int errno_)
  {
    fd               = fd_;
    m                = m_;
    bytesTransferred = bytesTransferred_;
    err              = errno_;
  };
  void
  setHandle(void *v)
  {
    handle = v;
  }
  void *
  getHandle()
  {
    return handle;
  }
  void free();
  int
  getBytesTransferred()
  {
    return bytesTransferred;
  }
  IOBufferBlock *
  getIOBufferBlock()
  {
    return b;
  }
  int
  getError()
  {
    return err;
  }
  Continuation *
  getContinuation()
  {
    return continuation;
  }
  static void free(UDPIOEvent *e);

private:
  void *operator new(size_t size); // undefined
  int fd;
  int err; // error code
  struct msghdr *m;
  void *handle;         // some extra data for the client handler
  Ptr<IOBufferBlock> b; // holds buffer that I/O will go to
  int bytesTransferred; // actual bytes transferred
};

extern ClassAllocator<UDPIOEvent> UDPIOEventAllocator;
TS_INLINE void
UDPIOEvent::free(UDPIOEvent *e)
{
  e->b     = NULL;
  e->mutex = NULL;
  UDPIOEventAllocator.free(e);
}
#endif
