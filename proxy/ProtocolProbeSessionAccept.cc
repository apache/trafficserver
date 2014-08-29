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
#include "http2/HTTP2.h"

static bool
proto_is_spdy(IOBufferReader * reader)
{
  // SPDY clients have to start by sending a control frame (the high bit is set). Let's assume
  // that no other protocol could possibly ever set this bit!
  return ((uint8_t)(*reader)[0]) == 0x80u;
}

static bool
proto_is_http2(IOBufferReader * reader)
{
  char buf[HTTP2_CONNECTION_PREFACE_LEN];
  char * end;
  ptrdiff_t nbytes;

  end = reader->memcpy(buf, sizeof(buf), 0 /* offset */);
  nbytes = end - buf;

  // Client must send at least 4 bytes to get a reasonable match.
  if (nbytes < 4) {
    return false;
  }

  ink_assert(nbytes <= (int64_t)HTTP2_CONNECTION_PREFACE_LEN);
  return memcmp(HTTP2_CONNECTION_PREFACE, buf, nbytes) == 0;
}

struct ProtocolProbeTrampoline : public Continuation, public ProtocolProbeSessionAcceptEnums
{
  static const size_t minimum_read_size = 1;
  static const unsigned buffer_size_index = CLIENT_CONNECTION_FIRST_READ_BUFFER_SIZE_INDEX;
  IOBufferReader *  reader;

  explicit
  ProtocolProbeTrampoline(const ProtocolProbeSessionAccept * probe, ProxyMutex * mutex)
    : Continuation(mutex), probeParent(probe)
  {
    this->iobuf = new_MIOBuffer(buffer_size_index);
    reader = iobuf->alloc_reader(); // reader must be allocated only on a new MIOBuffer.
    SET_HANDLER(&ProtocolProbeTrampoline::ioCompletionEvent);
  }

  int ioCompletionEvent(int event, void * edata)
  {
    VIO *             vio;
    NetVConnection *  netvc;
    ProtoGroupKey  key = N_PROTO_GROUPS; // use this as an invalid value.

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

    ink_assert(netvc != NULL);

    if (!reader->is_read_avail_more_than(minimum_read_size - 1)) {
      // Not enough data read. Well, that sucks.
      netvc->do_io_close();
      goto done;
    }

    // SPDY clients have to start by sending a control frame (the high bit is set). Let's assume
    // that no other protocol could possibly ever set this bit!
    if (proto_is_spdy(reader)) {
      key = PROTO_SPDY;
    } else if (proto_is_http2(reader)) {
      key = PROTO_HTTP2;
    } else {
      key = PROTO_HTTP;
    }

    netvc->do_io_read(this, 0, NULL); // Disable the read IO that we started.

    if (probeParent->endpoint[key] == NULL) {
      Warning("Unregistered protocol type %d", key);
      netvc->do_io_close();
      goto done;
    }

    // Directly invoke the session acceptor, letting it take ownership of the input buffer.
    probeParent->endpoint[key]->accept(netvc, this->iobuf, reader);
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
    ProtocolProbeTrampoline * probe = new ProtocolProbeTrampoline(this, netvc->mutex);

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
ProtocolProbeSessionAccept::registerEndpoint(ProtoGroupKey key, SessionAccept * ap)
{
  ink_release_assert(endpoint[key] == NULL);
  this->endpoint[key] = ap;
}
