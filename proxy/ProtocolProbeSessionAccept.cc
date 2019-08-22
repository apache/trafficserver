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
#include "http2/HTTP2.h"
#include "ProxyProtocol.h"
#include "I_NetVConnection.h"

static bool
proto_is_http2(IOBufferReader *reader)
{
  char buf[HTTP2_CONNECTION_PREFACE_LEN];
  char *end;
  ptrdiff_t nbytes;

  end    = reader->memcpy(buf, sizeof(buf), 0 /* offset */);
  nbytes = end - buf;

  // Client must send at least 4 bytes to get a reasonable match.
  if (nbytes < 4) {
    return false;
  }

  ink_assert(nbytes <= (int64_t)HTTP2_CONNECTION_PREFACE_LEN);
  return memcmp(HTTP2_CONNECTION_PREFACE, buf, nbytes) == 0;
}

struct ProtocolProbeTrampoline : public Continuation, public ProtocolProbeSessionAcceptEnums {
  static const size_t minimum_read_size   = 1;
  static const unsigned buffer_size_index = CLIENT_CONNECTION_FIRST_READ_BUFFER_SIZE_INDEX;
  IOBufferReader *reader;

  explicit ProtocolProbeTrampoline(const ProtocolProbeSessionAccept *probe, Ptr<ProxyMutex> &mutex, MIOBuffer *buffer,
                                   IOBufferReader *reader)
    : Continuation(mutex), probeParent(probe)
  {
    this->iobuf  = buffer ? buffer : new_MIOBuffer(buffer_size_index);
    this->reader = reader ? reader : iobuf->alloc_reader(); // reader must be allocated only on a new MIOBuffer.
    SET_HANDLER(&ProtocolProbeTrampoline::ioCompletionEvent);
  }

  int
  ioCompletionEvent(int event, void *edata)
  {
    VIO *vio;
    NetVConnection *netvc;
    ProtoGroupKey key = N_PROTO_GROUPS; // use this as an invalid value.

    vio   = static_cast<VIO *>(edata);
    netvc = static_cast<NetVConnection *>(vio->vc_server);

    switch (event) {
    case VC_EVENT_EOS:
    case VC_EVENT_ERROR:
    case VC_EVENT_ACTIVE_TIMEOUT:
    case VC_EVENT_INACTIVITY_TIMEOUT:
      // Error ....
      goto done;
    case VC_EVENT_READ_READY:
    case VC_EVENT_READ_COMPLETE:
      break;
    default:
      return EVENT_ERROR;
    }

    ink_assert(netvc != nullptr);

    if (!reader->is_read_avail_more_than(minimum_read_size - 1)) {
      // Not enough data read. Well, that sucks.
      goto done;
    }

    // if proxy_protocol is enabled via port descriptor AND the src IP is in
    // the trusted whitelist for proxy protocol, then check to see if it is
    // present

    IpMap *pp_ipmap;
    pp_ipmap = probeParent->proxy_protocol_ipmap;

    if (netvc->get_is_proxy_protocol()) {
      Debug("proxyprotocol", "ioCompletionEvent: proxy protocol is enabled on this port");
      if (pp_ipmap->count() > 0) {
        Debug("proxyprotocol", "ioCompletionEvent: proxy protocol has a configured whitelist of trusted IPs - checking");
        void *payload = nullptr;
        if (!pp_ipmap->contains(netvc->get_remote_addr(), &payload)) {
          Debug("proxyprotocol",
                "ioCompletionEvent: proxy protocol src IP is NOT in the configured whitelist of trusted IPs - closing connection");
          goto done;
        } else {
          char new_host[INET6_ADDRSTRLEN];
          Debug("proxyprotocol", "ioCompletionEvent: Source IP [%s] is trusted in the whitelist for proxy protocol",
                ats_ip_ntop(netvc->get_remote_addr(), new_host, sizeof(new_host)));
        }
      } else {
        Debug("proxyprotocol",
              "ioCompletionEvent: proxy protocol DOES NOT have a configured whitelist of trusted IPs but proxy protocol is "
              "ernabled on this port - processing all connections");
      }

      if (http_has_proxy_v1(reader, netvc)) {
        Debug("proxyprotocol", "ioCompletionEvent: http has proxy_v1 header");
        netvc->set_remote_addr(netvc->get_proxy_protocol_src_addr());
      } else {
        Debug("proxyprotocol",
              "ioCompletionEvent: proxy protocol was enabled, but required header was not present in the transaction - "
              "closing connection");
        goto done;
      }
    } // end of Proxy Protocol processing

    if (proto_is_http2(reader)) {
      key = PROTO_HTTP2;
    } else {
      key = PROTO_HTTP;
    }

    netvc->do_io_read(nullptr, 0, nullptr); // Disable the read IO that we started.

    if (probeParent->endpoint[key] == nullptr) {
      Warning("Unregistered protocol type %d", key);
      goto done;
    }

    // Directly invoke the session acceptor, letting it take ownership of the input buffer.
    if (!probeParent->endpoint[key]->accept(netvc, this->iobuf, reader)) {
      // IPAllow check fails in XxxSessionAccept::accept() if false returned.
      goto done;
    }
    delete this;
    return EVENT_CONT;

  done:
    netvc->do_io_close();
    free_MIOBuffer(this->iobuf);
    this->iobuf = nullptr;
    delete this;
    return EVENT_CONT;
  }

  MIOBuffer *iobuf;
  const ProtocolProbeSessionAccept *probeParent;
};

int
ProtocolProbeSessionAccept::mainEvent(int event, void *data)
{
  if (event == NET_EVENT_ACCEPT) {
    ink_assert(data);

    VIO *vio;
    NetVConnection *netvc          = static_cast<NetVConnection *>(data);
    ProtocolProbeTrampoline *probe = new ProtocolProbeTrampoline(this, netvc->mutex, nullptr, nullptr);

    // XXX we need to apply accept inactivity timeout here ...

    if (!probe->reader->is_read_avail_more_than(0)) {
      Debug("http", "probe needs data, read..");
      vio = netvc->do_io_read(probe, BUFFER_SIZE_FOR_INDEX(ProtocolProbeTrampoline::buffer_size_index), probe->iobuf);
      vio->reenable();
    } else {
      Debug("http", "probe already has data, call ioComplete directly..");
      vio = netvc->do_io_read(nullptr, 0, nullptr);
      probe->ioCompletionEvent(VC_EVENT_READ_COMPLETE, (void *)vio);
    }
    return EVENT_CONT;
  }

  ink_abort("Protocol probe received a fatal error: errno = %d", -(static_cast<int>((intptr_t)data)));
  return EVENT_CONT;
}

bool
ProtocolProbeSessionAccept::accept(NetVConnection *, MIOBuffer *, IOBufferReader *)
{
  ink_release_assert(0);
  return false;
}

void
ProtocolProbeSessionAccept::registerEndpoint(ProtoGroupKey key, SessionAccept *ap)
{
  ink_release_assert(endpoint[key] == nullptr);
  this->endpoint[key] = ap;
}
