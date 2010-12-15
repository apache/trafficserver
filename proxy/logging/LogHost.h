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
//#if defined(IOCORE_LOG_COLLATION)
// ifdef removed so friend declaration won't break compile
// friend needed so LogCollationClientSM will compile
class LogCollationClientSM;
//#endif

#include "LogBufferSink.h"

/*-------------------------------------------------------------------------
  LogHost

  This is a new addition to the Traffic Server logging as of the 3.1
  (Panda) release.  This object corresponds to a named log collation host.
  -------------------------------------------------------------------------*/

class LogHost:public LogBufferSink
{

//#if defined(IOCORE_LOG_COLLATION)
  friend class LogCollationClientSM;
//#endif

public:
    LogHost(char *object_filename, uint64_t object_signature);
    LogHost(const LogHost &);
   ~LogHost();

  int set_name_or_ipstr(char *name_or_ipstr);
  int set_ipstr_port(char *ipstr, unsigned int port);
  int set_name_port(char *hostname, unsigned int port);

  bool connected(bool ping);
  bool connect();
  void disconnect();
  int write(LogBuffer * lb, size_t * to_disk, size_t * to_net, size_t * to_pipe);

  char *name()
  {
    return (char *) ((m_name) ? m_name : "UNKNOWN");
  }
  unsigned port()
  {
    return m_port;
  }
  unsigned ip()
  {
    return m_ip;
  }
  char *ipstr()
  {
    return m_ipstr;
  };

  void display(FILE * fd = stdout);
  LogFile *get_orphan_logfile()
  {
    return m_orphan_file;
  };
  // check if we will be able to write orphan file
  int do_filesystem_checks()
  {
    return m_orphan_file->do_filesystem_checks();
  };

private:
  void clear();
  bool authenticated();
  int orphan_write(LogBuffer * lb, size_t * to_disk = 0);
  int orphan_write_and_delete(LogBuffer * lb, size_t * to_disk = 0);
  void create_orphan_LogFile_object();

private:
  char *m_object_filename;
  uint64_t m_object_signature;
  unsigned m_ip;
  char *m_ipstr;
  char *m_name;
  int m_port;
  LogSock *m_sock;
  int m_sock_fd;
  bool m_connected;
  LogFile *m_orphan_file;
#if defined(IOCORE_LOG_COLLATION)
  LogCollationClientSM *m_log_collation_client_sm;
#endif

public:
  LINK(LogHost, link);

private:
  // -- member functions not allowed --
  LogHost();
  LogHost & operator=(const LogHost &);
};

/*-------------------------------------------------------------------------
  LogHostList
  -------------------------------------------------------------------------*/

class LogHostList:public LogBufferSink
{
public:
  LogHostList();
  ~LogHostList();

  void add(LogHost * host, bool copy = true);
  unsigned count();
  void clear();
  int write(LogBuffer * lb, size_t * to_disk = 0, size_t * to_net = 0, size_t * to_pipe = 0);

  LogHost *first()
  {
    return m_host_list.head;
  }
  LogHost *next(LogHost * here)
  {
    return (here->link).next;
  }

  void display(FILE * fd = stdout);
  bool operator==(LogHostList & rhs);
  int do_filesystem_checks();

private:
  Queue<LogHost> m_host_list;

  // -- member functions not allowed --
  LogHostList(const LogHostList &);
  LogHostList & operator=(const LogHostList &);
};

#endif
