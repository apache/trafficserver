/** @file

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

#include "tscore/ink_config.h"
#include "tscore/I_Layout.h"

#include "P_Net.h"
#include "records/I_RecHttp.h"

#include "P_QUICNetProcessor.h"
#include "P_QUICNet.h"
#include "P_QUICPacketHandler.h"
#include "QUICGlobals.h"
#include "QUICConfig.h"
#include "QUICMultiCertConfigLoader.h"
#include "QUICResetTokenTable.h"

//
// Global Data
//

QUICNetProcessor quic_NetProcessor;

QUICNetProcessor::QUICNetProcessor() {}

QUICNetProcessor::~QUICNetProcessor()
{
  // TODO: clear all values before destroy the table.
  delete this->_ctable;
}

void
QUICNetProcessor::init()
{
  // first we allocate a QUICPollCont.
  this->quicPollCont_offset = eventProcessor.allocate(sizeof(QUICPollCont));

  // schedule event
  eventProcessor.schedule_spawn(&initialize_thread_for_quic_net, ET_NET);
}

int
QUICNetProcessor::start(int, size_t stacksize)
{
  QUIC::init();
  // This initialization order matters ...
  // QUICInitializeLibrary();
  QUICConfig::startup();
  QUICCertConfig::startup();

#ifdef TLS1_3_VERSION_DRAFT_TXT
  // FIXME: remove this when TLS1_3_VERSION_DRAFT_TXT is removed
  Debug("quic_ps", "%s", TLS1_3_VERSION_DRAFT_TXT);
#endif

  return 0;
}

NetAccept *
QUICNetProcessor::createNetAccept(const NetProcessor::AcceptOptions &opt)
{
  if (this->_ctable == nullptr) {
    QUICConfig::scoped_config params;
    this->_ctable = new QUICConnectionTable(params->connection_table_size());
    this->_rtable = new QUICResetTokenTable();
  }
  return (NetAccept *)new QUICPacketHandlerIn(opt, *this->_ctable, *this->_rtable);
}

NetVConnection *
QUICNetProcessor::allocate_vc(EThread *t)
{
  QUICNetVConnection *vc;

  if (t) {
    vc = THREAD_ALLOC(quicNetVCAllocator, t);
    new (vc) QUICNetVConnection();
  } else {
    if (likely(vc = quicNetVCAllocator.alloc())) {
      new (vc) QUICNetVConnection();
      vc->from_accept_thread = true;
    }
  }

  vc->ep.syscall = false;
  return vc;
}

Action *
QUICNetProcessor::connect_re(Continuation *cont, sockaddr const *remote_addr, NetVCOptions *opt)
{
  Debug("quic_ps", "connect to server");
  EThread *t = cont->mutex->thread_holding;
  ink_assert(t);
  QUICNetVConnection *vc = static_cast<QUICNetVConnection *>(this->allocate_vc(t));

  if (opt) {
    vc->options = *opt;
  } else {
    opt = &vc->options;
  }

  int fd;
  Action *status;
  bool result = udpNet.CreateUDPSocket(&fd, remote_addr, &status, *opt);
  if (!result) {
    vc->free(t);
    return status;
  }

  // Setup UDPConnection
  UnixUDPConnection *con = new UnixUDPConnection(fd);
  Debug("quic_ps", "con=%p fd=%d", con, fd);

  this->_rtable                        = new QUICResetTokenTable();
  QUICPacketHandlerOut *packet_handler = new QUICPacketHandlerOut(*this->_rtable);
  if (opt->local_ip.isValid()) {
    con->setBinding(opt->local_ip, opt->local_port);
  }
  con->bindToThread(packet_handler);

  PollCont *pc       = get_UDPPollCont(con->ethread);
  PollDescriptor *pd = pc->pollDescriptor;

  errno   = 0;
  int res = con->ep.start(pd, con, EVENTIO_READ);
  if (res < 0) {
    Debug("udpnet", "Error: %s (%d)", strerror(errno), errno);
  }

  // Setup QUICNetVConnection
  QUICConnectionId client_dst_cid;
  client_dst_cid.randomize();
  // vc->init set handler of vc `QUICNetVConnection::startEvent`
  vc->init(QUIC_SUPPORTED_VERSIONS[0], client_dst_cid, client_dst_cid, con, packet_handler, this->_rtable);
  packet_handler->init(vc);

  // Connection ID will be changed
  vc->id = net_next_connection_number();
  vc->set_context(NET_VCONNECTION_OUT);
  vc->con.setRemote(remote_addr);
  vc->submit_time = Thread::get_hrtime();
  vc->mutex       = cont->mutex;
  vc->action_     = cont;

  if (t->is_event_type(opt->etype)) {
    MUTEX_TRY_LOCK(lock, cont->mutex, t);
    if (lock.is_locked()) {
      MUTEX_TRY_LOCK(lock2, get_NetHandler(t)->mutex, t);
      if (lock2.is_locked()) {
        vc->connectUp(t, NO_FD);
        return ACTION_RESULT_DONE;
      }
    }
  }

  // Try to stay on the current thread if it is the right type
  if (t->is_event_type(opt->etype)) {
    t->schedule_imm(vc);
  } else { // Otherwise, pass along to another thread of the right type
    eventProcessor.schedule_imm(vc, opt->etype);
  }

  return ACTION_RESULT_DONE;
}

Action *
QUICNetProcessor::main_accept(Continuation *cont, SOCKET fd, AcceptOptions const &opt)
{
  // UnixNetProcessor *this_unp = static_cast<UnixNetProcessor *>(this);
  Debug("iocore_net_processor", "NetProcessor::main_accept - port %d,recv_bufsize %d, send_bufsize %d, sockopt 0x%0x",
        opt.local_port, opt.recv_bufsize, opt.send_bufsize, opt.sockopt_flags);

  ProxyMutex *mutex  = this_ethread()->mutex.get();
  int accept_threads = opt.accept_threads; // might be changed.
  IpEndpoint accept_ip;                    // local binding address.
  // char thr_name[MAX_THREAD_NAME_LENGTH];

  NetAccept *na = createNetAccept(opt);

  if (accept_threads < 0) {
    REC_ReadConfigInteger(accept_threads, "proxy.config.accept_threads");
  }
  NET_INCREMENT_DYN_STAT(net_accepts_currently_open_stat);

  if (opt.localhost_only) {
    accept_ip.setToLoopback(opt.ip_family);
  } else if (opt.local_ip.isValid()) {
    accept_ip.assign(opt.local_ip);
  } else {
    accept_ip.setToAnyAddr(opt.ip_family);
  }
  ink_assert(0 < opt.local_port && opt.local_port < 65536);
  accept_ip.network_order_port() = htons(opt.local_port);

  na->accept_fn = net_accept;
  na->server.fd = fd;
  ats_ip_copy(&na->server.accept_addr, &accept_ip);

  na->action_         = new NetAcceptAction();
  *na->action_        = cont;
  na->action_->server = &na->server;
  na->init_accept();

  SCOPED_MUTEX_LOCK(lock, na->mutex, this_ethread());
  udpNet.UDPBind((Continuation *)na, &na->server.accept_addr.sa, fd, 1048576, 1048576);

  return na->action_.get();
}
