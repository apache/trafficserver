/** @file

  This file implements the functionality to test the Proxy

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
#include <limits.h>
#include "Net.h"
#include "Disk.h"
#include "Main.h"
#include "HostDB.h"
#include "Cluster.h"
#include "OneWayTunnel.h"
#include "OneWayMultiTunnel.h"
#include "Cache.h"

struct TestProxy : Continuation {
  VConnection *vc;
  VConnection *vconnection_vector[2];
  VConnection *remote;
  MIOBuffer *inbuf;
  MIOBuffer *outbuf;
  VIO *clusterOutVIO;
  VIO *inVIO;
  char host[1024], *url, *url_end, amode;
  int port;
  char s[1024];
  ClusterVCToken token;
  OneWayTunnel *tunnel;
  char url_str[1024];
  VConnection *cachefile;
  URL *url_struct;
  HostDBInfo *hostdbinfo;
  CacheObjInfo *objinfo;
  HttpHeader *request_header;

  int
  done()
  {
    ink_assert(inbuf);
    if (inbuf)
      free_MIOBuffer(inbuf);
    inbuf = 0;
    if (outbuf)
      free_MIOBuffer(outbuf);
    if (vc)
      vc->do_io(VIO::CLOSE);
    if (remote)
      remote->do_io(VIO::CLOSE);
    if (cachefile)
      cachefile->do_io(VIO::CLOSE);
    if (tunnel)
      delete tunnel;
    delete this;
    return EVENT_DONE;
  }

  int
  gets(VIO *vio)
  {
    char *sx = s, *x;
    int t, i;
    for (x = vio->buffer.mbuf->start; *x && x < vio->buffer.mbuf->end; x++) {
      if (x - vio->buffer.mbuf->start > 1023)
        return -1;
      if (*x == '\n')
        break;
      *sx++ = *x;
    }

    t = 2;
    for (i = 0; t && s[i]; i++) {
      if (s[i] == ' ')
        --t;
    }

    //    i = strrchr(s,' ');

    if (s[i - 2] == 'X') {
      i -= 2;
      amode = 'x';
      while (s[i] != '\0') {
        s[i] = s[i + 1];
        ++i;
      }
      return x - vio->buffer.mbuf->start - 1;
    }
    return x - vio->buffer.mbuf->start;
  }

  int
  startEvent(int event, VIO *vio)
  {
    char *temp;
    if (event != VC_EVENT_READ_READY) {
      printf("TestProxy startEvent error %d %X\n", event, (unsigned int)vio->vc_server);
      return done();
    }
    inVIO       = vio;
    vc          = (NetVConnection *)vio->vc_server;
    int res     = 0;
    char *thost = NULL;
    if ((res = gets(vio))) {
      if (res < 0) {
        printf("TestProxy startEvent line too long\n");
        return done();
      }
      // for (int i = 0; i <= res; i++) fprintf(stderr,"[%c (%d)]\n",s[i],s[i]);
      s[res] = 0;
      if ((res > 0) && (s[res - 1] == '\r'))
        s[res - 1] = 0;
      // printf("got [%s]\n",s);
      if (s[4] == '/') {
        url      = s + 5;
        url_end  = strchr(url, ' ');
        *url_end = 0;
        SET_HANDLER(fileEvent);
        diskProcessor.open_vc(this, url, O_RDONLY);
        return EVENT_DONE;
      } else
        thost = s + 11;             // GET http
      url     = strchr(thost, '/'); // done before portStr stompage */
      temp    = strchr(thost, ' ');
      ink_assert(temp - thost < 1024);
      ink_strlcpy(url_str, thost, sizeof(url_str));
      if (!url)
        return done();
      char *portStr = strchr(thost, ':');
      *url          = 0;
      if (portStr == NULL) {
        port = 80;
        ink_strlcpy(host, thost, sizeof(host));
      } else {
        *portStr = '\0'; /* close off the hostname */
        port     = atoi(portStr + 1);
        ink_strlcpy(host, thost, sizeof(host));
        *portStr = ':';
      }
      url_end = strchr(url + 1, ' ');
      SET_HANDLER(dnsEvent);
      *url = '/';
      hostDBProcessor.getbyname(this, host);
      return EVENT_DONE;
    }
    return EVENT_CONT;
  }

  int
  clusterOpenEvent(int event, void *data)
  {
    if (event == CLUSTER_EVENT_OPEN_FAILED)
      return done();
    if (event == CLUSTER_EVENT_OPEN) {
      if (!data)
        return done();
      remote        = (VConnection *)data;
      clusterOutVIO = remote->do_io(VIO::WRITE, this, INT64_MAX, inbuf);
      ink_assert(clusterOutVIO);
      SET_HANDLER(tunnelEvent);
      tunnel = new OneWayTunnel(remote, vc, this, TUNNEL_TILL_DONE, true, true, true);
    }
    return EVENT_CONT;
  }

  int
  clusterEvent(int event, VConnection *data)
  {
    (void)event;
    vc = data;
    if (!vc)
      return done();
    SET_HANDLER(startEvent);
    vc->do_io(VIO::READ, this, INT64_MAX, inbuf);
    return EVENT_CONT;
  }

  int
  fileEvent(int event, DiskVConnection *aremote)
  {
    if (event != DISK_EVENT_OPEN) {
      printf("TestProxy fileEvent error %d\n", event);
      return done();
    }
    remote = aremote;
    SET_HANDLER(tunnelEvent);
    tunnel = new OneWayTunnel(remote, vc, this, TUNNEL_TILL_DONE, true, true, true);
    return EVENT_CONT;
  }

  int
  dnsEvent(int event, HostDBInfo *info)
  {
    if (!info) {
      printf("TestProxy dnsEvent error %d\n", event);
      return done();
    }
    SET_HANDLER(cacheCheckEvent);
    url_struct = new URL((const char *)url_str, sizeof(url_str), true);
    hostdbinfo = info;
    cacheProcessor.lookup(this, url_struct, false);
    // SET_HANDLER(connectEvent);
    // netProcessor.connect(this,info->ip,port,host);
    return EVENT_DONE;
  }

  int
  cacheCheckEvent(int event, void *data)
  {
    if (event == CACHE_EVENT_LOOKUP) {
      if (amode == 'x') {
        cout << "Removing object from the cache\n";
        SET_HANDLER(NULL);
        amode = 0;
        cacheProcessor.remove(&(((CacheObjInfoVector *)data)->data[0]), false);
        return done();
      } else {
        cout << "Serving the object from cache\n";
        SET_HANDLER(cacheReadEvent);
        cacheProcessor.open_read(this, &(((CacheObjInfoVector *)data)->data[0]), false);
        return EVENT_CONT;
      }
    } else if (event == CACHE_EVENT_LOOKUP_FAILED) {
      cout << "Getting the object from origin server\n";
      SET_HANDLER(cacheCreateCacheFileEvent);
      objinfo               = new CacheObjInfo;
      request_header        = new HttpHeader;
      request_header->m_url = *url_struct;
      objinfo->request      = *request_header;
      cacheProcessor.open_write(this, objinfo, false, CACHE_UNKNOWN_SIZE);
      return EVENT_DONE;
    } else {
      printf("TestProxy cacheCheckEvent error %d\n", event);
      return done();
    }
  }

  int
  cacheReadEvent(int event, DiskVConnection *aremote)
  {
    if (event != CACHE_EVENT_OPEN_READ) {
      printf("TestProxy cacheReadEvent error %d\n", event);
      return done();
    }
    remote = aremote;
    SET_HANDLER(tunnelEvent);
    new OneWayTunnel(remote, vc, this, TUNNEL_TILL_DONE, true, true, true);
    return EVENT_CONT;
  }
  int
  cacheCreateCacheFileEvent(int event, VConnection *acachefile)
  {
    if (event != CACHE_EVENT_OPEN_WRITE) {
      printf("TestProxy cacheCreateCacheFileEvent error %d\n", event);
      cachefile = 0;
    } else
      cachefile = acachefile;
    SET_HANDLER(cacheSendGetEvent);
    netProcessor.connect(this, hostdbinfo->ip, port, host);
    return EVENT_CONT;
  }
  int
  cacheSendGetEvent(int event, NetVConnection *aremote)
  {
    if (event != NET_EVENT_OPEN) {
      printf("TestProxy cacheSendGetEvent error %d\n", event);
      return done();
    }
    remote = aremote;
    outbuf = new_MIOBuffer();
    SET_HANDLER(cacheTransRemoteToCacheFileEvent);
    // aremote->set_inactivity_timeout(HRTIME_MSECONDS(2000));
    // aremote->set_active_timeout(HRTIME_MSECONDS(60000));
    *url_end = 0;
    sprintf(outbuf->start, "GET %s HTTP/1.0\nHost: %s\n\n", url, host);
    outbuf->fill(strlen(outbuf->start) + 1);
    remote->do_io(VIO::WRITE, this, INT64_MAX, outbuf);
    // printf("sending [%s]\n",outbuf->start);
    return EVENT_CONT;
  }
  int
  cacheTransRemoteToCacheFileEvent(int event, VIO *vio)
  {
    if (event != VC_EVENT_WRITE_READY) {
      printf("TestProxy cacheTransRemoteToCacheFileEvent error %d\n", event);
      return done();
    }
    if (vio->buffer.size())
      return EVENT_CONT;
    SET_HANDLER(tunnelEvent);
    vconnection_vector[0] = vc;
    vconnection_vector[1] = cachefile;
    {
      int n     = cachefile ? 2 : 1;
      cachefile = 0;
      new OneWayMultiTunnel(remote, vconnection_vector, n, this, TUNNEL_TILL_DONE, true, true, true);
    }
    return EVENT_DONE;
  }

  int
  connectEvent(int event, NetVConnection *aremote)
  {
    if (event != NET_EVENT_OPEN) {
      printf("TestProxy connectEvent error %d\n", event);
      return done();
    }
    remote = aremote;
    outbuf = new_MIOBuffer();
    SET_HANDLER(sendEvent);
    *url_end = 0;
    sprintf(outbuf->start, "GET %s HTTP/1.0\nHost: %s\n\n", url, host);
    outbuf->fill(strlen(outbuf->start) + 1);
    remote->do_io(VIO::WRITE, this, INT64_MAX, outbuf);
    // printf("sending [%s]\n",outbuf->start);
    return EVENT_CONT;
  }

  int
  sendEvent(int event, VIO *vio)
  {
    if (event != VC_EVENT_WRITE_READY) {
      printf("TestProxy sendEvent error %d\n", event);
      return done();
    }
    if (vio->buffer.size())
      return EVENT_CONT;
    SET_HANDLER(tunnelEvent);
    clusterOutVIO = (VIO *)-1; // some impossible value
    if (((NetVConnectionBase *)vc)->closed) {
      printf("TestProxy sendEvent unexpected close %X\n", (unsigned int)vc);
      vc = 0;
      return done();
    }
    tunnel = new OneWayTunnel(remote, vc, this, TUNNEL_TILL_DONE, true, true, true);
    return EVENT_DONE;
  }

  int
  tunnelEvent(int event, Continuation *cont)
  {
    (void)cont;
    if ((VIO *)cont == clusterOutVIO || (VIO *)cont == inVIO) {
      if (event == VC_EVENT_WRITE_COMPLETE)
        return EVENT_DONE;
      if (event == VC_EVENT_ERROR || event == VC_EVENT_EOS)
        return EVENT_DONE;
      return EVENT_CONT;
    }
    remote = 0;
    vc     = 0;
    if (event != VC_EVENT_EOS) {
      printf("TestProxy sendEvent error %d\n", event);
      return done();
    }
    // printf("successful proxy of %s\n",url);
    return done();
  }

  TestProxy(MIOBuffer *abuf)
    : Continuation(new_ProxyMutex()),
      vc(0),
      remote(0),
      inbuf(abuf),
      outbuf(0),
      clusterOutVIO(0),
      inVIO(0),
      url(0),
      url_end(0),
      amode(0),
      tunnel(0),
      cachefile(0)
  {
    SET_HANDLER(startEvent);
  }
};

struct TestAccept : Continuation {
  int
  startEvent(int event, NetVConnection *e)
  {
    if (event == NET_EVENT_ACCEPT) {
      MIOBuffer *buf = new_MIOBuffer();
      e->do_io(VIO::READ, new TestProxy(buf), INT64_MAX, buf);
    } else {
      printf("TestAccept error %d\n", event);
      return EVENT_DONE;
    }
    return EVENT_CONT;
  }
  TestAccept() : Continuation(new_ProxyMutex()) { SET_HANDLER(startEvent); }
};

void
redirect_test(Machine *m, void *data, int len)
{
  (void)m;
  (void)len;
  MIOBuffer *buf = new_MIOBuffer();
  TestProxy *c   = new TestProxy(buf);
  SET_CONTINUATION_HANDLER(c, clusterEvent);
  clusterProcessor.connect(c, *(ClusterVCToken *)data);
}

#ifndef SUB_TEST
void
test()
{
  ptest_ClusterFunction = redirect_test;
  netProcessor.proxy_accept(new TestAccept);
}
#endif
