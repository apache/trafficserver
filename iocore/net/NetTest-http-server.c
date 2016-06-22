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

IOBufferBlock *resp_blk;
int doc_len;

struct NetTesterSM : public Continuation {
  VIO *read_vio;
  IOBufferReader *reader, *resp_reader;
  NetVConnection *vc;
  MIOBuffer *req_buf, *resp_buf;
  char request[2000];
  int req_len;

  NetTesterSM(ProxyMutex *_mutex, NetVConnection *_vc) : Continuation(_mutex)
  {
    MUTEX_TRY_LOCK(lock, mutex, _vc->thread);
    ink_release_assert(lock);
    vc = _vc;
    Debug("net_test", "Accepted a connection");
    SET_HANDLER(&NetTesterSM::handle_read);
    req_buf  = new_MIOBuffer(1);
    reader   = req_buf->alloc_reader();
    read_vio = vc->do_io_read(this, INT64_MAX, req_buf);
    // vc->set_inactivity_timeout(HRTIME_SECONDS(60));
    resp_buf = new_empty_MIOBuffer(6);
    resp_buf->append_block(resp_blk->clone());
    req_len     = 0;
    resp_reader = resp_buf->alloc_reader();
  }

  ~NetTesterSM()
  {
    req_buf->dealloc_all_readers();
    req_buf->clear();
    free_MIOBuffer(req_buf);

    resp_buf->dealloc_all_readers();
    resp_buf->clear();
    free_MIOBuffer(resp_buf);
  }

  /* ********************* jtest sample request   **********************
     GET http://npdev:8080/0.5216393021/6000 HTTP/1.0
     Proxy-Connection: Keep-Alive
   */

  int
  handle_read(int event, void *data)
  {
    int r;
    char *str;
    switch (event) {
    case VC_EVENT_READ_READY:
      r = reader->read_avail();
      reader->read(&request[req_len], r);
      req_len += r;
      request[req_len] = 0;
      Debug("net_test", "%s\n", request);
      fflush(stdout);
      // vc->set_inactivity_timeout(HRTIME_SECONDS(30));
      if (strcmp(&request[req_len - 4], "\r\n\r\n") == 0) {
        Debug("net_test", "The request header is :\n%s\n", request);
        // parse and get the doc size
        SET_HANDLER(&NetTesterSM::handle_write);
        ink_assert(doc_len == resp_reader->read_avail());
        vc->do_io_write(this, doc_len, resp_reader);
        // vc->set_inactivity_timeout(HRTIME_SECONDS(10));
      }
      break;
    case VC_EVENT_READ_COMPLETE:
    /* FALLSTHROUGH */
    case VC_EVENT_EOS:
      r   = reader->read_avail();
      str = new char[r + 10];
      reader->read(str, r);
      Debug("net_test", "%s", str);
      fflush(stdout);
    case VC_EVENT_ERROR:
    case VC_EVENT_INACTIVITY_TIMEOUT:
      vc->do_io_close();
      // fixme
      // handle timeout events
      break;
    default:
      ink_release_assert(!"unknown event");
    }
    return EVENT_CONT;
  }

  int
  handle_write(int event, Event *e)
  {
    switch (event) {
    case VC_EVENT_WRITE_READY:
      break;

    case VC_EVENT_WRITE_COMPLETE:
    case VC_EVENT_EOS:
    case VC_EVENT_ERROR:
    case VC_EVENT_INACTIVITY_TIMEOUT:
      vc->do_io_close();
      delete this;
      return EVENT_DONE;
      break;
    default:
      ink_release_assert(!"unknown event");
    }
    return EVENT_CONT;
  }
};

struct NetTesterAccept : public Continuation {
  NetTesterAccept(ProxyMutex *_mutex) : Continuation(_mutex) { SET_HANDLER(&NetTesterAccept::handle_accept); }
  int
  handle_accept(int event, void *data)
  {
    Debug("net_test", "Accepted a connection\n");
    fflush(stdout);
    NetVConnection *vc = (NetVConnection *)data;
    new NetTesterSM(new_ProxyMutex(), vc);
    return EVENT_CONT;
  }
};

struct Stop : public Continuation {
  Action *a;
  Stop(ProxyMutex *m) : Continuation(m) { SET_HANDLER(&Stop::stop); }
  int
  stop(int event, Event *e)
  {
    a->cancel();
    return EVENT_DONE;
  }
};

int
test_main()
{
  const char *response_hdr = "HTTP/1.0 200 OK\n"
                             "Content-Type: text/html\n"
                             "Content-Length: 8000\r\n\r\n";

  resp_blk = new_IOBufferBlock();
  resp_blk->alloc(6);
  char *b = resp_blk->start();
  ink_strlcpy(b, response_hdr, resp_blk->block_size());
  memset(b + strlen(response_hdr), 'x', 8000);
  resp_blk->fill(doc_len = strlen(response_hdr) + 8000);

  Action *a = sslNetProcessor.accept(new NetTesterAccept(new_ProxyMutex()), 8080, true);

  return 0;
}
