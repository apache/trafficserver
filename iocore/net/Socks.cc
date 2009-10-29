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

socks_conf_struct *g_socks_conf_stuff = 0;

ClassAllocator<SocksEntry> socksAllocator("socksAllocator");

void
SocksEntry::init(ProxyMutex * m, SocksNetVC * vc, unsigned char socks_support, unsigned char ver)
{
  mutex = m;
  buf = new_MIOBuffer();
  reader = buf->alloc_reader();

  socks_cmd = socks_support;

  if (ver == SOCKS_DEFAULT_VERSION)
    version = netProcessor.socks_conf_stuff->default_version;
  else
    version = ver;

  SET_HANDLER(&SocksEntry::startEvent);

  ip = vc->ip;
  port = vc->port;

#ifdef SOCKS_WITH_TS
  req_data.hdr = 0;
  req_data.hostname_str = 0;
  req_data.api_info = 0;
  req_data.xact_start = time(0);
  req_data.dest_ip = ip;

  //we dont have information about the source. set to destination's
  req_data.src_ip = ip;
  req_data.incoming_port = port;

  server_params = SocksServerConfig::acquire();
#endif

  nattempts = 0;
  findServer();

  timeout = this_ethread()->schedule_in(this, HRTIME_SECONDS(netProcessor.socks_conf_stuff->server_connect_timeout));
  write_done = false;
}

void
SocksEntry::findServer()
{
  nattempts++;

#ifdef SOCKS_WITH_TS
  if (nattempts == 1) {
    ink_debug_assert(server_result.r == PARENT_UNDEFINED);
    server_params->findParent(&req_data, &server_result);
  } else {

    socks_conf_struct *conf = netProcessor.socks_conf_stuff;
    if ((nattempts - 1) % conf->per_server_connection_attempts)
      return;                   //attempt again

    server_params->markParentDown(&server_result);

    if (nattempts > conf->connection_attempts)
      server_result.r = PARENT_FAIL;
    else
      server_params->nextParent(&req_data, &server_result);
  }

  switch (server_result.r) {
  case PARENT_SPECIFIED:
    server_ip = inet_addr(server_result.hostname);
    server_port = server_result.port;
    break;

  default:
    ink_debug_assert(!"Unexpected event");
  case PARENT_DIRECT:
  case PARENT_FAIL:
    server_ip = (unsigned int) -1;
  }
#else
  server_ip = (nattempts > netProcessor.socks_conf_stuff->connection_attempts)
    ? (unsigned int) -1 : g_socks_conf_stuff->socks_server;
  server_port = g_socks_conf_stuff->socks_server_port;
#endif // SOCKS_WITH_TS

  Debug("SocksParents", "findServer result: %u.%u.%u.%u:%d", PRINT_IP(server_ip), server_port);
}

void
SocksEntry::free()
{
  MUTEX_TRY_LOCK(lock, action_.mutex, this_ethread());
  if (!lock) {
    // Socks continuation share the user's lock
    // so acquiring a lock shouldn't fail
    ink_debug_assert(0);
    return;
  }

  if (timeout)
    timeout->cancel(this);

#ifdef SOCKS_WITH_TS
  if (!lerrno && netVConnection && server_result.retry)
    server_params->recordRetrySuccess(&server_result);
#endif

  if ((action_.cancelled || lerrno) && netVConnection)
    netVConnection->do_io_close();

  if (!action_.cancelled) {
    if (lerrno) {
      Debug("Socks", "retryevent: Sent errno %d to HTTP", lerrno);
      NET_INCREMENT_DYN_STAT(socks_connections_unsuccessful_stat);
      action_.continuation->handleEvent(NET_EVENT_OPEN_FAILED, (void *) (-lerrno));
    } else {
      netVConnection->do_io_read(this, 0, 0);
      netVConnection->do_io_write(this, 0, 0);
      netVConnection->action_ = action_;        //assign the original continuation
      netVConnection->ip = ip;
      netVConnection->port = port;      // we already have the lock for the continuation
      Debug("Socks", "Sent success to HTTP");
      NET_INCREMENT_DYN_STAT(socks_connections_successful_stat);
      action_.continuation->handleEvent(NET_EVENT_OPEN, netVConnection);
    }
  }
#ifdef SOCKS_WITH_TS
  SocksServerConfig::release(server_params);
#endif

  free_MIOBuffer(buf);
  action_ = NULL;
  mutex = NULL;
  socksAllocator.free(this);
}

int
SocksEntry::startEvent(int event, void *data)
{
  if (event == NET_EVENT_OPEN) {
    netVConnection = (SocksNetVC *) data;

    if (version == SOCKS5_VERSION)
      auth_handler = &socks5BasicAuthHandler;

    SET_HANDLER((SocksEntryHandler) & SocksEntry::mainEvent);
    mainEvent(NET_EVENT_OPEN, data);
  } else {
    if (timeout) {
      timeout->cancel(this);
      timeout = NULL;
    }

    Debug("Socks", "Failed to connect to %u.%u.%u.%u:%d", PRINT_IP(server_ip), server_port);

    findServer();

    if (server_ip == (inku32) - 1) {
      Debug("Socks", "Unable to open connection to the SOCKS server");
      lerrno = ESOCK_NO_SOCK_SERVER_CONN;
      free();
      return EVENT_CONT;
    }

    if (timeout) {
      timeout->cancel(this);
      timeout = 0;
    }

    if (netVConnection) {
      netVConnection->do_io_close();
      netVConnection = 0;
    }

    timeout = this_ethread()->schedule_in(this, HRTIME_SECONDS(netProcessor.socks_conf_stuff->server_connect_timeout));

    write_done = false;

    NetVCOptions options;
    options.socks_support = NO_SOCKS;
    netProcessor.connect_re(this, server_ip, server_port, 0, &options);
  }

  return EVENT_CONT;
}

int
SocksEntry::mainEvent(int event, void *data)
{
  int ret = EVENT_DONE;
  int n_bytes = 0;
  unsigned char *p;

  switch (event) {

  case NET_EVENT_OPEN:
    buf->reset();
    unsigned short ts;
    unsigned long tl;
    p = (unsigned char *) buf->start();
    ink_debug_assert(netVConnection);

    if (auth_handler) {
      n_bytes = invokeSocksAuthHandler(auth_handler, SOCKS_AUTH_OPEN, p);
    } else {

      //Debug("Socks", " Got NET_EVENT_OPEN to SOCKS server\n");

      p[n_bytes++] = version;
      p[n_bytes++] = (socks_cmd == NORMAL_SOCKS) ? SOCKS_CONNECT : socks_cmd;
      ts = (unsigned short) htons(port);
      tl = (unsigned long) ip;

      if (version == SOCKS5_VERSION) {
        p[n_bytes++] = 0;       //Reserved
        p[n_bytes++] = 1;       //IPv4 addr
        p[n_bytes++] = ((unsigned char *) &ip)[0];
        p[n_bytes++] = ((unsigned char *) &ip)[1];
        p[n_bytes++] = ((unsigned char *) &ip)[2];
        p[n_bytes++] = ((unsigned char *) &ip)[3];
      }

      p[n_bytes++] = ((unsigned char *) &ts)[0];
      p[n_bytes++] = ((unsigned char *) &ts)[1];

      if (version == SOCKS4_VERSION) {
        //for socks4, ip addr is after the port
        p[n_bytes++] = ((unsigned char *) &ip)[0];
        p[n_bytes++] = ((unsigned char *) &ip)[1];
        p[n_bytes++] = ((unsigned char *) &ip)[2];
        p[n_bytes++] = ((unsigned char *) &ip)[3];

        p[n_bytes++] = 0;       // NULL
      }

    }

    buf->fill(n_bytes);

    if (!timeout) {
      /* timeout would be already set when we come here from StartEvent() */
      timeout = this_ethread()->schedule_in(this, HRTIME_SECONDS(netProcessor.socks_conf_stuff->socks_timeout));
    }

    netVConnection->do_io_write(this, n_bytes, reader, 0);
    //Debug("Socks", "Sent the request to the SOCKS server\n");

    ret = EVENT_CONT;
    break;

  case VC_EVENT_WRITE_READY:

    ret = EVENT_CONT;
    break;

  case VC_EVENT_WRITE_COMPLETE:
    if (timeout) {
      timeout->cancel(this);
      timeout = NULL;
      write_done = true;
    }

    buf->reset();               // Use the same buffer for a read now

    if (auth_handler)
      n_bytes = invokeSocksAuthHandler(auth_handler, SOCKS_AUTH_WRITE_COMPLETE, NULL);
    else if (socks_cmd == NORMAL_SOCKS)
      n_bytes = (version == SOCKS5_VERSION)
        ? SOCKS5_REP_LEN : SOCKS4_REP_LEN;
    else {
      Debug("Socks", "Tunnelling the connection");
      //let the client handle the response
      free();
      break;
    }

    timeout = this_ethread()->schedule_in(this, HRTIME_SECONDS(netProcessor.socks_conf_stuff->socks_timeout));

    netVConnection->do_io_read(this, n_bytes, buf);

    ret = EVENT_DONE;
    break;

  case VC_EVENT_READ_READY:
    ret = EVENT_CONT;

    if (version == SOCKS5_VERSION && auth_handler == NULL) {
      VIO *vio = (VIO *) data;
      p = (unsigned char *) buf->start();

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
          Debug("Socks", "Illegal address type(%d) in Socks server", (int) p[3]);
        }

        if (vio->ndone >= reply_len) {
          vio->set_nbytes(vio->ndone);
          ret = EVENT_DONE;
        }
      }
    }

    if (ret == EVENT_CONT)
      break;
    // Fall Through 
  case VC_EVENT_READ_COMPLETE:
    if (timeout) {
      timeout->cancel(this);
      timeout = NULL;
    }
    //Debug("Socks", "Successfully read the reply from the SOCKS server\n");
    p = (unsigned char *) buf->start();

    if (auth_handler) {
      SocksAuthHandler temp = auth_handler;

      if (invokeSocksAuthHandler(auth_handler, SOCKS_AUTH_READ_COMPLETE, p) < 0) {
        lerrno = ESOCK_DENIED;
        free();
      } else if (auth_handler != temp) {
        // here either authorization is done or there is another
        // stage left.
        mainEvent(NET_EVENT_OPEN, NULL);
      }

    } else {

      bool success;
      if (version == SOCKS5_VERSION) {
        success = (p[0] == SOCKS5_VERSION && p[1] == SOCKS5_REQ_GRANTED);
        Debug("Socks", "received reply of length %d addr type %d", ((VIO *) data)->ndone, (int) p[3]);
      } else
        success = (p[0] == 0 && p[1] == SOCKS4_REQ_GRANTED);

      //ink_debug_assert(*(p) == 0);
      if (!success) {           // SOCKS request failed
        Debug("Socks", "Socks request denied %d", (int) *(p + 1));
        lerrno = ESOCK_DENIED;
      } else {
        Debug("Socks", "Socks request successful %d", (int) *(p + 1));
        lerrno = 0;
      }
      free();
    }

    break;

  case EVENT_INTERVAL:
    timeout = NULL;
    if (write_done) {
      lerrno = ESOCK_TIMEOUT;
      free();
      break;
    }
    /* else
       This is server_connect_timeout. So we treat this as server being
       down.
       Should cancel any pending connect() action. Important on windows

       fall through
     */
  case VC_EVENT_ERROR:
    /*This is mostly ECONNREFUSED on Unix */
    SET_HANDLER(&SocksEntry::startEvent);
    startEvent(NET_EVENT_OPEN_FAILED, NULL);
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
    ink_debug_assert(!"bad case value");
    Debug("Socks", "Bad Case/Net Error Event");
    lerrno = ESOCK_NO_SOCK_SERVER_CONN;
    free();
    break;
  }

  return ret;
}

#define SOCKS_REG_STAT(stat)						\
  RecRegisterRawStat(net_rsb, RECT_PROCESS,				\
		     "proxy.process.socks." #stat, RECD_INT, RECP_NULL,	\
		     socks_##stat##_stat, RecRawStatSyncSum)

void
loadSocksConfiguration(socks_conf_struct * socks_conf_stuff)
{
  int socks_config_fd = -1;
  char error_msg[512];
  char config_pathname[512];
  char *socks_config_file = 0, *config_dir = 0, *temp_str = 0;

  error_msg[0] = 0;

  socks_conf_stuff->accept_enabled = 0; //initialize it INKqa08593 
  socks_conf_stuff->socks_needed = IOCORE_ConfigReadInteger("proxy.config.socks.socks_needed");
  if (!socks_conf_stuff->socks_needed) {
    Debug("Socks", "Socks Turned Off");
    return;
  }

  socks_conf_stuff->default_version = IOCORE_ConfigReadInteger("proxy.config.socks.socks_version");
  Debug("Socks", "Socks Version %d", socks_conf_stuff->default_version);

  if (socks_conf_stuff->default_version != 4 && socks_conf_stuff->default_version != 5) {
    snprintf(error_msg, 512, "Unsupported Version: %d", socks_conf_stuff->default_version);
    goto error;
  }

  socks_conf_stuff->server_connect_timeout = IOCORE_ConfigReadInteger("proxy.config.socks.server_connect_timeout");
  socks_conf_stuff->socks_timeout = IOCORE_ConfigReadInteger("proxy.config.socks.socks_timeout");
  Debug("Socks", "server connect timeout: %d socks respnonse timeout %d",
        socks_conf_stuff->server_connect_timeout, socks_conf_stuff->socks_timeout);

  socks_conf_stuff->per_server_connection_attempts =
    IOCORE_ConfigReadInteger("proxy.config.socks.per_server_connection_attempts");
  socks_conf_stuff->connection_attempts = IOCORE_ConfigReadInteger("proxy.config.socks.connection_attempts");

  socks_conf_stuff->accept_enabled = IOCORE_ConfigReadInteger("proxy.config.socks.accept_enabled");
  socks_conf_stuff->accept_port = IOCORE_ConfigReadInteger("proxy.config.socks.accept_port");
  socks_conf_stuff->http_port = IOCORE_ConfigReadInteger("proxy.config.socks.http_port");
  Debug("SocksProxy", "Read SocksProxy info: accept_enabled = %d "
        "accept_port = %d http_port = %d", socks_conf_stuff->accept_enabled,
        socks_conf_stuff->accept_port, socks_conf_stuff->http_port);

#ifdef SOCKS_WITH_TS
  SocksServerConfig::startup();
#endif

  socks_config_file = IOCORE_ConfigReadString("proxy.config.socks.socks_config_file");
  config_dir = IOCORE_ConfigReadString("proxy.config.config_dir");

  if (!socks_config_file || !config_dir) {
    snprintf(error_msg, 512, "could not read config file name");
    goto error;
  }

  snprintf(config_pathname, 512, "%.128s%s%.128s", config_dir, DIR_SEP, socks_config_file);
  Debug("Socks", "Socks Config File: %s", config_pathname);

  socks_config_fd =::open(config_pathname, O_RDONLY);

  if (socks_config_fd < 0) {
    snprintf(error_msg, 512, "could not open config file '%s'", config_pathname);
    goto error;
  }
#ifdef SOCKS_WITH_TS
  temp_str = socks_conf_stuff->ip_range.read_table_from_file(socks_config_fd, "no_socks");

  if (temp_str) {
    snprintf(error_msg, 512, "Error while reading ip_range: %.256s", temp_str);
    goto error;
  }
#endif

  if (loadSocksAuthInfo(socks_config_fd, socks_conf_stuff) != 0) {
    snprintf(error_msg, 512, "Error while reading Socks auth info");
    goto error;
  }

error:
  if (error_msg[0]) {
    Error("SOCKS Config: %s. SOCKS Turned off", error_msg);
    socks_conf_stuff->socks_needed = 0;
    socks_conf_stuff->accept_enabled = 0;
  } else {
    Debug("Socks", "Socks Turned on");
    SOCKS_REG_STAT(connections_successful);
    SOCKS_REG_STAT(connections_unsuccessful);
    SOCKS_REG_STAT(connections_currently_open);
  }

  if (socks_config_fd >= 0)
    ::close(socks_config_fd);

  if (socks_config_file)
    xfree(socks_config_file);
  if (config_dir)
    xfree(config_dir);
  if (temp_str)
    xfree(temp_str);
}

int
loadSocksAuthInfo(int fd, socks_conf_struct * socks_stuff)
{
  char c = '\0';
  char line[256] = { 0 };       // initialize all chars to nil
  char user_name[256] = { 0 };
  char passwd[256] = { 0 };

  if (lseek(fd, 0, SEEK_SET) < 0) {
    Warning("Can not seek on Socks configuration file\n");
    return -1;
  }

  bool end_of_file = false;
  do {
    int n = 0, rc;
    while (((rc = read(fd, &c, 1)) == 1) && (c != '\n') && (n < 254))
      line[n++] = c;
    if (rc <= 0)
      end_of_file = true;
    line[n] = '\0';

    // coverity[secure_coding]
    rc = sscanf(line, " auth u %255s %255s ", user_name, passwd);
    if (rc >= 2) {
      int len1 = strlen(user_name);
      int len2 = strlen(passwd);

      Debug("Socks", "Read user_name(%s) and passwd(%s) from config file", user_name, passwd);

      socks_stuff->user_name_n_passwd_len = len1 + len2 + 2;

      char *ptr = (char *) xmalloc(socks_stuff->user_name_n_passwd_len);
      ptr[0] = len1;
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
socks5BasicAuthHandler(int event, unsigned char *p, void (**h_ptr) (void))
{
  //for more info on Socks5 see RFC 1928
  int ret = 0;
  char *pass_phrase = netProcessor.socks_conf_stuff->user_name_n_passwd;

  switch (event) {

  case SOCKS_AUTH_OPEN:
    p[ret++] = SOCKS5_VERSION;  //version
    p[ret++] = (pass_phrase) ? 2 : 1;   //#Methods
    p[ret++] = 0;               //no authentication
    if (pass_phrase)
      p[ret++] = 2;

    break;

  case SOCKS_AUTH_WRITE_COMPLETE:
    //return number of bytes to read
    ret = 2;
    break;

  case SOCKS_AUTH_READ_COMPLETE:

    if (p[0] == SOCKS5_VERSION) {
      switch (p[1]) {

      case 0:                  // no authentication required
        Debug("Socks", "No authentication required for Socks server");
        //make sure this is ok for us. right now it is always ok for us.
        *h_ptr = NULL;
        break;

      case 2:
        Debug("Socks", "Socks server wants username/passwd");
        if (!pass_phrase) {
          Debug("Socks", "Buggy Socks server: asks for username/passwd " "when not supplied as an option");
          ret = -1;
          *h_ptr = NULL;
        } else
          *(SocksAuthHandler *) h_ptr = &socks5PasswdAuthHandler;

        break;

      case 0xff:
        Debug("Socks", "None of the Socks authentcations is acceptable " "to the server");
        *h_ptr = NULL;
        ret = -1;
        break;

      default:
        Debug("Socks", "Unexpected Socks auth method (%d) from the server", (int) p[1]);
        ret = -1;
        break;
      }
    } else {
      Debug("Socks", "authEvent got wrong version %d from the Socks server", (int) p[0]);
      ret = -1;
    }

    break;

  default:
    //This should be inpossible
    ink_debug_assert(!"bad case value");
    ret = -1;
    break;
  }
  return ret;
}

int
socks5PasswdAuthHandler(int event, unsigned char *p, void (**h_ptr) (void))
{
  //for more info see RFC 1929
  int ret = 0;
  char *pass_phrase;
  int pass_len;

  switch (event) {

  case SOCKS_AUTH_OPEN:
    pass_phrase = netProcessor.socks_conf_stuff->user_name_n_passwd;
    pass_len = netProcessor.socks_conf_stuff->user_name_n_passwd_len;
    ink_debug_assert(pass_phrase);

    p[0] = 1;                   //version
    memcpy(&p[1], pass_phrase, pass_len);

    ret = 1 + pass_len;
    break;

  case SOCKS_AUTH_WRITE_COMPLETE:
    //return number of bytes to read
    ret = 2;
    break;

  case SOCKS_AUTH_READ_COMPLETE:

    //if (p[0] == 1) { // skip this. its not clear what this should be.
    // NEC thinks it is 5 RFC seems to indicate 1.
    switch (p[1]) {

    case 0:
      Debug("Socks", "Username/Passwd succeded");
      *h_ptr = NULL;
      break;

    default:
      Debug("Socks", "Username/Passwd authentication failed ret_code: %d", (int) p[1]);
      ret = -1;
    }
    //}
    //else {
    //  Debug("Socks", "authPassEvent got wrong version %d from "
    //        "Socks server\n", (int)p[0]);
    //  ret = -1;
    //}

    break;

  default:
    ink_debug_assert(!"bad case value");
    ret = -1;
    break;
  }
  return ret;
}
