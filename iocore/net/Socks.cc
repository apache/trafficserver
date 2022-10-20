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

#include "P_Net.h"
#include "tscore/I_Layout.h"
#include "tscore/ink_sock.h"
#include "tscore/InkErrno.h"
#include "tscore/IpMapConf.h"

socks_conf_struct *g_socks_conf_stuff = nullptr;

ClassAllocator<SocksEntry> socksAllocator("socksAllocator");

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

#ifdef SOCKS_WITH_TS
  req_data.hdr          = nullptr;
  req_data.hostname_str = nullptr;
  req_data.api_info     = nullptr;
  req_data.xact_start   = time(nullptr);

  assert(ats_is_ip4(&target_addr));
  ats_ip_copy(&req_data.dest_ip, &target_addr);

  // we dont have information about the source. set to destination's
  ats_ip_copy(&req_data.src_ip, &target_addr);

  server_params = SocksServerConfig::acquire();
#endif

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

#ifdef SOCKS_WITH_TS
  if (nattempts == 1) {
    ink_assert(server_result.result == PARENT_UNDEFINED);
    server_params->findParent(&req_data, &server_result, fail_threshold, retry_time);
  } else {
    socks_conf_struct *conf = netProcessor.socks_conf_stuff;
    if ((nattempts - 1) % conf->per_server_connection_attempts) {
      return; // attempt again
    }

    server_params->markParentDown(&server_result, fail_threshold, retry_time);

    if (nattempts > conf->connection_attempts) {
      server_result.result = PARENT_FAIL;
    } else {
      server_params->nextParent(&req_data, &server_result, fail_threshold, retry_time);
    }
  }

  switch (server_result.result) {
  case PARENT_SPECIFIED:
    // Original was inet_addr, but should hostnames work?
    // ats_ip_pton only supports numeric (because other clients
    // explicitly want to avoid hostname lookups).
    if (0 == ats_ip_pton(server_result.hostname, &server_addr)) {
      ats_ip_port_cast(&server_addr) = htons(server_result.port);
    } else {
      Debug("SocksParent", "Invalid parent server specified %s", server_result.hostname);
    }
    break;

  default:
    ink_assert(!"Unexpected event");
  // fallthrough
  case PARENT_DIRECT:
  case PARENT_FAIL:
    memset(&server_addr, 0, sizeof(server_addr));
  }
#else
  if (nattempts > netProcessor.socks_conf_stuff->connection_attempts)
    memset(&server_addr, 0, sizeof(server_addr));
  else
    ats_ip_copy(&server_addr, &g_socks_conf_stuff->server_addr);
#endif // SOCKS_WITH_TS

  char buff[INET6_ADDRSTRLEN];
  Debug("SocksParents", "findServer result: %s:%d", ats_ip_ntop(&server_addr.sa, buff, sizeof(buff)),
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

#ifdef SOCKS_WITH_TS
  if (!lerrno && netVConnection && server_result.retry) {
    server_params->markParentUp(&server_result);
  }
#endif

  if ((action_.cancelled || lerrno) && netVConnection) {
    netVConnection->do_io_close();
  }

  if (!action_.cancelled) {
    if (lerrno || !netVConnection) {
      Debug("Socks", "retryevent: Sent errno %d to HTTP", lerrno);
      NET_INCREMENT_DYN_STAT(socks_connections_unsuccessful_stat);
      action_.continuation->handleEvent(NET_EVENT_OPEN_FAILED, (void *)static_cast<intptr_t>(-lerrno));
    } else {
      netVConnection->do_io_read(this, 0, nullptr);
      netVConnection->do_io_write(this, 0, nullptr);
      netVConnection->action_ = action_; // assign the original continuation
      netVConnection->con.setRemote(&server_addr.sa);
      Debug("Socks", "Sent success to HTTP");
      NET_INCREMENT_DYN_STAT(socks_connections_successful_stat);
      action_.continuation->handleEvent(NET_EVENT_OPEN, netVConnection);
    }
  }
#ifdef SOCKS_WITH_TS
  SocksServerConfig::release(server_params);
#endif

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
    Debug("Socks", "Failed to connect to %s", ats_ip_nptop(&server_addr.sa, buff, sizeof(buff)));

    findServer();

    if (!ats_is_ip(&server_addr)) {
      Debug("Socks", "Unable to open connection to the SOCKS server");
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
    netProcessor.connect_re(this, &server_addr.sa, &options);
  }

  return EVENT_CONT;
}

int
SocksEntry::mainEvent(int event, void *data)
{
  int ret     = EVENT_DONE;
  int n_bytes = 0;
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
      // Debug("Socks", " Got NET_EVENT_OPEN to SOCKS server");

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
          Debug("Socks", "SOCKS supports only IP addresses.");
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
          Debug("Socks", "SOCKS v4 supports only IPv4 addresses.");
        }
      }
    }

    buf->fill(n_bytes);

    if (!timeout) {
      /* timeout would be already set when we come here from StartEvent() */
      timeout = this_ethread()->schedule_in(this, HRTIME_SECONDS(netProcessor.socks_conf_stuff->socks_timeout));
    }

    netVConnection->do_io_write(this, n_bytes, reader, false);
    // Debug("Socks", "Sent the request to the SOCKS server");

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
      Debug("Socks", "Tunnelling the connection");
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
          Debug("Socks", "Who is using IPv6 Addr?");
          reply_len = 22;
          break;
        default:
          reply_len = INT_MAX;
          Debug("Socks", "Illegal address type(%d) in Socks server", (int)p[3]);
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
    // Debug("Socks", "Successfully read the reply from the SOCKS server");
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
        Debug("Socks", "received reply of length %" PRId64 " addr type %d", ((VIO *)data)->ndone, (int)p[3]);
      } else {
        success = (p[0] == 0 && p[1] == SOCKS4_REQ_GRANTED);
      }

      // ink_assert(*(p) == 0);
      if (!success) { // SOCKS request failed
        Debug("Socks", "Socks request denied %d", (int)*(p + 1));
        lerrno = ESOCK_DENIED;
      } else {
        Debug("Socks", "Socks request successful %d", (int)*(p + 1));
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
    Debug("Socks", "VC_EVENT error: %s", get_vc_event_name(event));
    lerrno = ESOCK_NO_SOCK_SERVER_CONN;
    free();
    break;
  default:
    // BUGBUG:: could be active/inactivity timeout ...
    ink_assert(!"bad case value");
    Debug("Socks", "Bad Case/Net Error Event");
    lerrno = ESOCK_NO_SOCK_SERVER_CONN;
    free();
    break;
  }

  return ret;
}

void
loadSocksConfiguration(socks_conf_struct *socks_conf_stuff)
{
  int socks_config_fd = -1;
  ats_scoped_str config_pathname;
#ifdef SOCKS_WITH_TS
  char *tmp;
#endif

  socks_conf_stuff->accept_enabled = 0; // initialize it INKqa08593
  socks_conf_stuff->socks_needed   = REC_ConfigReadInteger("proxy.config.socks.socks_needed");
  if (!socks_conf_stuff->socks_needed) {
    Debug("Socks", "Socks Turned Off");
    return;
  }

  socks_conf_stuff->default_version = REC_ConfigReadInteger("proxy.config.socks.socks_version");
  Debug("Socks", "Socks Version %d", socks_conf_stuff->default_version);

  if (socks_conf_stuff->default_version != 4 && socks_conf_stuff->default_version != 5) {
    Error("SOCKS Config: Unsupported Version: %d. SOCKS Turned off", socks_conf_stuff->default_version);
    goto error;
  }

  socks_conf_stuff->server_connect_timeout = REC_ConfigReadInteger("proxy.config.socks.server_connect_timeout");
  socks_conf_stuff->socks_timeout          = REC_ConfigReadInteger("proxy.config.socks.socks_timeout");
  Debug("Socks", "server connect timeout: %d socks response timeout %d", socks_conf_stuff->server_connect_timeout,
        socks_conf_stuff->socks_timeout);

  socks_conf_stuff->per_server_connection_attempts = REC_ConfigReadInteger("proxy.config.socks.per_server_connection_attempts");
  socks_conf_stuff->connection_attempts            = REC_ConfigReadInteger("proxy.config.socks.connection_attempts");

  socks_conf_stuff->accept_enabled = REC_ConfigReadInteger("proxy.config.socks.accept_enabled");
  socks_conf_stuff->accept_port    = REC_ConfigReadInteger("proxy.config.socks.accept_port");
  socks_conf_stuff->http_port      = REC_ConfigReadInteger("proxy.config.socks.http_port");
  Debug("SocksProxy",
        "Read SocksProxy info: accept_enabled = %d "
        "accept_port = %d http_port = %d",
        socks_conf_stuff->accept_enabled, socks_conf_stuff->accept_port, socks_conf_stuff->http_port);

#ifdef SOCKS_WITH_TS
  SocksServerConfig::startup();
#endif

  config_pathname = RecConfigReadConfigPath("proxy.config.socks.socks_config_file");
  Debug("Socks", "Socks Config File: %s", (const char *)config_pathname);

  if (!config_pathname) {
    Error("SOCKS Config: could not read config file name. SOCKS Turned off");
    goto error;
  }

  socks_config_fd = ::open(config_pathname, O_RDONLY);

  if (socks_config_fd < 0) {
    Error("SOCKS Config: could not open config file '%s'. SOCKS Turned off", (const char *)config_pathname);
    goto error;
  }
#ifdef SOCKS_WITH_TS
  tmp = Load_IpMap_From_File(&socks_conf_stuff->ip_map, socks_config_fd, "no_socks");

  if (tmp) {
    Error("SOCKS Config: Error while reading ip_range: %s.", tmp);
    ats_free(tmp);
    goto error;
  }
#endif

  if (loadSocksAuthInfo(socks_config_fd, socks_conf_stuff) != 0) {
    Error("SOCKS Config: Error while reading Socks auth info");
    goto error;
  }
  Debug("Socks", "Socks Turned on");
  ::close(socks_config_fd);

  return;
error:

  socks_conf_stuff->socks_needed   = 0;
  socks_conf_stuff->accept_enabled = 0;
  if (socks_config_fd >= 0) {
    ::close(socks_config_fd);
  }
}

int
loadSocksAuthInfo(int fd, socks_conf_struct *socks_stuff)
{
  char c              = '\0';
  char line[256]      = {0}; // initialize all chars to nil
  char user_name[256] = {0};
  char passwd[256]    = {0};

  if (lseek(fd, 0, SEEK_SET) < 0) {
    Warning("Can not seek on Socks configuration file\n");
    return -1;
  }

  bool end_of_file = false;
  do {
    int n = 0, rc;
    while (((rc = read(fd, &c, 1)) == 1) && (c != '\n') && (n < 254)) {
      line[n++] = c;
    }
    if (rc <= 0) {
      end_of_file = true;
    }
    line[n] = '\0';

    // coverity[secure_coding]
    rc = sscanf(line, " auth u %255s %255s ", user_name, passwd);
    if (rc >= 2) {
      int len1 = strlen(user_name);
      int len2 = strlen(passwd);

      Debug("Socks", "Read user_name(%s) and passwd(%s) from config file", user_name, passwd);

      socks_stuff->user_name_n_passwd_len = len1 + len2 + 2;

      char *ptr = static_cast<char *>(ats_malloc(socks_stuff->user_name_n_passwd_len));
      ptr[0]    = len1;
      memcpy(&ptr[1], user_name, len1);
      ptr[len1 + 1] = len2;
      memcpy(&ptr[len1 + 2], passwd, len2);

      socks_stuff->user_name_n_passwd = ptr;

      return 0;
    }
  } while (!end_of_file);

  return 0;
}

int
socks5BasicAuthHandler(int event, unsigned char *p, void (**h_ptr)(void))
{
  // for more info on Socks5 see RFC 1928
  int ret           = 0;
  char *pass_phrase = netProcessor.socks_conf_stuff->user_name_n_passwd;

  switch (event) {
  case SOCKS_AUTH_OPEN:
    p[ret++] = SOCKS5_VERSION;        // version
    p[ret++] = (pass_phrase) ? 2 : 1; //#Methods
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
        Debug("Socks", "No authentication required for Socks server");
        // make sure this is ok for us. right now it is always ok for us.
        *h_ptr = nullptr;
        break;

      case 2:
        Debug("Socks", "Socks server wants username/passwd");
        if (!pass_phrase) {
          Debug("Socks", "Buggy Socks server: asks for username/passwd "
                         "when not supplied as an option");
          ret    = -1;
          *h_ptr = nullptr;
        } else {
          *reinterpret_cast<SocksAuthHandler *>(h_ptr) = &socks5PasswdAuthHandler;
        }

        break;

      case 0xff:
        Debug("Socks", "None of the Socks authentications is acceptable "
                       "to the server");
        *h_ptr = nullptr;
        ret    = -1;
        break;

      default:
        Debug("Socks", "Unexpected Socks auth method (%d) from the server", (int)p[1]);
        ret = -1;
        break;
      }
    } else {
      Debug("Socks", "authEvent got wrong version %d from the Socks server", (int)p[0]);
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
  int ret = 0;
  char *pass_phrase;
  int pass_len;

  switch (event) {
  case SOCKS_AUTH_OPEN:
    pass_phrase = netProcessor.socks_conf_stuff->user_name_n_passwd;
    pass_len    = netProcessor.socks_conf_stuff->user_name_n_passwd_len;
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
      Debug("Socks", "Username/Passwd succeeded");
      *h_ptr = nullptr;
      break;

    default:
      Debug("Socks", "Username/Passwd authentication failed ret_code: %d", (int)p[1]);
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
