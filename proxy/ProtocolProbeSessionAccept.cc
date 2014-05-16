/** @file

  ProtocolProbeSessionAccept

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
#include "I_Machine.h"
#include "ProtocolProbeSessionAccept.h"
#include "Error.h"

struct ProtocolProbeTrampoline : public Continuation
{
  static const size_t minimum_read_size = 1;
  static const unsigned buffer_size_index = CLIENT_CONNECTION_FIRST_READ_BUFFER_SIZE_INDEX;

  explicit
  ProtocolProbeTrampoline(const ProtocolProbeSessionAccept * probe, ProxyMutex * mutex)
    : Continuation(mutex), probeParent(probe)
  {
    this->iobuf = new_MIOBuffer(buffer_size_index);
    SET_HANDLER(&ProtocolProbeTrampoline::ioCompletionEvent);
  }

  int ioCompletionEvent(int event, void * edata)
  {
    VIO *             vio;
    IOBufferReader *  reader;
    NetVConnection *  netvc;
    TSProtoType       proto_type = TS_PROTO_NULL;

    vio = static_cast<VIO *>(edata);
    netvc = static_cast<NetVConnection *>(vio->vc_server);

    switch (event) {
    case VC_EVENT_EOS:
    case VC_EVENT_ERROR:
    case VC_EVENT_ACTIVE_TIMEOUT:
    case VC_EVENT_INACTIVITY_TIMEOUT:
      // Error ....
      netvc->do_io_close();
      goto done;
    case VC_EVENT_READ_READY:
    case VC_EVENT_READ_COMPLETE:
      break;
    default:
      return EVENT_ERROR;
    }

    reader = iobuf->alloc_reader();
    ink_assert(netvc != NULL);

    if (!reader->is_read_avail_more_than(minimum_read_size - 1)) {
      // Not enough data read. Well, that sucks.
      netvc->do_io_close();
      goto done;
    }

    // SPDY clients have to start by sending a control frame (the high bit is set). Let's assume
    // that no other protocol could possibly ever set this bit!
    if ((uint8_t)(*reader->start()) == 0x80u) {
      proto_type = TS_PROTO_SPDY;
    } else {
      proto_type = TS_PROTO_HTTP;
    }

    netvc->do_io_read(this, 0, NULL); // Disable the read IO that we started.
    netvc->proto_stack |= (1u << proto_type);

    if (probeParent->endpoint[proto_type] == NULL) {
      Warning("Unregistered protocol type %d", proto_type);
      netvc->do_io_close();
      goto done;
    }

    // Directly invoke the session acceptor, letting it take ownership of the input buffer.
    probeParent->endpoint[proto_type]->accept(netvc, this->iobuf, reader);
    delete this;
    return EVENT_CONT;

done:
    free_MIOBuffer(this->iobuf);
    delete this;
    return EVENT_CONT;
  }

  MIOBuffer * iobuf;
  const ProtocolProbeSessionAccept * probeParent;
};

int
ProtocolProbeSessionAccept::mainEvent(int event, void *data)
{
  if (event == NET_EVENT_ACCEPT) {
    ink_assert(data);

    VIO * vio;
    NetVConnection * netvc = static_cast<NetVConnection*>(data);
    ProtocolProbeTrampoline * probe = NEW(new ProtocolProbeTrampoline(this, netvc->mutex));

    // XXX we need to apply accept inactivity timeout here ...

    vio = netvc->do_io_read(probe,
		    BUFFER_SIZE_FOR_INDEX(ProtocolProbeTrampoline::buffer_size_index), probe->iobuf);
    vio->reenable();
    return EVENT_CONT;
  }

  MachineFatal("Protocol probe received a fatal error: errno = %d", -((int)(intptr_t)data));
  return EVENT_CONT;
}

void
ProtocolProbeSessionAccept::accept(NetVConnection *, MIOBuffer *, IOBufferReader *)
{
  ink_release_assert(0);
}

void
ProtocolProbeSessionAccept::registerEndpoint(TSProtoType proto_type, SessionAccept * ap)
{
  ink_release_assert(endpoint[proto_type] == NULL);
  this->endpoint[proto_type] = ap;
}
