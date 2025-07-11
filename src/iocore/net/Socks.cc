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

/*
  Socks.cc



  This file contains the Socks client functionality. Previously this code was
  duplicated in UnixNet.cc and NTNetProcessor.cc
*/

#include "P_Socks.h"
#include "P_Net.h"
#include "../eventsystem/P_VConnection.h"
#include "iocore/net/NetProcessor.h"
#include "tscore/InkErrno.h"
#include "swoc/swoc_file.h"

using namespace swoc::literals;

ClassAllocator<SocksEntry> socksAllocator("socksAllocator");

namespace
{

DbgCtl dbg_ctl_Socks{"Socks"};
DbgCtl dbg_ctl_SocksParent{"SocksParent"};
DbgCtl dbg_ctl_SocksProxy{"SocksProxy"};

} // end anonymous namespace

void
SocksEntry::init(Ptr<ProxyMutex> &m, SocksNetVC *vc, unsigned char socks_support, unsigned char ver)
{
  mutex  = m;
  buf    = new_MIOBuffer(BUFFER_SIZE_INDEX_32K);
  reader = buf->alloc_reader();

  socks_cmd = socks_support;

  if (ver == SOCKS_DEFAULT_VERSION) {
    version = netProcessor.socks_conf_stuff->default_version;
  } else {
    version = ver;
  }

  SET_HANDLER(&SocksEntry::startEvent);

  ats_ip_copy(&target_addr, vc->get_remote_addr());

  req_data.hdr          = nullptr;
  req_data.hostname_str = nullptr;
  req_data.api_info     = nullptr;
  req_data.xact_start   = time(nullptr);

  assert(ats_is_ip4(&target_addr));
  ats_ip_copy(&req_data.dest_ip, &target_addr);

  // we dont have information about the source. set to destination's
  ats_ip_copy(&req_data.src_ip, &target_addr);

  server_params = SocksServerConfig::acquire();

  nattempts = 0;
  findServer();

  timeout    = this_ethread()->schedule_in(this, HRTIME_SECONDS(netProcessor.socks_conf_stuff->server_connect_timeout));
  write_done = false;
}

void
SocksEntry::findServer()
{
  nattempts++;
  unsigned int fail_threshold = server_params->policy.FailThreshold;
  unsigned int retry_time     = server_params->policy.ParentRetryTime;

  if (nattempts == 1) {
    ink_assert(server_result.result == ParentResultType::UNDEFINED);
    server_params->findParent(&req_data, &server_result, fail_threshold, retry_time);
  } else {
    socks_conf_struct *conf = netProcessor.socks_conf_stuff;
    if ((nattempts - 1) % conf->per_server_connection_attempts) {
      return; // attempt again
    }

    server_params->markParentDown(&server_result, fail_threshold, retry_time);

    if (nattempts > conf->connection_attempts) {
      server_result.result = ParentResultType::FAIL;
    } else {
      server_params->nextParent(&req_data, &server_result, fail_threshold, retry_time);
    }
  }

  switch (server_result.result) {
  case ParentResultType::SPECIFIED:
    // Original was inet_addr, but should hostnames work?
    // ats_ip_pton only supports numeric (because other clients
    // explicitly want to avoid hostname lookups).
    if (0 == ats_ip_pton(server_result.hostname, &server_addr)) {
      ats_ip_port_cast(&server_addr) = htons(server_result.port);
    } else {
      Dbg(dbg_ctl_SocksParent, "Invalid parent server specified %s", server_result.hostname);
    }
    break;

  default:
    ink_assert(!"Unexpected event");
  // fallthrough
  case ParentResultType::DIRECT:
  case ParentResultType::FAIL:
    memset(&server_addr, 0, sizeof(server_addr));
  }

  char buff[INET6_ADDRSTRLEN];
  Dbg(dbg_ctl_SocksParent, "findServer result: %s:%d", ats_ip_ntop(&server_addr.sa, buff, sizeof(buff)),
      ats_ip_port_host_order(&server_addr));
}

void
SocksEntry::free()
{
  MUTEX_TRY_LOCK(lock, action_.mutex, this_ethread());
  // Socks continuation share the user's lock
  // so acquiring a lock shouldn't fail
  ink_release_assert(lock.is_locked());

  if (timeout) {
    timeout->cancel(this);
  }

  if (!lerrno && netVConnection && server_result.retry) {
    server_params->markParentUp(&server_result);
  }

  if ((action_.cancelled || lerrno) && netVConnection) {
    netVConnection->do_io_close();
  }

  if (!action_.cancelled) {
    if (lerrno || !netVConnection) {
      Dbg(dbg_ctl_Socks, "retryevent: Sent errno %d to HTTP", lerrno);
      Metrics::Counter::increment(net_rsb.socks_connections_unsuccessful);
      action_.continuation->handleEvent(NET_EVENT_OPEN_FAILED, reinterpret_cast<void *>(-lerrno));
    } else {
      netVConnection->do_io_read(this, 0, nullptr);
      netVConnection->do_io_write(this, 0, nullptr);
      netVConnection->action_ = action_; // assign the original continuation
      netVConnection->con.setRemote(&server_addr.sa);
      Dbg(dbg_ctl_Socks, "Sent success to HTTP");
      Metrics::Counter::increment(net_rsb.socks_connections_successful);
      action_.continuation->handleEvent(NET_EVENT_OPEN, netVConnection);
    }
  }
  SocksServerConfig::release(server_params);

  free_MIOBuffer(buf);
  action_ = nullptr;
  mutex   = nullptr;
  socksAllocator.free(this);
}

int
SocksEntry::startEvent(int event, void *data)
{
  if (event == NET_EVENT_OPEN) {
    netVConnection = static_cast<SocksNetVC *>(data);

    if (version == SOCKS5_VERSION) {
      auth_handler = &socks5BasicAuthHandler;
    }

    SET_HANDLER(&SocksEntry::mainEvent);
    mainEvent(NET_EVENT_OPEN, data);
  } else {
    if (timeout) {
      timeout->cancel(this);
      timeout = nullptr;
    }

    char buff[INET6_ADDRPORTSTRLEN];
    Dbg(dbg_ctl_Socks, "Failed to connect to %s", ats_ip_nptop(&server_addr.sa, buff, sizeof(buff)));

    findServer();

    if (!ats_is_ip(&server_addr)) {
      Dbg(dbg_ctl_Socks, "Unable to open connection to the SOCKS server");
      lerrno = ESOCK_NO_SOCK_SERVER_CONN;
      free();
      return EVENT_CONT;
    }

    if (timeout) {
      timeout->cancel(this);
      timeout = nullptr;
    }

    if (netVConnection) {
      netVConnection->do_io_close();
      netVConnection = nullptr;
    }

    timeout = this_ethread()->schedule_in(this, HRTIME_SECONDS(netProcessor.socks_conf_stuff->server_connect_timeout));

    write_done = false;

    NetVCOptions options;
    options.socks_support = NO_SOCKS;
    netProcessor.connect_re(this, &server_addr.sa, options);
  }

  return EVENT_CONT;
}

int
SocksEntry::mainEvent(int event, void *data)
{
  int            ret     = EVENT_DONE;
  int            n_bytes = 0;
  unsigned char *p;

  switch (event) {
  case NET_EVENT_OPEN:
    buf->reset();
    unsigned short ts;
    p = reinterpret_cast<unsigned char *>(buf->start());
    ink_assert(netVConnection);

    if (auth_handler) {
      n_bytes = invokeSocksAuthHandler(auth_handler, SOCKS_AUTH_OPEN, p);
    } else {
      p[n_bytes++] = version;
      p[n_bytes++] = (socks_cmd == NORMAL_SOCKS) ? SOCKS_CONNECT : socks_cmd;
      ts           = ats_ip_port_cast(&target_addr);

      if (version == SOCKS5_VERSION) {
        p[n_bytes++] = 0; // Reserved
        if (ats_is_ip4(&target_addr)) {
          p[n_bytes++] = 1; // IPv4 addr
          memcpy(p + n_bytes, &target_addr.sin.sin_addr, 4);
          n_bytes += 4;
        } else if (ats_is_ip6(&target_addr)) {
          p[n_bytes++] = 4; // IPv6 addr
          memcpy(p + n_bytes, &target_addr.sin6.sin6_addr, TS_IP6_SIZE);
          n_bytes += TS_IP6_SIZE;
        } else {
          Dbg(dbg_ctl_Socks, "SOCKS supports only IP addresses.");
        }
      }

      memcpy(p + n_bytes, &ts, 2);
      n_bytes += 2;

      if (version == SOCKS4_VERSION) {
        if (ats_is_ip4(&target_addr)) {
          // for socks4, ip addr is after the port
          memcpy(p + n_bytes, &target_addr.sin.sin_addr, 4);
          n_bytes += 4;

          p[n_bytes++] = 0; // nullptr
        } else {
          Dbg(dbg_ctl_Socks, "SOCKS v4 supports only IPv4 addresses.");
        }
      }
    }

    buf->fill(n_bytes);

    if (!timeout) {
      /* timeout would be already set when we come here from StartEvent() */
      timeout = this_ethread()->schedule_in(this, HRTIME_SECONDS(netProcessor.socks_conf_stuff->socks_timeout));
    }

    netVConnection->do_io_write(this, n_bytes, reader, false);

    ret = EVENT_CONT;
    break;

  case VC_EVENT_WRITE_READY:

    ret = EVENT_CONT;
    break;

  case VC_EVENT_WRITE_COMPLETE:
    if (timeout) {
      timeout->cancel(this);
      timeout    = nullptr;
      write_done = true;
    }

    buf->reset(); // Use the same buffer for a read now

    if (auth_handler) {
      n_bytes = invokeSocksAuthHandler(auth_handler, SOCKS_AUTH_WRITE_COMPLETE, nullptr);
    } else if (socks_cmd == NORMAL_SOCKS) {
      n_bytes = (version == SOCKS5_VERSION) ? SOCKS5_REP_LEN : SOCKS4_REP_LEN;
    } else {
      Dbg(dbg_ctl_Socks, "Tunnelling the connection");
      // let the client handle the response
      free();
      break;
    }

    timeout = this_ethread()->schedule_in(this, HRTIME_SECONDS(netProcessor.socks_conf_stuff->socks_timeout));

    netVConnection->do_io_read(this, n_bytes, buf);

    ret = EVENT_DONE;
    break;

  case VC_EVENT_READ_READY:
    ret = EVENT_CONT;

    if (version == SOCKS5_VERSION && auth_handler == nullptr) {
      VIO *vio = static_cast<VIO *>(data);
      p        = reinterpret_cast<unsigned char *>(buf->start());

      if (vio->ndone >= 5) {
        int reply_len;

        switch (p[3]) {
        case SOCKS_ATYPE_IPV4:
          reply_len = 10;
          break;
        case SOCKS_ATYPE_FQHN:
          reply_len = 7 + p[4];
          break;
        case SOCKS_ATYPE_IPV6:
          Dbg(dbg_ctl_Socks, "Who is using IPv6 Addr?");
          reply_len = 22;
          break;
        default:
          reply_len = INT_MAX;
          Dbg(dbg_ctl_Socks, "Illegal address type(%d) in Socks server", static_cast<int>(p[3]));
        }

        if (vio->ndone >= reply_len) {
          vio->nbytes = vio->ndone;
          ret         = EVENT_DONE;
        }
      }
    }

    if (ret == EVENT_CONT) {
      break;
    }
  // Fall Through
  case VC_EVENT_READ_COMPLETE:
    if (timeout) {
      timeout->cancel(this);
      timeout = nullptr;
    }
    p = reinterpret_cast<unsigned char *>(buf->start());

    if (auth_handler) {
      SocksAuthHandler temp = auth_handler;

      if (invokeSocksAuthHandler(auth_handler, SOCKS_AUTH_READ_COMPLETE, p) < 0) {
        lerrno = ESOCK_DENIED;
        free();
      } else if (auth_handler != temp) {
        // here either authorization is done or there is another
        // stage left.
        mainEvent(NET_EVENT_OPEN, nullptr);
      }

    } else {
      bool success;
      if (version == SOCKS5_VERSION) {
        success = (p[0] == SOCKS5_VERSION && p[1] == SOCKS5_REQ_GRANTED);
        Dbg(dbg_ctl_Socks, "received reply of length %" PRId64 " addr type %d", ((VIO *)data)->ndone, static_cast<int>(p[3]));
      } else {
        success = (p[0] == 0 && p[1] == SOCKS4_REQ_GRANTED);
      }

      // ink_assert(*(p) == 0);
      if (!success) { // SOCKS request failed
        Dbg(dbg_ctl_Socks, "Socks request denied %d", static_cast<int>(*(p + 1)));
        lerrno = ESOCK_DENIED;
      } else {
        Dbg(dbg_ctl_Socks, "Socks request successful %d", static_cast<int>(*(p + 1)));
        lerrno = 0;
      }
      free();
    }

    break;

  case EVENT_INTERVAL:
    timeout = nullptr;
    if (write_done) {
      lerrno = ESOCK_TIMEOUT;
      free();
      break;
    }
    /* else
       This is server_connect_timeout. So we treat this as server being
       down.
       Should cancel any pending connect() action. Important on windows
    */
    // fallthrough

  case VC_EVENT_ERROR:
    /*This is mostly ECONNREFUSED on Unix */
    SET_HANDLER(&SocksEntry::startEvent);
    startEvent(NET_EVENT_OPEN_FAILED, nullptr);
    break;

  case VC_EVENT_EOS:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
    Dbg(dbg_ctl_Socks, "VC_EVENT error: %s", get_vc_event_name(event));
    lerrno = ESOCK_NO_SOCK_SERVER_CONN;
    free();
    break;
  default:
    // BUGBUG:: could be active/inactivity timeout ...
    ink_assert(!"bad case value");
    Dbg(dbg_ctl_Socks, "Bad Case/Net Error Event");
    lerrno = ESOCK_NO_SOCK_SERVER_CONN;
    free();
    break;
  }

  return ret;
}

void
loadSocksConfiguration(socks_conf_struct *socks_conf_stuff)
{
  ats_scoped_str  config_pathname;
  swoc::Errata    errata;
  std::error_code ec;
  std::string     config_text;

  socks_conf_stuff->accept_enabled = 0; // initialize it INKqa08593
  socks_conf_stuff->socks_needed   = RecGetRecordInt("proxy.config.socks.socks_needed").value_or(0);
  if (!socks_conf_stuff->socks_needed) {
    Dbg(dbg_ctl_Socks, "Socks Turned Off");
    return;
  }

  socks_conf_stuff->default_version = RecGetRecordInt("proxy.config.socks.socks_version").value_or(0);
  Dbg(dbg_ctl_Socks, "Socks Version %d", socks_conf_stuff->default_version);

  if (socks_conf_stuff->default_version != 4 && socks_conf_stuff->default_version != 5) {
    Error("SOCKS Config: Unsupported Version: %d. SOCKS Turned off", socks_conf_stuff->default_version);
    goto error;
  }

  socks_conf_stuff->server_connect_timeout = RecGetRecordInt("proxy.config.socks.server_connect_timeout").value_or(0);
  socks_conf_stuff->socks_timeout          = RecGetRecordInt("proxy.config.socks.socks_timeout").value_or(0);
  Dbg(dbg_ctl_Socks, "server connect timeout: %d socks response timeout %d", socks_conf_stuff->server_connect_timeout,
      socks_conf_stuff->socks_timeout);

  socks_conf_stuff->per_server_connection_attempts =
    RecGetRecordInt("proxy.config.socks.per_server_connection_attempts").value_or(0);
  socks_conf_stuff->connection_attempts = RecGetRecordInt("proxy.config.socks.connection_attempts").value_or(0);

  socks_conf_stuff->accept_enabled = RecGetRecordInt("proxy.config.socks.accept_enabled").value_or(0);
  socks_conf_stuff->accept_port    = RecGetRecordInt("proxy.config.socks.accept_port").value_or(0);
  socks_conf_stuff->http_port      = RecGetRecordInt("proxy.config.socks.http_port").value_or(0);
  Dbg(dbg_ctl_SocksProxy,
      "Read SocksProxy info: accept_enabled = %d "
      "accept_port = %d http_port = %d",
      socks_conf_stuff->accept_enabled, socks_conf_stuff->accept_port, socks_conf_stuff->http_port);

  SocksServerConfig::startup();

  config_pathname = RecConfigReadConfigPath("proxy.config.socks.socks_config_file");
  Dbg(dbg_ctl_Socks, "Socks Config File: %s", config_pathname.get());

  if (!config_pathname) {
    Error("SOCKS Config: could not read config file name. SOCKS Turned off");
    goto error;
  }

  config_text = swoc::file::load(swoc::file::path(config_pathname), ec);
  if (ec) {
    swoc::bwprint(config_text, "SOCK Config: Disabled, could not open config file \"{}\" {}", config_pathname, ec);
    Error("%s", config_text.c_str());
    goto error;
  }

  errata = loadSocksIPAddrs(config_text, socks_conf_stuff);

  if (!errata.is_ok()) {
    swoc::bwprint(config_text, "SOCK Config: Error\n{}", errata);
    Error("%s", config_text.c_str());
    goto error;
  }

  if (loadSocksAuthInfo(config_text, socks_conf_stuff) != 0) {
    Error("SOCKS Config: Error while reading Socks auth info");
    goto error;
  }
  Dbg(dbg_ctl_Socks, "Socks Turned on");

  return;
error:

  socks_conf_stuff->socks_needed   = 0;
  socks_conf_stuff->accept_enabled = 0;
}

int
loadSocksAuthInfo(swoc::TextView content, socks_conf_struct *socks_stuff)
{
  static constexpr swoc::TextView PREFIX = "auth u ";
  std::string                     text;

  while (content.ltrim_if(&isspace)) {
    auto line = content.take_prefix_at('\n');

    if (line.starts_with(PREFIX)) {
      line.remove_prefix(PREFIX.size()).ltrim_if(&isspace);
      auto user_name = line.take_prefix_if(&isspace);
      auto password  = line.take_prefix_if(&isspace);

      if (!user_name.empty() && !password.empty()) {
        Dbg(dbg_ctl_Socks, "%s", swoc::bwprint(text, "Read auth credentials \"{}\" : \"{}\"", user_name, password).c_str());
        swoc::bwprint(socks_stuff->user_name_n_passwd, "{}{}{}{}", static_cast<char>(user_name.size()), user_name,
                      static_cast<char>(password.size()), password);
      }
    }
  }
  return 0;
}

swoc::Errata
loadSocksIPAddrs(swoc::TextView content, socks_conf_struct *socks_stuff)
{
  static constexpr swoc::TextView PREFIX = "no_socks ";
  std::string                     text;

  while (content.ltrim_if(&isspace)) {
    auto line = content.take_prefix_at('\n');
    if (line.starts_with(PREFIX)) {
      line.remove_prefix(PREFIX.size());
      while (line.ltrim_if(&isspace)) {
        auto token = line.take_prefix_at(',');
        if (swoc::IPRange r; r.load(token)) {
          socks_stuff->ip_addrs.mark(r);
        } else {
          return swoc::Errata(ERRATA_ERROR, "Invalid IP address range \"{}\"", token);
        }
      }
    }
  }
  return {};
}

int
socks5BasicAuthHandler(int event, unsigned char *p, void (**h_ptr)(void))
{
  // for more info on Socks5 see RFC 1928
  int   ret         = 0;
  char *pass_phrase = netProcessor.socks_conf_stuff->user_name_n_passwd.data();

  switch (event) {
  case SOCKS_AUTH_OPEN:
    p[ret++] = SOCKS5_VERSION;        // version
    p[ret++] = (pass_phrase) ? 2 : 1; // #Methods
    p[ret++] = 0;                     // no authentication
    if (pass_phrase) {
      p[ret++] = 2;
    }

    break;

  case SOCKS_AUTH_WRITE_COMPLETE:
    // return number of bytes to read
    ret = 2;
    break;

  case SOCKS_AUTH_READ_COMPLETE:

    if (p[0] == SOCKS5_VERSION) {
      switch (p[1]) {
      case 0: // no authentication required
        Dbg(dbg_ctl_Socks, "No authentication required for Socks server");
        // make sure this is ok for us. right now it is always ok for us.
        *h_ptr = nullptr;
        break;

      case 2:
        Dbg(dbg_ctl_Socks, "Socks server wants username/passwd");
        if (!pass_phrase) {
          Dbg(dbg_ctl_Socks, "Buggy Socks server: asks for username/passwd "
                             "when not supplied as an option");
          ret    = -1;
          *h_ptr = nullptr;
        } else {
          *reinterpret_cast<SocksAuthHandler *>(h_ptr) = &socks5PasswdAuthHandler;
        }

        break;

      case 0xff:
        Dbg(dbg_ctl_Socks, "None of the Socks authentications is acceptable "
                           "to the server");
        *h_ptr = nullptr;
        ret    = -1;
        break;

      default:
        Dbg(dbg_ctl_Socks, "Unexpected Socks auth method (%d) from the server", static_cast<int>(p[1]));
        ret = -1;
        break;
      }
    } else {
      Dbg(dbg_ctl_Socks, "authEvent got wrong version %d from the Socks server", static_cast<int>(p[0]));
      ret = -1;
    }

    break;

  default:
    // This should be impossible
    ink_assert(!"bad case value");
    ret = -1;
    break;
  }
  return ret;
}

int
socks5PasswdAuthHandler(int event, unsigned char *p, void (**h_ptr)(void))
{
  // for more info see RFC 1929
  int   ret = 0;
  char *pass_phrase;
  int   pass_len;

  switch (event) {
  case SOCKS_AUTH_OPEN:
    pass_phrase = netProcessor.socks_conf_stuff->user_name_n_passwd.data();
    pass_len    = netProcessor.socks_conf_stuff->user_name_n_passwd.length();
    ink_assert(pass_phrase);

    p[0] = 1; // version
    memcpy(&p[1], pass_phrase, pass_len);

    ret = 1 + pass_len;
    break;

  case SOCKS_AUTH_WRITE_COMPLETE:
    // return number of bytes to read
    ret = 2;
    break;

  case SOCKS_AUTH_READ_COMPLETE:
    // NEC thinks it is 5 RFC seems to indicate 1.
    switch (p[1]) {
    case 0:
      Dbg(dbg_ctl_Socks, "Username/Passwd succeeded");
      *h_ptr = nullptr;
      break;

    default:
      Dbg(dbg_ctl_Socks, "Username/Passwd authentication failed ret_code: %d", static_cast<int>(p[1]));
      ret = -1;
    }

    break;

  default:
    ink_assert(!"bad case value");
    ret = -1;
    break;
  }
  return ret;
}

void
SocksAddrType::reset()
{
  if (type != SOCKS_ATYPE_IPV4 && addr.buf) {
    ats_free(addr.buf);
  }

  addr.buf = nullptr;
  type     = SOCKS_ATYPE_NONE;
}
