/** @file

  ProtocolProbeSessionAccept

  @par License

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

#include "../iocore/net/P_UnixNetVConnection.h"
#include "iocore/utils/Machine.h"
#include "proxy/ProtocolProbeSessionAccept.h"
#include "proxy/http2/HTTP2.h"
#include "iocore/net/ProxyProtocol.h"
#include "iocore/net/NetVConnection.h"
#include "proxy/http/HttpConfig.h"

#include <cstring>
#include <string>

namespace
{
bool
proto_is_http2(IOBufferReader *reader)
{
  char      buf[HTTP2_CONNECTION_PREFACE_LEN];
  char     *end;
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

/** Render @a addr for a diagnostic message.

    This is the IP address for IP sockets and "unix:<path>" for Unix domain sockets. A client's Unix socket is usually
    unbound, so its path is reported as "(unnamed)".
 */
std::string
addr_to_text(sockaddr const *addr)
{
  if (ats_is_ip(addr)) {
    ip_text_buffer buf;
    return ats_ip_ntop(addr, buf, sizeof(buf));
  }
  if (addr->sa_family == AF_UNIX) {
    char const  *path = ats_unix_cast(addr)->sun_path;
    size_t const len  = strnlen(path, TS_UNIX_SIZE);
    return len == 0 ? std::string{"unix:(unnamed)"} : "unix:" + std::string{path, len};
  }
  return "unknown";
}

/** Warn that the probe is closing @a netvc because its port has no acceptor for the protocol the client used.

    @a key is the protocol group the probe selected. @a h2_preface_on_tls is true if the client sent the HTTP/2
    connection preface over TLS without negotiating h2 via ALPN, which the probe handles as HTTP/1.x.
 */
void
warn_unregistered_protocol(NetVConnection *netvc, ProtocolProbeSessionAcceptEnums::ProtoGroupKey key, bool h2_preface_on_tls)
{
  // Behind a PROXY protocol load balancer the TCP peer is the balancer, so name the client the header reports.
  std::string client = addr_to_text(netvc->get_remote_addr());
  if (netvc->get_proxy_protocol_version() != ProxyProtocolVersion::UNDEFINED) {
    if (sockaddr const *pp_src = netvc->get_proxy_protocol_src_addr(); pp_src != nullptr) {
      client = addr_to_text(pp_src) + " (peer " + client + ")";
    }
  }

  sockaddr const *local = netvc->get_local_addr();
  std::string     listener =
    ats_is_ip(local) ? "local port " + std::to_string(ats_ip_port_host_order(local)) : "listener " + addr_to_text(local);

  char const *detected = nullptr;
  char const *advice   = "Check the protocols for this port in proxy.config.http.server_ports (proto=...).";
  if (key == ProtocolProbeSessionAcceptEnums::ProtoGroupKey::HTTP2) {
    detected = "sent an HTTP/2 connection preface (h2c prior knowledge), but this port does not enable HTTP/2";
  } else if (h2_preface_on_tls) {
    // Enabling HTTP/1.x would not help: the client must negotiate h2 via ALPN instead.
    detected = "sent an HTTP/2 connection preface over TLS without negotiating h2 via ALPN, which RFC 9113 forbids, so it was "
               "handled as HTTP/1.x, which this port does not enable";
    advice   = "The client must negotiate h2 via ALPN.";
  } else {
    detected = "did not send an HTTP/2 connection preface, so it was handled as HTTP/1.x, which this port does not enable";
  }

  Warning("Closing connection from client_ip=%s on %s: the client %s. %s", client.c_str(), listener.c_str(), detected, advice);
}

DbgCtl dbg_ctl_proxyprotocol{"proxyprotocol"};
DbgCtl dbg_ctl_http{"http"};

} // end anonymous namespace

struct ProtocolProbeTrampoline : public Continuation, public ProtocolProbeSessionAcceptEnums {
  static const size_t   minimum_read_size = 1;
  static const unsigned buffer_size_index = BUFFER_SIZE_INDEX_4K;
  IOBufferReader       *reader;

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
    VIO            *vio;
    NetVConnection *netvc;
    SessionAccept  *acceptor          = nullptr;
    ProtoGroupKey   key               = ProtoGroupKey::N_GROUPS; // use this as an invalid value.
    bool            h2_preface_on_tls = false;

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
    // the trusted allowlist for proxy protocol, then check to see if it is
    // present

    // Haha, can't do @c auto because the @c goto won't work across it.
    swoc::IPRangeSet *pp_ipmap;
    pp_ipmap = probeParent->proxy_protocol_ipmap;

    if (netvc->get_is_proxy_protocol() && netvc->get_proxy_protocol_version() == ProxyProtocolVersion::UNDEFINED) {
      Dbg(dbg_ctl_proxyprotocol, "ioCompletionEvent: proxy protocol is enabled on this port");
      if (netvc->has_proxy_protocol_preface(reader)) {
        if (pp_ipmap->count() > 0) {
          Dbg(dbg_ctl_proxyprotocol, "ioCompletionEvent: proxy protocol has a configured allowlist of trusted IPs - checking");
          if (!pp_ipmap->contains(swoc::IPAddr(netvc->get_remote_addr()))) {
            Dbg(dbg_ctl_proxyprotocol,
                "ioCompletionEvent: Source IP is NOT in the configured allowlist of trusted IPs - closing connection");
            goto done;
          } else {
            char new_host[INET6_ADDRSTRLEN];
            Dbg(dbg_ctl_proxyprotocol, "ioCompletionEvent: Source IP [%s] is trusted in the allowlist for proxy protocol",
                ats_ip_ntop(netvc->get_remote_addr(), new_host, sizeof(new_host)));
          }
        } else {
          Dbg(dbg_ctl_proxyprotocol,
              "ioCompletionEvent: proxy protocol DOES NOT have a configured allowlist of trusted IPs but proxy protocol is "
              "enabled on this port - processing all connections with Proxy Protocol headers");
        }

        HttpConfigParams *param           = HttpConfig::acquire();
        int               max_header_size = param->pp_hdr_max_size;
        HttpConfig::release(param);
        if (netvc->has_proxy_protocol(reader, max_header_size)) {
          Dbg(dbg_ctl_proxyprotocol, "ioCompletionEvent: http has proxy protocol header");
        } else {
          Dbg(dbg_ctl_proxyprotocol,
              "ioCompletionEvent: proxy protocol preface was present, but Proxy Protocol header could not be parsed");
        }
      } else {
        Dbg(dbg_ctl_proxyprotocol, "ioCompletionEvent: proxy protocol was enabled, but Proxy Protocol header was not present");
      }
    } // end of Proxy Protocol processing

    if (proto_is_http2(reader)) {
      if (netvc->get_service<TLSBasicSupport>() == nullptr) {
        key = ProtoGroupKey::HTTP2;
      } else {
        // RFC 9113 Section 3.3: Prior knowledge is only permissible for HTTP/2 over plaintext (non-TLS) connections.
        Dbg(dbg_ctl_http, "HTTP/2 prior knowledge was used on a TLS connection (protocol violation). Selecting HTTP/1 instead.");
        key               = ProtoGroupKey::HTTP;
        h2_preface_on_tls = true;
      }
    } else {
      key = ProtoGroupKey::HTTP;
    }

    acceptor = probeParent->endpoint[static_cast<int>(key)];
    if (acceptor == nullptr) {
      warn_unregistered_protocol(netvc, key, h2_preface_on_tls);
      goto done;
    }

    // Disable the read IO that we started.
    netvc->do_io_read(acceptor, 0, nullptr);

    // Directly invoke the session acceptor, letting it take ownership of the input buffer.
    if (!acceptor->accept(netvc, this->iobuf, reader)) {
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

  MIOBuffer                        *iobuf;
  const ProtocolProbeSessionAccept *probeParent;
};

int
ProtocolProbeSessionAccept::mainEvent(int event, void *data)
{
  if (event == NET_EVENT_ACCEPT) {
    ink_assert(data);

    VIO                     *vio;
    NetVConnection          *netvc = static_cast<NetVConnection *>(data);
    ProtocolProbeTrampoline *probe;
    UnixNetVConnection      *unix_netvc = dynamic_cast<UnixNetVConnection *>(netvc);
    if (unix_netvc != nullptr && unix_netvc->read.vio.get_writer() != nullptr) {
      probe = new ProtocolProbeTrampoline(this, netvc->mutex, unix_netvc->read.vio.get_writer(), unix_netvc->read.vio.get_reader());
    } else {
      probe = new ProtocolProbeTrampoline(this, netvc->mutex, nullptr, nullptr);
    }

    // The connection has completed, set the accept inactivity timeout here to watch over the difference between the
    // connection set up and the first transaction..
    HttpConfigParams *param = HttpConfig::acquire();
    netvc->set_inactivity_timeout(HRTIME_SECONDS(param->accept_no_activity_timeout));
    HttpConfig::release(param);

    if (!probe->reader->is_read_avail_more_than(0)) {
      Dbg(dbg_ctl_http, "probe needs data, read..");
      vio = netvc->do_io_read(probe, BUFFER_SIZE_FOR_INDEX(ProtocolProbeTrampoline::buffer_size_index), probe->iobuf);
      vio->reenable();
    } else {
      Dbg(dbg_ctl_http, "probe already has data, call ioComplete directly..");
      vio = netvc->do_io_read(this, 0, nullptr);
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
  ink_release_assert(endpoint[static_cast<int>(key)] == nullptr);
  this->endpoint[static_cast<int>(key)] = ap;
}
