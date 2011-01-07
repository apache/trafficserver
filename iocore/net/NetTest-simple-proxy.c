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



char *origin_server = "npdev.inktomi.com";
unsigned int origin_server_ip;
unsigned short origin_server_port = 8080;

struct NetTesterSM:public Continuation
{
  VIO *client_read_vio;
  VIO *client_resp_write_vio;
  VIO *server_resp_read_vio;
  int server_vc_closed, client_vc_closed;

  IOBufferReader *client_reader, *client_parse_reader;

  NetVConnection *client_vc, *server_vc;
  MIOBuffer *request_buf;
  MIOBuffer *response_buf;
  char request[2000];
  int req_len;


    NetTesterSM(ProxyMutex * _mutex, NetVConnection * _vc):Continuation(_mutex),
    server_vc_closed(0), client_vc_closed(0)
  {
    MUTEX_TRY_LOCK(lock, mutex, _vc->thread);
    ink_release_assert(lock);
    client_vc = _vc;
    server_vc = NULL;
    SET_HANDLER(&NetTesterSM::handle_request_read_from_client);
    // jtest headers are really short
    request_buf = new_MIOBuffer(1);
    response_buf = new_MIOBuffer(8);
    client_reader = request_buf->alloc_reader();
    client_parse_reader = request_buf->alloc_reader();
    client_read_vio = client_vc->do_io_read(this, INT_MAX, request_buf);
    //client_vc->set_inactivity_timeout(HRTIME_SECONDS(60));
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
    //close_server_vc();
    //close_client_vc();
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
      Debug("net_test", "%s\n", request);
      fflush(stdout);
      //client_vc->set_inactivity_timeout(HRTIME_SECONDS(30));
      if (strcmp(&request[req_len - 4], "\r\n\r\n") == 0) {
        Debug("net_test", "The request header is :\n%s\n", request);
        client_vc->cancel_inactivity_timeout();
        // connect to the origin server
        SET_HANDLER(&NetTesterSM::handle_server_connect);
        sslNetProcessor.connect_re(this, origin_server_ip, origin_server_port);
      }
      break;
    case VC_EVENT_READ_COMPLETE:
      /* FALLSTHROUGH */
    case VC_EVENT_EOS:
      r = client_parse_reader->read_avail();
      str = NEW(new char[r + 10]);
      client_parse_reader->read(str, r);
      /* FALLSTHROUGH */
    case VC_EVENT_ERROR:
    case VC_EVENT_INACTIVITY_TIMEOUT:
      close_client_vc();
      // fixme
      // handle timeout events
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
      Debug("net_test", "connected to server\n");
      Debug("net_test", "writing %d to server\n", client_reader->read_avail());
      server_vc->do_io_write(this, client_reader->read_avail(), client_reader);
      //vc->set_inactivity_timeout(HRTIME_SECONDS(10));
      break;
    case NET_EVENT_OPEN_FAILED:
    default:
      close_client_vc();
      delete this;
    }
    return EVENT_CONT;
  }

  int handle_write_request_to_server(int event, Event * e)
  {
    IOBufferReader *resp_reader;
    switch (event) {
    case VC_EVENT_WRITE_READY:
      Debug("net_test", "wrote some bytes to server\n");
      break;

    case VC_EVENT_WRITE_COMPLETE:
      Debug("net_test", "wrote request to server\n");
      SET_HANDLER(&NetTesterSM::handle_response_pump);
      resp_reader = response_buf->alloc_reader();

      response_buf->autopilot = 1;
      server_resp_read_vio = server_vc->do_io_read(this, INT64_MAX, response_buf);
      client_resp_write_vio = client_vc->do_io_write(this, INT64_MAX, resp_reader);
      response_buf->assign_reader_vio(client_resp_write_vio, resp_reader);
      response_buf->assign_writer_vio(server_resp_read_vio);
      break;
    case VC_EVENT_EOS:
    case VC_EVENT_ERROR:
    case VC_EVENT_INACTIVITY_TIMEOUT:
      close_server_vc();
      close_client_vc();
      delete this;
      return EVENT_DONE;
      break;
    default:
      ink_release_assert(!"unknown event");
    }
    return EVENT_CONT;
  }

  void close_server_vc()
  {
    if (!server_vc_closed)
      server_vc->do_io_close();
    server_vc = NULL;
    server_vc_closed = 1;
  }

  void close_client_vc()
  {
    if (!client_vc_closed)
      client_vc->do_io_close();
    client_vc = NULL;
    client_vc_closed = 1;
  }

  int handle_response_pump(int event, Event * e)
  {
    int doc_len;
    switch (event) {
    case VC_EVENT_ERROR:
    case VC_EVENT_INACTIVITY_TIMEOUT:
      close_server_vc();
      close_client_vc();
      delete this;
      return EVENT_DONE;
      break;


    case VC_EVENT_WRITE_READY:
    case VC_EVENT_READ_READY:
      ink_release_assert(!"unexpected READY event in handle_response_pump");
      break;

    case VC_EVENT_READ_COMPLETE:
    case VC_EVENT_EOS:
      doc_len = server_resp_read_vio->ndone;
      Debug("net_test", "Got response %d bytes from server\n", doc_len);
      close_server_vc();
      if (client_resp_write_vio->ndone != doc_len) {
        client_resp_write_vio->set_nbytes(doc_len);
        client_vc->reenable(client_resp_write_vio);
      } else {
        Debug("net_test", "Wrote response %d bytes to client\n", client_resp_write_vio->ndone);
        close_client_vc();
        delete this;
        return EVENT_DONE;
      }
      break;
    case VC_EVENT_WRITE_COMPLETE:
      Debug("net_test", "Wrote response %d bytes to client\n", client_resp_write_vio->ndone);
      close_client_vc();
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
    Debug("net_test", "Accepted a connection\n");
    NetVConnection *vc = (NetVConnection *) data;
    NEW(new NetTesterSM(new_ProxyMutex(), vc));
    return EVENT_CONT;
  }


};

//#define TEST_ACCEPT_CANCEL

struct Stop:public Continuation
{
  Action *a;
    Stop(ProxyMutex * m):Continuation(m)
  {
    SET_HANDLER(&Stop::stop);
  }

  int stop(int event, Event * e)
  {
    printf("Cancelling accept\n");
    a->cancel();
    return EVENT_DONE;
  }
};


int
test_main()
{
  Action *a;
  origin_server_ip = get_addr(origin_server);

  a = sslNetProcessor.accept(NEW(new NetTesterAccept(new_ProxyMutex())), 45080, true);

#ifdef TEST_ACCEPT_CANCEL
  Stop *s = NEW(new Stop(new_ProxyMutex()));
  eventProcessor.schedule_in(s, HRTIME_SECONDS(10));
  s->a = a;
#endif

  return 0;
}
