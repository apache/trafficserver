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

#include "tscore/ink_platform.h"
#include "P_Net.h"
#include "Show.h"
#include "I_Tasks.h"

struct ShowNet;
using ShowNetEventHandler = int (ShowNet::*)(int, Event *);
struct ShowNet : public ShowCont {
  int ithread;
  IpEndpoint addr;

  int
  showMain(int event, Event *e)
  {
    CHECK_SHOW(begin("Net"));
    CHECK_SHOW(show("<H3>Show <A HREF=\"./connections\">Connections</A></H3>\n"
                    //"<H3>Show <A HREF=\"./threads\">Net Threads</A></H3>\n"
                    "<form method = GET action = \"./ips\">\n"
                    "Show Connections to/from IP (e.g. 127.0.0.1):<br>\n"
                    "<input type=text name=ip size=64 maxlength=256>\n"
                    "</form>\n"
                    "<form method = GET action = \"./ports\">\n"
                    "Show Connections to/from Port (e.g. 80):<br>\n"
                    "<input type=text name=name size=64 maxlength=256>\n"
                    "</form>\n"));
    return complete(event, e);
  }

  int
  showConnectionsOnThread(int event, Event *e)
  {
    EThread *ethread = e->ethread;
    NetHandler *nh   = get_NetHandler(ethread);
    MUTEX_TRY_LOCK(lock, nh->mutex, ethread);
    if (!lock.is_locked()) {
      ethread->schedule_in(this, HRTIME_MSECONDS(net_retry_delay));
      return EVENT_DONE;
    }

    ink_hrtime now = Thread::get_hrtime();
    forl_LL(UnixNetVConnection, vc, nh->open_list)
    {
      //      uint16_t port = ats_ip_port_host_order(&addr.sa);
      if (ats_is_ip(&addr) && !ats_ip_addr_port_eq(&addr.sa, vc->get_remote_addr())) {
        continue;
      }
      //      if (port && port != ats_ip_port_host_order(&vc->server_addr.sa) && port != vc->accept_port)
      //        continue;
      char ipbuf[INET6_ADDRSTRLEN];
      ats_ip_ntop(vc->get_remote_addr(), ipbuf, sizeof(ipbuf));
      char opt_ipbuf[INET6_ADDRSTRLEN];
      char interbuf[80];
      snprintf(interbuf, sizeof(interbuf), "[%s] %s:%d", vc->options.toString(vc->options.addr_binding),
               vc->options.local_ip.toString(opt_ipbuf, sizeof(opt_ipbuf)), vc->options.local_port);
      CHECK_SHOW(show("<tr>"
                      //"<td><a href=\"/connection/%d\">%d</a></td>"
                      "<td>%d</td>"          // ID
                      "<td>%s</td>"          // ipbuf
                      "<td>%d</td>"          // port
                      "<td>%d</td>"          // fd
                      "<td>%s</td>"          // interbuf
                                             //                      "<td>%d</td>"     // accept port
                      "<td>%d secs ago</td>" // start time
                      "<td>%d</td>"          // thread id
                      "<td>%d</td>"          // read enabled
                      "<td>%" PRId64 "</td>" // read NBytes
                      "<td>%" PRId64 "</td>" // read NDone
                      "<td>%d</td>"          // write enabled
                      "<td>%" PRId64 "</td>" // write nbytes
                      "<td>%" PRId64 "</td>" // write ndone
                      "<td>%d secs</td>"     // Inactivity timeout at
                      "<td>%d secs</td>"     // Activity timeout at
                      "<td>%d</td>"          // shutdown
                      "<td>-%s</td>"         // comments
                      "</tr>\n",
                      vc->id, ipbuf, ats_ip_port_host_order(vc->get_remote_addr()), vc->con.fd, interbuf,
                      //                      vc->accept_port,
                      (int)((now - vc->submit_time) / HRTIME_SECOND), ethread->id, vc->read.enabled, vc->read.vio.nbytes,
                      vc->read.vio.ndone, vc->write.enabled, vc->write.vio.nbytes, vc->write.vio.ndone,
                      (int)(vc->inactivity_timeout_in / HRTIME_SECOND), (int)(vc->active_timeout_in / HRTIME_SECOND),
                      vc->f.shutdown, vc->closed ? "closed " : ""));
    }
    ithread++;
    if (ithread < eventProcessor.thread_group[ET_NET]._count) {
      eventProcessor.thread_group[ET_NET]._thread[ithread]->schedule_imm(this);
    } else {
      CHECK_SHOW(show("</table>\n"));
      return complete(event, e);
    }
    return EVENT_CONT;
  }

  int
  showConnections(int event, Event *e)
  {
    CHECK_SHOW(begin("Net Connections"));
    CHECK_SHOW(show("<H3>Connections</H3>\n"
                    "<table border=1><tr>"
                    "<th>ID</th>"
                    "<th>IP</th>"
                    "<th>Port</th>"
                    "<th>FD</th>"
                    "<th>Interface</th>"
                    "<th>Accept Port</th>"
                    "<th>Time Started</th>"
                    "<th>Thread</th>"
                    "<th>Read Enabled</th>"
                    "<th>Read NBytes</th>"
                    "<th>Read NDone</th>"
                    "<th>Write Enabled</th>"
                    "<th>Write NBytes</th>"
                    "<th>Write NDone</th>"
                    "<th>Inactive Timeout</th>"
                    "<th>Active   Timeout</th>"
                    "<th>Shutdown</th>"
                    "<th>Comments</th>"
                    "</tr>\n"));
    SET_HANDLER(&ShowNet::showConnectionsOnThread);
    eventProcessor.thread_group[ET_NET]._thread[0]->schedule_imm(this); // This can not use ET_TASK.
    return EVENT_CONT;
  }

  int
  showSingleThread(int event, Event *e)
  {
    EThread *ethread               = e->ethread;
    NetHandler *nh                 = get_NetHandler(ethread);
    PollDescriptor *pollDescriptor = get_PollDescriptor(ethread);
    MUTEX_TRY_LOCK(lock, nh->mutex, ethread);
    if (!lock.is_locked()) {
      ethread->schedule_in(this, HRTIME_MSECONDS(net_retry_delay));
      return EVENT_DONE;
    }

    CHECK_SHOW(show("<H3>Thread: %d</H3>\n", ithread));
    CHECK_SHOW(show("<table border=1>\n"));
    int connections = 0;
    forl_LL(UnixNetVConnection, vc, nh->open_list) connections++;
    CHECK_SHOW(show("<tr><td>%s</td><td>%d</td></tr>\n", "Connections", connections));
    // CHECK_SHOW(show("<tr><td>%s</td><td>%d</td></tr>\n", "Last Poll Size", pollDescriptor->nfds));
    CHECK_SHOW(show("<tr><td>%s</td><td>%d</td></tr>\n", "Last Poll Ready", pollDescriptor->result));
    CHECK_SHOW(show("</table>\n"));
    CHECK_SHOW(show("<table border=1>\n"));
    CHECK_SHOW(show("<tr><th>#</th><th>Read Priority</th><th>Read Bucket</th><th>Write Priority</th><th>Write Bucket</th></tr>\n"));
    CHECK_SHOW(show("</table>\n"));
    ithread++;
    if (ithread < eventProcessor.thread_group[ET_NET]._count) {
      eventProcessor.thread_group[ET_NET]._thread[ithread]->schedule_imm(this);
    } else {
      return complete(event, e);
    }
    return EVENT_CONT;
  }

  int
  showThreads(int event, Event *e)
  {
    CHECK_SHOW(begin("Net Threads"));
    SET_HANDLER(&ShowNet::showSingleThread);
    eventProcessor.thread_group[ET_NET]._thread[0]->schedule_imm(this); // This can not use ET_TASK
    return EVENT_CONT;
  }
  int
  showSingleConnection(int event, Event *e)
  {
    CHECK_SHOW(begin("Net Connection"));
    return complete(event, e);
  }
  int
  showHostnames(int event, Event *e)
  {
    CHECK_SHOW(begin("Net Connections to/from Host"));
    return complete(event, e);
  }

  ShowNet(Continuation *c, HTTPHdr *h) : ShowCont(c, h), ithread(0)
  {
    memset(&addr, 0, sizeof(addr));
    SET_HANDLER(&ShowNet::showMain);
  }
};

#undef STREQ_PREFIX
#define STREQ_PREFIX(_x, _n, _s) (!ptr_len_ncasecmp(_x, _n, _s, sizeof(_s) - 1))
Action *
register_ShowNet(Continuation *c, HTTPHdr *h)
{
  ShowNet *s = new ShowNet(c, h);
  int path_len;
  const char *path = h->url_get()->path_get(&path_len);

  SET_CONTINUATION_HANDLER(s, &ShowNet::showMain);
  if (STREQ_PREFIX(path, path_len, "connections")) {
    SET_CONTINUATION_HANDLER(s, &ShowNet::showConnections);
  } else if (STREQ_PREFIX(path, path_len, "threads")) {
    SET_CONTINUATION_HANDLER(s, &ShowNet::showThreads);
  } else if (STREQ_PREFIX(path, path_len, "ips")) {
    int query_len;
    const char *query = h->url_get()->query_get(&query_len);
    s->sarg           = ats_strndup(query, query_len);
    char *gn          = nullptr;
    if (s->sarg) {
      gn = static_cast<char *>(memchr(s->sarg, '=', strlen(s->sarg)));
    }
    if (gn) {
      ats_ip_pton(gn + 1, &s->addr);
    }
    SET_CONTINUATION_HANDLER(s, &ShowNet::showConnections);
  } else if (STREQ_PREFIX(path, path_len, "ports")) {
    int query_len;
    const char *query = h->url_get()->query_get(&query_len);
    s->sarg           = ats_strndup(query, query_len);
    char *gn          = nullptr;
    if (s->sarg) {
      gn = static_cast<char *>(memchr(s->sarg, '=', strlen(s->sarg)));
    }
    if (gn) {
      ats_ip_port_cast(&s->addr.sa) = htons(atoi(gn + 1));
    }
    SET_CONTINUATION_HANDLER(s, &ShowNet::showConnections);
  }
  eventProcessor.schedule_imm(s, ET_TASK);
  return &s->action;
}
