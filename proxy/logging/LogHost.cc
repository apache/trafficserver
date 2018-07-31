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

static Ptr<LogFile>
make_orphan_logfile(LogHost *lh, const char *filename)
{
  const char *ext   = "orphan";
  unsigned name_len = (unsigned)(strlen(filename) + strlen(lh->name()) + strlen(ext) + 16);
  char *name_buf    = (char *)ats_malloc(name_len);

  // NT: replace ':'s with '-'s.  This change is necessary because
  // NT doesn't like filenames with ':'s in them.  ^_^
  snprintf(name_buf, name_len, "%s%s%s-%u.%s", filename, LOGFILE_SEPARATOR_STRING, lh->name(), lh->port(), ext);

  // XXX should check for conflicts with orphan filename

  Ptr<LogFile> orphan(new LogFile(name_buf, nullptr, LOG_FILE_ASCII, lh->signature()));

  ats_free(name_buf);
  return orphan;
}

/*-------------------------------------------------------------------------
  LogHost
  -------------------------------------------------------------------------*/

LogHost::LogHost(const char *object_filename, uint64_t object_signature)
  : m_object_filename(ats_strdup(object_filename)),
    m_object_signature(object_signature),
    m_port(0),
    m_name(nullptr),
    m_sock(nullptr),
    m_sock_fd(-1),
    m_connected(false),
    m_orphan_file(nullptr),
    m_log_collation_client_sm(nullptr)
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
    m_sock(nullptr),
    m_sock_fd(-1),
    m_connected(false),
    m_orphan_file(nullptr),
    m_log_collation_client_sm(nullptr)
{
  memcpy(m_ipstr, rhs.m_ipstr, sizeof(m_ipstr));
  m_orphan_file = make_orphan_logfile(this, m_object_filename);
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
bool
LogHost::set_name_port(const char *hostname, unsigned int pt)
{
  if (!hostname || hostname[0] == 0) {
    Note("Cannot establish LogHost with NULL hostname");
    return false;
  }

  clear(); // remove all previous state for this LogHost

  m_name = ats_strdup(hostname);
  m_port = pt;

  Debug("log-host", "LogHost established as %s:%u", this->name(), this->port());

  m_orphan_file = make_orphan_logfile(this, m_object_filename);
  return true;
}

bool
LogHost::set_ipstr_port(const char *ipstr, unsigned int pt)
{
  if (!ipstr || ipstr[0] == 0) {
    Note("Cannot establish LogHost with NULL ipstr");
    return false;
  }

  clear(); // remove all previous state for this LogHost

  if (0 != m_ip.load(ipstr)) {
    Note("Log host failed to parse IP address %s", ipstr);
  }

  m_port = pt;
  ink_strlcpy(m_ipstr, ipstr, sizeof(m_ipstr));
  m_name = ats_strdup(ipstr);

  Debug("log-host", "LogHost established as %s:%u", name(), pt);

  m_orphan_file = make_orphan_logfile(this, m_object_filename);
  return true;
}

bool
LogHost::set_name_or_ipstr(const char *name_or_ip)
{
  if (name_or_ip && name_or_ip[0] != '\0') {
    std::string_view addr, port;
    if (ats_ip_parse(std::string_view(name_or_ip), &addr, &port) == 0) {
      uint16_t p = port.empty() ? Log::config->collation_port : atoi(port.data());
      char *n    = const_cast<char *>(addr.data());
      // Force termination. We know we can do this because the address
      // string is followed by either a nul or a colon.
      n[addr.size()] = 0;
      if (AF_UNSPEC == ats_ip_check_characters(addr)) {
        return set_name_port(n, p);
      } else {
        return set_ipstr_port(n, p);
      }
    }
  }

  return false;
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

  if (m_sock == nullptr) {
    m_sock = new LogSock();
    ink_assert(m_sock != nullptr);
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
    m_log_collation_client_sm = nullptr;
  }
  m_connected = false;
}

//
// preprocess the given buffer data before sent to target host
// and try to delete it when its reference become zero.
//
bool
LogHost::preproc_and_try_delete(LogBuffer *&lb)
{
  if (lb == nullptr) {
    Note("Cannot write LogBuffer to LogHost %s; LogBuffer is NULL", name());
    return false;
  }

  LogBufferHeader *buffer_header = lb->header();
  if (buffer_header == nullptr) {
    Note("Cannot write LogBuffer to LogHost %s; LogBufferHeader is NULL", name());
    goto done;
  }

  if (buffer_header->entry_count == 0) {
    // no bytes to write
    goto done;
  }

  // create a new collation client if necessary
  if (m_log_collation_client_sm == nullptr) {
    m_log_collation_client_sm = new LogCollationClientSM(this);
    ink_assert(m_log_collation_client_sm != nullptr);
  }

  // send log_buffer
  if (m_log_collation_client_sm->send(lb) <= 0) {
    goto done;
  }

  return true;

done:
  LogBuffer::destroy(lb);
  return false;
}

//
// write the given buffer data to orphan file and
// try to delete it when its reference become zero.
//
void
LogHost::orphan_write_and_try_delete(LogBuffer *&lb)
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
  }
  LogBuffer::destroy(lb);
}

void
LogHost::display(FILE *fd)
{
  fprintf(fd, "LogHost: %s:%u, %s\n", name(), port(), (connected(NOPING)) ? "connected" : "not connected");

  LogHost *host = this;
  while (host->failover_link.next != nullptr) {
    fprintf(fd, "Failover: %s:%u, %s\n", host->name(), host->port(), (host->connected(NOPING)) ? "connected" : "not connected");
    host = host->failover_link.next;
  }
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
  m_name      = nullptr;
  m_sock      = nullptr;
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

LogHostList::LogHostList() {}

LogHostList::~LogHostList()
{
  clear();
}

void
LogHostList::add(LogHost *object, bool copy)
{
  ink_assert(object != nullptr);
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
  int success = false;
  unsigned nr_host, nr;
  bool need_orphan        = true;
  LogHost *available_host = nullptr;

  ink_release_assert(lb->m_references == 0);

  nr_host = nr = count();
  ink_atomic_increment(&lb->m_references, nr_host);

  for (LogHost *host = first(); host && nr; host = next(host)) {
    LogHost *lh    = host;
    available_host = lh;

    do {
      ink_atomic_increment(&lb->m_references, 1);
      success     = lh->preproc_and_try_delete(lb);
      need_orphan = need_orphan && (success == false);
    } while (lb && (success == false) && (lh = lh->failover_link.next));

    nr--;
  }

  if (lb != nullptr && need_orphan && available_host) {
    ink_atomic_increment(&lb->m_references, 1);
    available_host->orphan_write_and_try_delete(lb);
  }

  if (lb != nullptr) {
    LogBuffer::destroy(lb);
  }
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
    if (rhs_host == nullptr) {
      return false;
    }
  }
  return true;
}
