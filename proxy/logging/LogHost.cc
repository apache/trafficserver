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

/***************************************************************************
 LogHost.cc


 ***************************************************************************/
#include "ts/ink_platform.h"

#include "Error.h"

#include "LogUtils.h"
#include "LogSock.h"
#include "LogField.h"
#include "LogFile.h"
#include "LogFormat.h"
#include "LogBuffer.h"
#include "LogHost.h"
#include "LogObject.h"
#include "LogConfig.h"
#include "Log.h"

#include "LogCollationClientSM.h"

#define PING true
#define NOPING false

/*-------------------------------------------------------------------------
  LogHost
  -------------------------------------------------------------------------*/

LogHost::LogHost(const char *object_filename, uint64_t object_signature)
  : m_object_filename(ats_strdup(object_filename)),
    m_object_signature(object_signature),
    m_port(0),
    m_name(NULL),
    m_sock(NULL),
    m_sock_fd(-1),
    m_connected(false),
    m_orphan_file(NULL),
    m_log_collation_client_sm(NULL)
{
  ink_zero(m_ip);
  ink_zero(m_ipstr);
}

LogHost::LogHost(const LogHost &rhs)
  : m_object_filename(ats_strdup(rhs.m_object_filename)),
    m_object_signature(rhs.m_object_signature),
    m_ip(rhs.m_ip),
    m_port(0),
    m_name(ats_strdup(rhs.m_name)),
    m_sock(NULL),
    m_sock_fd(-1),
    m_connected(false),
    m_orphan_file(NULL),
    m_log_collation_client_sm(NULL)
{
  memcpy(m_ipstr, rhs.m_ipstr, sizeof(m_ipstr));
  create_orphan_LogFile_object();
}

LogHost::~LogHost()
{
  clear();
  ats_free(m_object_filename);
}

//
// There are 3 ways to establish a LogHost:
// - by "hostname:port" or IP:port", where IP is a string of the
//   form "xxx.xxx.xxx.xxx".
// - by specifying a hostname and a port (as separate arguments).
// - by specifying an ip and a port (as separate arguments).
//
int
LogHost::set_name_port(char *hostname, unsigned int pt)
{
  if (!hostname || hostname[0] == 0) {
    Note("Cannot establish LogHost with NULL hostname");
    return 1;
  }

  clear(); // remove all previous state for this LogHost

  m_name = ats_strdup(hostname);
  m_port = pt;

  Debug("log-host", "LogHost established as %s:%u", this->name(), this->port());

  create_orphan_LogFile_object();
  return 0;
}

int
LogHost::set_ipstr_port(char *ipstr, unsigned int pt)
{
  if (!ipstr || ipstr[0] == 0) {
    Note("Cannot establish LogHost with NULL ipstr");
    return 1;
  }

  clear(); // remove all previous state for this LogHost

  if (0 != m_ip.load(ipstr))
    Note("Log host failed to parse IP address %s", ipstr);
  m_port = pt;
  ink_strlcpy(m_ipstr, ipstr, sizeof(m_ipstr));
  m_name = ats_strdup(ipstr);

  Debug("log-host", "LogHost established as %s:%u", name(), pt);

  create_orphan_LogFile_object();
  return 0;
}

int
LogHost::set_name_or_ipstr(char *name_or_ip)
{
  int retVal = 1;

  if (name_or_ip && name_or_ip[0] != 0) {
    ts::ConstBuffer addr, port;
    if (ats_ip_parse(ts::ConstBuffer(name_or_ip, strlen(name_or_ip)), &addr, &port) == 0) {
      uint16_t p = port ? atoi(port.data()) : Log::config->collation_port;
      char *n    = const_cast<char *>(addr.data());
      // Force termination. We know we can do this because the address
      // string is followed by either a nul or a colon.
      n[addr.size()] = 0;
      if (AF_UNSPEC == ats_ip_check_characters(addr)) {
        retVal = set_name_port(n, p);
      } else {
        retVal = set_ipstr_port(n, p);
      }
    }
  }
  return retVal;
}

bool
LogHost::connected(bool ping)
{
  if (m_connected && m_sock && m_sock_fd >= 0) {
    if (m_sock->is_connected(m_sock_fd, ping)) {
      return true;
    }
  }
  return false;
}

bool
LogHost::connect()
{
  if (!m_ip.isValid()) {
    Note("Cannot connect to LogHost; host IP has not been established");
    return false;
  }

  if (connected(PING)) {
    return true;
  }

  IpEndpoint target;
  ip_port_text_buffer ipb;
  target.assign(m_ip, htons(m_port));

  if (is_debug_tag_set("log-host")) {
    Debug("log-host", "Connecting to LogHost %s", ats_ip_nptop(&target, ipb, sizeof ipb));
  }

  disconnect(); // make sure connection members are initialized

  if (m_sock == NULL) {
    m_sock = new LogSock();
    ink_assert(m_sock != NULL);
  }
  m_sock_fd = m_sock->connect(&target.sa);
  if (m_sock_fd < 0) {
    Note("Connection to LogHost %s failed", ats_ip_nptop(&target, ipb, sizeof ipb));
    return false;
  }
  m_connected = true;

  if (!authenticated()) {
    Note("Authentication to LogHost %s failed", ats_ip_nptop(&target, ipb, sizeof ipb));
    disconnect();
    return false;
  }

  return true;
}

void
LogHost::disconnect()
{
  if (m_sock && m_sock_fd >= 0) {
    m_sock->close(m_sock_fd);
    m_sock_fd = -1;
  }
  if (m_log_collation_client_sm) {
    delete m_log_collation_client_sm;
    m_log_collation_client_sm = NULL;
  }
  m_connected = false;
}

void
LogHost::create_orphan_LogFile_object()
{
  delete m_orphan_file;

  const char *orphan_ext = "orphan";
  unsigned name_len      = (unsigned)(strlen(m_object_filename) + strlen(name()) + strlen(orphan_ext) + 16);
  char *name_buf         = (char *)ats_malloc(name_len);

  // NT: replace ':'s with '-'s.  This change is necessary because
  // NT doesn't like filenames with ':'s in them.  ^_^
  snprintf(name_buf, name_len, "%s%s%s-%u.%s", m_object_filename, LOGFILE_SEPARATOR_STRING, name(), port(), orphan_ext);

  // should check for conflicts with orphan filename
  //
  m_orphan_file = new LogFile(name_buf, NULL, LOG_FILE_ASCII, m_object_signature);
  ink_assert(m_orphan_file != NULL);
  ats_free(name_buf);
}

//
// preprocess the given buffer data before sent to target host
// and try to delete it when its reference become zero.
//
int
LogHost::preproc_and_try_delete(LogBuffer *lb)
{
  int ret = -1;

  if (lb == NULL) {
    Note("Cannot write LogBuffer to LogHost %s; LogBuffer is NULL", name());
    return -1;
  }
  LogBufferHeader *buffer_header = lb->header();
  if (buffer_header == NULL) {
    Note("Cannot write LogBuffer to LogHost %s; LogBufferHeader is NULL", name());
    goto done;
  }
  if (buffer_header->entry_count == 0) {
    // no bytes to write
    goto done;
  }

  // create a new collation client if necessary
  if (m_log_collation_client_sm == NULL) {
    m_log_collation_client_sm = new LogCollationClientSM(this);
    ink_assert(m_log_collation_client_sm != NULL);
  }

  // send log_buffer;
  if (m_log_collation_client_sm->send(lb) <= 0)
    goto done;

  return 0;

done:
  LogBuffer::destroy(lb);
  return ret;
}

//
// write the given buffer data to orhpan file and
// try to delete it when its reference become zero.
//
void
LogHost::orphan_write_and_try_delete(LogBuffer *lb)
{
  RecIncrRawStat(log_rsb, this_thread()->mutex->thread_holding, log_stat_num_lost_before_sent_to_network_stat,
                 lb->header()->entry_count);

  RecIncrRawStat(log_rsb, this_thread()->mutex->thread_holding, log_stat_bytes_lost_before_sent_to_network_stat,
                 lb->header()->byte_count);

  if (!Log::config->logging_space_exhausted) {
    Debug("log-host", "Sending LogBuffer to orphan file %s", m_orphan_file->get_name());
    m_orphan_file->preproc_and_try_delete(lb);
  } else {
    Debug("log-host", "logging space exhausted, failed to write orphan file, drop(%" PRIu32 ") bytes", lb->header()->byte_count);
    LogBuffer::destroy(lb);
  }
}

void
LogHost::display(FILE *fd)
{
  fprintf(fd, "LogHost: %s:%u, %s\n", name(), port(), (connected(NOPING)) ? "connected" : "not connected");
}

void
LogHost::clear()
{
  // close an established connection and clear the state of this host

  disconnect();

  ats_free(m_name);
  delete m_sock;
  m_orphan_file.clear();

  ink_zero(m_ip);
  m_port = 0;
  ink_zero(m_ipstr);
  m_name      = NULL;
  m_sock      = NULL;
  m_sock_fd   = -1;
  m_connected = false;
}

bool
LogHost::authenticated()
{
  if (!connected(NOPING)) {
    Note("Cannot authenticate LogHost %s; not connected", name());
    return false;
  }

  Debug("log-host", "Authenticating LogHost %s ...", name());
  char *auth_key        = Log::config->collation_secret;
  unsigned auth_key_len = (unsigned)::strlen(auth_key) + 1; // incl null
  int bytes             = m_sock->write(m_sock_fd, auth_key, auth_key_len);
  if ((unsigned)bytes != auth_key_len) {
    Debug("log-host", "... bad write on authenticate");
    return false;
  }

  Debug("log-host", "... authenticated");
  return true;
}

/*-------------------------------------------------------------------------
  LogHostList
  -------------------------------------------------------------------------*/

LogHostList::LogHostList()
{
}

LogHostList::~LogHostList()
{
  clear();
}

void
LogHostList::add(LogHost *object, bool copy)
{
  ink_assert(object != NULL);
  if (copy) {
    m_host_list.enqueue(new LogHost(*object));
  } else {
    m_host_list.enqueue(object);
  }
}

unsigned
LogHostList::count()
{
  unsigned cnt = 0;
  for (LogHost *host = first(); host; host = next(host)) {
    cnt++;
  }
  return cnt;
}

void
LogHostList::clear()
{
  LogHost *host;
  while ((host = m_host_list.dequeue())) {
    delete host;
  }
}

int
LogHostList::preproc_and_try_delete(LogBuffer *lb)
{
  int ret;
  unsigned nr_host, nr;
  bool need_orphan        = true;
  LogHost *available_host = NULL;

  ink_release_assert(lb->m_references == 0);

  nr_host = nr = count();
  ink_atomic_increment(&lb->m_references, nr_host);

  for (LogHost *host = first(); host && nr; host = next(host)) {
    LogHost *lh    = host;
    available_host = lh;

    do {
      ink_atomic_increment(&lb->m_references, 1);
      ret         = lh->preproc_and_try_delete(lb);
      need_orphan = need_orphan && (ret < 0);
    } while (ret < 0 && (lh = lh->failover_link.next));

    nr--;
  }

  if (need_orphan && available_host) {
    ink_atomic_increment(&lb->m_references, 1);
    available_host->orphan_write_and_try_delete(lb);
  }

  LogBuffer::destroy(lb);
  return 0;
}

void
LogHostList::display(FILE *fd)
{
  for (LogHost *host = first(); host; host = next(host)) {
    host->display(fd);
  }
}

bool
LogHostList::operator==(LogHostList &rhs)
{
  LogHost *host;
  for (host = first(); host; host = next(host)) {
    LogHost *rhs_host;
    for (rhs_host = rhs.first(); rhs_host; rhs_host = next(host)) {
      if ((host->port() == rhs_host->port() && host->ip_addr().isValid() && host->ip_addr() == rhs_host->ip_addr()) ||
          (host->name() && rhs_host->name() && (strcmp(host->name(), rhs_host->name()) == 0)) ||
          (*(host->ipstr()) && *(rhs_host->ipstr()) && (strcmp(host->ipstr(), rhs_host->ipstr()) == 0))) {
        break;
      }
    }
    if (rhs_host == NULL) {
      return false;
    }
  }
  return true;
}

int
LogHostList::do_filesystem_checks()
{
  for (LogHost *host = first(); host; host = next(host)) {
    if (host->do_filesystem_checks() < 0) {
      return -1;
    }
  }
  return 0;
}
