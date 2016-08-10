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
#ifndef LOG_HOST_H
#define LOG_HOST_H

class LogSock;
class LogBuffer;
class LogCollationClientSM;

#include "LogBufferSink.h"

/*-------------------------------------------------------------------------
  LogHost
  This object corresponds to a named log collation host.
  -------------------------------------------------------------------------*/
class LogHost
{
  friend class LogCollationClientSM;

public:
  LogHost(const char *object_filename, uint64_t object_signature);
  LogHost(const LogHost &);
  ~LogHost();

  bool set_name_or_ipstr(const char *name_or_ipstr);
  bool set_ipstr_port(const char *ipstr, unsigned int port);
  bool set_name_port(const char *hostname, unsigned int port);

  bool connected(bool ping);
  bool connect();
  void disconnect();
  //
  // preprocess the given buffer data before sent to target host
  // and try to delete it when its reference become zero.
  //
  int preproc_and_try_delete(LogBuffer *lb);

  //
  // write the given buffer data to orhpan file and
  // try to delete it when its reference become zero.
  //
  void orphan_write_and_try_delete(LogBuffer *lb);

  const char *
  name() const
  {
    return m_name ? m_name : "UNKNOWN";
  }

  uint64_t
  signature() const
  {
    return m_object_signature;
  }

  IpAddr const &
  ip_addr() const
  {
    return m_ip;
  }

  in_port_t
  port() const
  {
    return m_port;
  }

  char const *
  ipstr() const
  {
    return m_ipstr;
  }

  void display(FILE *fd = stdout);

  LogFile *
  get_orphan_logfile() const
  {
    return m_orphan_file;
  }
  // check if we will be able to write orphan file
  int
  do_filesystem_checks()
  {
    return m_orphan_file->do_filesystem_checks();
  }

private:
  void clear();
  bool authenticated();

private:
  char *m_object_filename;
  uint64_t m_object_signature;
  IpAddr m_ip;      // IP address, network order.
  in_port_t m_port; // IP port, host order.
  ip_text_buffer m_ipstr;
  char *m_name;
  LogSock *m_sock;
  int m_sock_fd;
  bool m_connected;
  Ptr<LogFile> m_orphan_file;
  LogCollationClientSM *m_log_collation_client_sm;

public:
  LINK(LogHost, link);
  SLINK(LogHost, failover_link);

private:
  // -- member functions not allowed --
  LogHost();
  LogHost &operator=(const LogHost &);
};

/*-------------------------------------------------------------------------
  LogHostList
  -------------------------------------------------------------------------*/
class LogHostList : public LogBufferSink
{
public:
  LogHostList();
  ~LogHostList();

  void add(LogHost *host, bool copy = true);
  unsigned count();
  void clear();
  int preproc_and_try_delete(LogBuffer *lb);

  LogHost *
  first()
  {
    return m_host_list.head;
  }

  LogHost *
  next(LogHost *here)
  {
    return (here->link).next;
  }

  void display(FILE *fd = stdout);
  bool operator==(LogHostList &rhs);
  int do_filesystem_checks();

private:
  Queue<LogHost> m_host_list;

  // -- member functions not allowed --
  LogHostList(const LogHostList &);
  LogHostList &operator=(const LogHostList &);
};

#endif
