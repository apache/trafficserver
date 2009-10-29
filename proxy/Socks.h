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


#ifndef _SOCKS_H_
#define _SOCKS_H_

#include "ParentSelection.h"
#include "IOBuffer.h"
#include "Action.h"
#include  "Event.h"
#include "IPRange.h"

#define SOCKS_DEFAULT_VERSION 0 //defined the configuration variable
#define SOCKS4_VERSION  4
#define SOCKS5_VERSION 5
#define SOCKS_CONNECT  1
#define SOCKS4_REQ_LEN  9
#define SOCKS4_REP_LEN  8
#define SOCKS5_REP_LEN 262      //maximum possible
#define SOCKS4_REQ_GRANTED 90
#define SOCKS4_CONN_FAILED 91
#define SOCKS5_REQ_GRANTED 0
#define SOCKS5_CONN_FAILED 1

enum
{
  //types of events for Socks auth handlers
  SOCKS_AUTH_OPEN,
  SOCKS_AUTH_WRITE_COMPLETE,
  SOCKS_AUTH_READ_COMPLETE,
  SOCKS_AUTH_FILL_WRITE_BUF
};

enum
{
  //For these two, we need to pick two values which are not used for any of the
  //"commands" (eg: CONNECT, BIND) in SOCKS protocols.
  NORMAL_SOCKS = 0,
  NO_SOCKS = 48
};

enum
{
  SOCKS_ATYPE_NONE = 0,
  SOCKS_ATYPE_IPV4 = 1,
  SOCKS_ATYPE_FQHN = 3,
  SOCKS_ATYPE_IPV6 = 4
};

struct socks_conf_struct
{
  int socks_needed;
  int server_connect_timeout;
  int socks_timeout;
  unsigned char default_version;
  IPRange ip_range;
  char *user_name_n_passwd;
  int user_name_n_passwd_len;

  int per_server_connection_attempts;
  int connection_attempts;

  //the following ports are used by SocksProxy
  int accept_enabled;
  int accept_port;
  unsigned short http_port;

    socks_conf_struct():user_name_n_passwd(NULL), user_name_n_passwd_len(0)
  {
}}
 ;

extern struct socks_conf_struct *g_socks_conf_stuff;

void start_SocksProxy(int port);

int loadSocksAuthInfo(int fd, socks_conf_struct * socks_stuff);

// umm.. the following typedef should take _its own_ type as one of the args
// not possible with C
// Right now just use a generic fn ptr and hide casting in an inline fn.
typedef int (*SocksAuthHandler) (int event, unsigned char *buf, void (**h_ptr) (void));

inline int
invokeSocksAuthHandler(SocksAuthHandler & h, int arg1, unsigned char *arg2)
{
  return (h) (arg1, arg2, (void (**)(void)) (&h));
}

void loadSocksConfiguration(socks_conf_struct * socks_conf_stuff);
int socks5BasicAuthHandler(int event, unsigned char *p, void (**)(void));
int socks5PasswdAuthHandler(int event, unsigned char *p, void (**)(void));
int socks5ServerAuthHandler(int event, unsigned char *p, void (**)(void));


struct SocksAddrType
{
  unsigned char type;
  union
  {
    //mostly it is ipv4. in other cases we will xalloc().
    unsigned char ipv4[4];
    unsigned char *buf;
  } addr;

  void reset()
  {
    if (type != SOCKS_ATYPE_IPV4 && addr.buf) {
      xfree(addr.buf);
    }
    addr.buf = 0;
    type = SOCKS_ATYPE_NONE;
  }

  SocksAddrType()
:  type(SOCKS_ATYPE_NONE) {
    addr.buf = 0;
  };
  ~SocksAddrType() {
    reset();
  }
};

class UnixNetVConnection;
typedef UnixNetVConnection SocksNetVC;

struct SocksEntry:public Continuation
{                               //enum { SOCKS_INIT=0, SOCKS_SEND, SOCKS_RECV };

  MIOBuffer *buf;
  IOBufferReader *reader;

  SocksNetVC *netVConnection;

  //VIO          *    read_vio;

  unsigned int ip;              // ip address in the original request
  int port;                     // port number in the original request

  unsigned int server_ip;
  int server_port;
  int nattempts;

  Action action_;
  //int               state;
  int lerrno;
  Event *timeout;
  unsigned char version;

  bool write_done;

  SocksAuthHandler auth_handler;
  unsigned char socks_cmd;

  //socks server selection:
  ParentConfigParams *server_params;
  HttpRequestData req_data;     //We dont use any http specific fields.
  ParentResult server_result;

  int startEvent(int event, void *data);
  int mainEvent(int event, void *data);
  void findServer();
  void init(ProxyMutex * m, SocksNetVC * netvc, unsigned char socks_support, unsigned char ver);
  void free();

    SocksEntry():Continuation(NULL), netVConnection(0), lerrno(0), timeout(0), auth_handler(NULL)
  {
}}
 ;

typedef int (SocksEntry::*SocksEntryHandler) (int, void *);

extern ClassAllocator<SocksEntry> socksAllocator;

#if 0
struct ChangeSocksConfig:Continuation
{
  int change_config_handler(int etype, void *data)
  {
    socks_conf_struct *new_socks_conf_stuff, *old_socks_conf_stuff;
      (void) data;
      ink_assert(etype == EVENT_IMMEDIATE);
      new_socks_conf_stuff = NEW(new socks_conf_struct);
      loadSocksConfiguration(new_socks_conf_stuff);
      old_socks_conf_stuff = netProcessor.socks_conf_stuff;
      netProcessor.socks_conf_stuff = new_socks_conf_stuff;
    if (file_changed)
        pmgmt->signalManager(MGMT_SIGNAL_CONFIG_FILE_READ, "Socks Config File");
    delete this;
      return EVENT_DONE;
  }
  int file_changed;
ChangeSocksConfig():Continuation(new_ProxyMutex()) {
    SET_HANDLER(&ChangeSocksConfig::change_config_handler);
    file_changed = 0;
  }
};
#endif

#endif //_SOCKS_H_
