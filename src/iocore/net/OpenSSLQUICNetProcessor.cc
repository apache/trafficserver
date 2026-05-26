/** @file

  OpenSSL native QUIC NetProcessor support.

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
#include "P_QUICNetProcessor.h"
#include "P_QUICPacketHandler.h"
#include "P_QUICNetVConnection.h"
#include "P_UnixNet.h"
#include "iocore/net/quic/QUICConfig.h"
#include "iocore/net/quic/QUICGlobals.h"
#include "iocore/net/QUICMultiCertConfigLoader.h"

#include <cerrno>

QUICNetProcessor quic_NetProcessor;

namespace
{
DbgCtl dbg_ctl_quic_ps{"quic_ps"};
DbgCtl dbg_ctl_iocore_net_processor{"iocore_net_processor"};
} // end anonymous namespace

QUICNetProcessor::QUICNetProcessor() {}

QUICNetProcessor::~QUICNetProcessor() {}

void
QUICNetProcessor::init()
{
}

int
QUICNetProcessor::start(int, size_t /* stacksize ATS_UNUSED */)
{
  QUIC::init();
  QUICConfig::startup();
  QUICCertConfig::startup();

  return 0;
}

NetAccept *
QUICNetProcessor::createNetAccept(NetProcessor::AcceptOptions const &opt)
{
  return new QUICPacketHandlerIn(opt);
}

NetVConnection *
QUICNetProcessor::allocate_vc(EThread *t)
{
  QUICNetVConnection *vc = nullptr;

  if (t) {
    vc = THREAD_ALLOC(quicNetVCAllocator, t);
    new (vc) QUICNetVConnection();
  } else if (likely(vc = quicNetVCAllocator.alloc())) {
    new (vc) QUICNetVConnection();
    vc->from_accept_thread = true;
  }

  if (vc != nullptr) {
    vc->ep.syscall = false;
  }

  return vc;
}

Action *
QUICNetProcessor::connect_re(Continuation *cont, sockaddr const * /* remote_addr ATS_UNUSED */,
                             NetVCOptions const & /* opt ATS_UNUSED */)
{
  Dbg(dbg_ctl_quic_ps, "OpenSSL native QUIC origin connections are not supported");
  cont->handleEvent(NET_EVENT_OPEN_FAILED, reinterpret_cast<void *>(-ENOTSUP));
  return ACTION_IO_ERROR;
}

Action *
QUICNetProcessor::main_accept(Continuation *cont, SOCKET fd, AcceptOptions const &opt)
{
  Dbg(dbg_ctl_iocore_net_processor, "NetProcessor::main_accept - port %d,recv_bufsize %d, send_bufsize %d, sockopt 0x%0x",
      opt.local_port, opt.recv_bufsize, opt.send_bufsize, opt.sockopt_flags);

  IpEndpoint accept_ip;
  NetAccept *na = createNetAccept(opt);

  Metrics::Gauge::increment(net_rsb.accepts_currently_open);

  if (opt.localhost_only) {
    accept_ip.setToLoopback(opt.ip_family);
  } else if (opt.local_ip.isValid()) {
    accept_ip.assign(opt.local_ip);
  } else {
    accept_ip.setToAnyAddr(opt.ip_family);
  }
  ink_assert(0 < opt.local_port && opt.local_port < 65536);
  accept_ip.network_order_port() = htons(opt.local_port);

  na->server.sock = UnixSocket{fd};
  ats_ip_copy(&na->server.accept_addr, &accept_ip);

  na->action_ = new NetAcceptAction(cont, &na->server);
  na->init_accept();

  return na->action_.get();
}
