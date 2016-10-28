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

#include "P_Net.h"

Diags *diags;
struct NetTesterSM : public Continuation {
  VIO *read_vio;
  IOBufferReader *reader;
  NetVConnection *vc;
  MIOBuffer *buf;

  NetTesterSM(ProxyMutex *_mutex, NetVConnection *_vc) : Continuation(_mutex)
  {
    MUTEX_TRY_LOCK(lock, mutex, _vc->thread);
    ink_release_assert(lock);
    vc = _vc;
    SET_HANDLER(&NetTesterSM::handle_read);
    buf      = new_MIOBuffer(8);
    reader   = buf->alloc_reader();
    read_vio = vc->do_io_read(this, INT64_MAX, buf);
  }

  int
  handle_read(int event, void *data)
  {
    int r;
    char *str = nullptr;
    switch (event) {
    case VC_EVENT_READ_READY:
      r   = reader->read_avail();
      str = new char[r + 10];
      reader->read(str, r);
      printf("%s", str);
      fflush(stdout);
      break;
    case VC_EVENT_READ_COMPLETE:
    /* FALLSTHROUGH */
    case VC_EVENT_EOS:
      r   = reader->read_avail();
      str = new char[r + 10];
      reader->read(str, r);
      printf("%s", str);
      fflush(stdout);
    case VC_EVENT_ERROR:
      vc->do_io_close();
      break;
    default:
      ink_release_assert(!"unknown event");
    }
    delete[] str;
    return EVENT_CONT;
  }
};

struct NetTesterAccept : public Continuation {
  NetTesterAccept(ProxyMutex *_mutex) : Continuation(_mutex) { SET_HANDLER(&NetTesterAccept::handle_accept); }
  int
  handle_accept(int event, void *data)
  {
    printf("Accepted a connection\n");
    fflush(stdout);
    NetVConnection *vc = (NetVConnection *)data;
    new NetTesterSM(new_ProxyMutex(), vc);
    return EVENT_CONT;
  }
};

int
main()
{
  ink_event_system_init(EVENT_SYSTEM_MODULE_VERSION);
  MIOBuffer *mbuf = new_MIOBuffer(5);
  eventProcessor.start(1);
  netProcessor.start();
  netProcessor.accept(new NetTesterAccept(new_ProxyMutex()), 8080, true);
  this_thread()->execute();
}
