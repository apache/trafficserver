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

#include "I_Net.h"
#include <netdb.h>

static unsigned int
get_addr(char *host)
{
  unsigned int addr = inet_addr(host);
  struct hostent *host_info = NULL;

  if (!addr || (-1 == (int) addr)) {
    host_info = gethostbyname(host);
    if (!host_info) {
      perror("gethostbyname");
      return (unsigned int) -1;
    }
    addr = *((unsigned int *) host_info->h_addr);
  }

  return addr;
}


#define MAX_INT 32767

char *origin_server = "npdev.inktomi.com";
unsigned short origin_server_port = 8080;

struct NetTesterSM:public Continuation
{
  VIO *client_read_vio;
  VIO *client_resp_write_vio;
  VIO *server_resp_read_vio;

  IOBufferReader *reader;
  IOBufferReader *client_reader, *client_parse_reader;

  NetVConnection *client_vc, *server_vc;
  MIOBuffer *request_buf;
  MIOBuffer *response_buf;
  char request[2000];
  int req_len;


    NetTesterSM(ProxyMutex * _mutex, NetVConnection * _vc):Continuation(_mutex)
  {
    MUTEX_TRY_LOCK(lock, mutex, _vc->thread);
    ink_release_assert(lock);
    client_vc = _vc;
    SET_HANDLER(&NetTesterSM::handle_request_read_from_client);
    request_buf = new_MIOBuffer(8);
    response_buf = new_MIOBuffer(8);
    client_reader = request_buf->alloc_reader();
    client_parse_reader = request_buf->alloc_reader();
    client_read_vio = client_vc->do_io_read(this, INT_MAX, request_buf);
    client_vc->set_inactivity_timeout(HRTIME_SECONDS(60));
    req_len = 0;
  }


   ~NetTesterSM()
  {
    request_buf->dealloc_all_readers();
    request_buf->clear();
    free_MIOBuffer(request_buf);
    response_buf->dealloc_all_readers();
    response_buf->clear();
    free_MIOBuffer(response_buf);
  }

  /* ********************* jtest sample request   **********************
     GET http://npdev:8080/0.5216393021/6000 HTTP/1.0
     Proxy-Connection: Keep-Alive
   */

  int handle_request_read_from_client(int event, void *data)
  {
    int r;
    char *str;
    switch (event) {
    case VC_EVENT_READ_READY:
      r = client_parse_reader->read_avail();
      client_parse_reader->read(&request[req_len], r);
      req_len += r;
      request[req_len] = 0;
      //printf("%s\n", request);
      fflush(stdout);
      client_vc->set_inactivity_timeout(HRTIME_SECONDS(30));
      if (strcmp(&request[req_len - 4], "\r\n\r\n") == 0) {
        //printf("The request header is :\n%s\n", request);
        client_vc->cancel_inactivity_timeout();
        // connect to the origin server
        SET_HANDLER(&NetTesterSM::handle_server_connect);
        netProcessor.connect_re(this, get_addr(origin_server), origin_server_port);
      }
      break;
    case VC_EVENT_READ_COMPLETE:
      /* FALLSTHROUGH */
    case VC_EVENT_EOS:
      r = reader->read_avail();
      str = NEW(new char[r + 10]);
      reader->read(str, r);
      //printf("%s", str);
      fflush(stdout);
    case VC_EVENT_ERROR:
    case VC_EVENT_INACTIVITY_TIMEOUT:
      client_vc->do_io_close();
      break;
    default:
      ink_release_assert(!"unknown event");

    }
    return EVENT_CONT;
  }

  int handle_server_connect(int event, Event * e)
  {
    switch (event) {
    case NET_EVENT_OPEN:
      server_vc = (NetVConnection *) e;
      SET_HANDLER(&NetTesterSM::handle_write_request_to_server);
      //printf("connected to server\n");
      //printf("writing %d to server\n", client_reader->read_avail());
      server_vc->do_io_write(this, client_reader->read_avail(), client_reader);
      //vc->set_inactivity_timeout(HRTIME_SECONDS(10));
      break;
    case NET_EVENT_OPEN_FAILED:
    default:
      client_vc->do_io_close();
      delete this;
    }
    return EVENT_CONT;
  }

  int handle_write_request_to_server(int event, Event * e)
  {
    IOBufferReader *resp_reader;
    switch (event) {
    case VC_EVENT_WRITE_READY:
      //printf("wrote some bytes to server\n");
      break;

    case VC_EVENT_WRITE_COMPLETE:
      //printf("wrote request to server\n");
      SET_HANDLER(&NetTesterSM::handle_response_pump);
      resp_reader = response_buf->alloc_reader();

      response_buf->autopilot = 1;
      server_resp_read_vio = server_vc->do_io_read(this, MAX_INT, response_buf);
      client_resp_write_vio = client_vc->do_io_write(this, MAX_INT, resp_reader);
      response_buf->assign_reader_vio(client_resp_write_vio, resp_reader);
      response_buf->assign_writer_vio(server_resp_read_vio);
      break;
    case VC_EVENT_EOS:
    case VC_EVENT_ERROR:
    case VC_EVENT_INACTIVITY_TIMEOUT:
      server_vc->do_io_close();
      client_vc->do_io_close();
      delete this;
      return EVENT_DONE;
      break;
    default:
      ink_release_assert(!"unknown event");
    }
    return EVENT_CONT;
  }

  int handle_response_pump(int event, Event * e)
  {
    int doc_len;
    switch (event) {
    case VC_EVENT_ERROR:
    case VC_EVENT_INACTIVITY_TIMEOUT:
      server_vc->do_io_close();
      client_vc->do_io_close();
      delete this;
      return EVENT_DONE;
      break;


    case VC_EVENT_WRITE_READY:
    case VC_EVENT_READ_READY:
      ink_release_assert(!"unexpected READY event in handle_response_pump");
      break;

    case VC_EVENT_READ_COMPLETE:
    case VC_EVENT_EOS:
      //printf("Got response from server\n");
      doc_len = server_resp_read_vio->ndone;
      server_vc->do_io_close();
      if (client_resp_write_vio->ndone != doc_len) {
        client_resp_write_vio->set_nbytes(doc_len);
        client_vc->reenable(client_resp_write_vio);
      } else {
        client_vc->do_io_close();
        delete this;
        return EVENT_DONE;
      }
      break;
    case VC_EVENT_WRITE_COMPLETE:
      client_vc->do_io_close();
      delete this;
      return EVENT_DONE;

    default:
      ink_release_assert(!"unexpected event in handle_response_pump");
    }
    return EVENT_CONT;

  }


};


struct NetTesterAccept:public Continuation
{

  NetTesterAccept(ProxyMutex * _mutex):Continuation(_mutex)
  {
    SET_HANDLER(&NetTesterAccept::handle_accept);
  }

  int handle_accept(int event, void *data)
  {
    //printf("Accepted a connection\n");        
    NetVConnection *vc = (NetVConnection *) data;
    NEW(new NetTesterSM(new_ProxyMutex(), vc));
    return EVENT_CONT;
  }


};



struct Stop:public Continuation
{
  Action *a;
    Stop(ProxyMutex * m):Continuation(m)
  {
    SET_HANDLER(&Stop::stop);
  }

  int stop(int event, Event * e)
  {
    a->cancel();
    return EVENT_DONE;
  }
};


int
main()
{
  // do not buffer stdout
  setbuf(stdout, NULL);
  ink_event_system_init(EVENT_SYSTEM_MODULE_VERSION);
  init_buffer_allocators();
  eventProcessor.start(1, 0, 0, 0);
  netProcessor.start();

  Action *a = netProcessor.accept(NEW(new NetTesterAccept(new_ProxyMutex())),
                                  45080, false);

#ifdef TEST_ACCEPT_CANCEL
  Stop *s = NEW(new Stop(new_ProxyMutex()));
  s->a = a;
  eventProcessor.schedule_in(s, HRTIME_SECONDS(10));
#endif
  this_thread()->execute();
}
