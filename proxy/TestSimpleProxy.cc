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

#include <limits.h>
#include "Net.h"
#include "Disk.h"
#include "Main.h"
#include "DNS.h"
#include "OneWayTunnel.h"

struct TestProxy : Continuation {
  NetVConnection *vc;
  NetVConnection *remote;
  MIOBuffer *inbuf;
  MIOBuffer *outbuf;
  char *host, *url, *url_end;
  char s[256];

  int
  done()
  {
    if (inbuf)
      free_MIOBuffer(inbuf);
    if (outbuf)
      free_MIOBuffer(outbuf);
    if (vc)
      vc->do_io(VIO::CLOSE);
    if (remote)
      remote->do_io(VIO::CLOSE);
    delete this;
    return EVENT_DONE;
  }

  int
  startEvent(int event, VIO *vio)
  {
    if (event != VC_EVENT_READ_READY) {
      printf("TestProxy startEvent error %d\n", event);
      return done();
    }
    if (vio->buffer.mbuf->gets(s, 255)) {
      host    = s + 11;
      url     = strchr(host, '/');
      url_end = strchr(url, ' ');
      *url    = 0;
      dnsProcessor.gethostbyname(host, this);
      *url = '/';
      SET_HANDLER(dnsEvent);
      vc = (NetVConnection *)vio->vc_server;
      return EVENT_DONE;
    }
    return EVENT_CONT;
  }

  int
  dnsEvent(int event, HostEnt *ent)
  {
    if (!ent) {
      printf("TestProxy dnsEvent error %d\n", event);
      return done();
    }
    unsigned int ip = *(unsigned int *)ent->h_addr_list[0];
    netProcessor.connect(this, ip, 80);
    SET_HANDLER(connectEvent);
    return EVENT_DONE;
  }

  int
  connectEvent(int event, NetVConnection *aremote)
  {
    if (!aremote) {
      printf("TestProxy connectEvent error %d\n", event);
      return done();
    }
    remote = aremote;
    outbuf = new_MIOBuffer();
    remote->do_io(VIO::WRITE, this, INT64_MAX, outbuf);
    *url_end = 0;
    sprintf(outbuf->start, "GET %s HTTP/1.0\n\n\n", url);
    outbuf->fill(strlen(outbuf->start) + 1);
    printf("sending [%s]\n", outbuf->start);
    SET_HANDLER(sendEvent);
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
    new OneWayTunnel(remote, vc, this, TUNNEL_TILL_DONE, true, true, true);
    SET_HANDLER(tunnelEvent);
    return EVENT_DONE;
  }

  int
  tunnelEvent(int event, Continuation *cont)
  {
    (void)cont;
    if (event != VC_EVENT_EOS) {
      printf("TestProxy sendEvent error %d\n", event);
      return done();
    }
    remote = 0;
    vc     = 0;
    printf("sucessful proxy of %s\n", url);
    return done();
  }

  TestProxy(MIOBuffer *abuf) : Continuation(new_ProxyMutex()), vc(0), remote(0), inbuf(abuf), outbuf(0), host(0), url(0), url_end(0)
  {
    SET_HANDLER(startEvent);
  }
};

struct TestAccept : Continuation {
  int
  startEvent(int event, NetVConnection *e)
  {
    if (!event) {
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
test()
{
  netProcessor.accept(new TestAccept, accept_port_number);
}
