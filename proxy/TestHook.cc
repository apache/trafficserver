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

/****************************************************************************

  TestHook.cc
 ****************************************************************************/

#include "ink_config.h"
#include "ink_unused.h"
#include <limits.h>
#include "P_Net.h"
#include "ParseRules.h"
#include "EventName.h"

volatile int state_machine_count = 0;

struct Globals
{
  int accept_port;
  int accept_count;
  int accept_spawn;
  int buffer_size;
  int default_body_size;

  void get_env_int(int *var, const char *env)
  {
    char *s = getenv(env);
    if (s)
       *var = atoi(s);
  }

  Globals()
  {
    accept_port = 38080;
    accept_count = 1;
    accept_spawn = true;
    buffer_size = default_large_iobuffer_size;
    default_body_size = 6000;

    get_env_int(&accept_port, "BRIOCORE_SERVER_ACCEPT_PORT");
    get_env_int(&accept_count, "BRIOCORE_SERVER_ACCEPT_COUNT");
    get_env_int(&accept_spawn, "BRIOCORE_SERVER_ACCEPT_SPAWN");
    get_env_int(&buffer_size, "BRIOCORE_SERVER_BUFFER_SIZE");
    get_env_int(&default_body_size, "BRIOCORE_SERVER_DEFAULT_BODY_SIZE");
  }
}

G;

class StateMachine;
class AcceptContinuation;

////////////////////////////////////////////////////////////////////////
//
//      StateMachine
//
//      This continuation keeps track of the state of an HTTP
//      request and response, and lives for life of the transaction.
//
////////////////////////////////////////////////////////////////////////

class StateMachine:public Continuation
{
private:
  char scratch_space[512];
  char url[512], *url_end, hdr[1024], *hdr_end;
  int header_size, url_size, body_size, total_size;
  int header_size_written, body_size_written;
  int parse_mode;

public:
    VIO * ivio, *ovio;
  VConnection *nvc;
  MIOBuffer *ibuf, *obuf;
  IOBufferReader *reader;

  int parse();
  int fill();
  int responseDataEvent(int event, void *data);
  void startResponse();
  void killStateMachine();
  int computeBodySize(char *url);
  int requestDataEvent(int event, void *data);

    StateMachine(NetVConnection * vc):Continuation(new_ProxyMutex()),
    header_size(0), url_size(0), body_size(0), total_size(0), header_size_written(0), body_size_written(0)
  {
    ink_atomic_increment((int *) &state_machine_count, 1);
    scratch_space[0] = url[0] = hdr[0] = 0;
    bzero(history, sizeof(history));
    nvc = vc;
    ivio = ovio = 0;
    ibuf = new_MIOBuffer(G.buffer_size);
    obuf = new_MIOBuffer(G.buffer_size);
    reader = ibuf->alloc_reader();
    parse_mode = 0;
    history_pos = 0;
    history_start_time = ink_get_based_hrtime();
    SET_HANDLER(&StateMachine::requestDataEvent);
  }

   ~StateMachine()
  {
    ink_atomic_increment((int *) &state_machine_count, -1);
    ibuf->dealloc_all_readers();
    free_MIOBuffer(ibuf);
    free_MIOBuffer(obuf);
  }

  enum
  { HISTORY_SIZE = 128 };
  struct HistoryItem
  {
    ink_hrtime time;
    int line;
    int event;
    int ndone;
  };
  HistoryItem history[HISTORY_SIZE];
  int history_pos;
  ink_hrtime history_start_time;
};

#define REMEMBER(_e, _ndone) { \
history[history_pos%HISTORY_SIZE].time  = ((ink_get_based_hrtime()-history_start_time) / HRTIME_MSECOND); \
history[history_pos%HISTORY_SIZE].line  = __LINE__; \
history[history_pos%HISTORY_SIZE].event = _e; \
history[history_pos%HISTORY_SIZE].ndone = _ndone; \
history_pos++; }
////////////////////////////////////////////////////////////////////////
//
//      StateMachine::requestDataEvent
//
//      Called back whenever there is request data available.
//      Incrementally parses out the request URL and headers,
//      then switches to a response mode to generate the output.
//
////////////////////////////////////////////////////////////////////////

int
StateMachine::requestDataEvent(int event, void *data)
{
  int done;

  REMEMBER(event, (data) ? ((VIO *) data)->ndone : -1);

  switch (event) {
  case VC_EVENT_READ_READY:
  case VC_EVENT_EOS:
    if (ivio == 0) {
      ivio = (VIO *) data;
    }
    done = parse();
    if (done || (event == VC_EVENT_EOS))
      startResponse();
    break;
  case VC_EVENT_ERROR:
    killStateMachine();
    break;
  default:
    printf("requestDataEvent got unexpected %s\n", event_int_to_string(event));
    break;
  }
  return (0);
}

////////////////////////////////////////////////////////////////////////
//
//      StateMachine::parse
//
//      Called on a chunk of bytes representing a piece of a request
//      headers, stripping out the URL, headers, etc.  The state of
//      the parse is kept in the variable <parse_mode> which represents
//      the current parsing step.
//
////////////////////////////////////////////////////////////////////////

int
StateMachine::parse()
{
  char *ptr;
  int n, orig_n;

  ptr = reader->start();
  n = reader->block_read_avail();
  orig_n = n;

  while (n) {
//      printf("PARSE MODE %d: [",parse_mode);
//      fwrite(ptr,1,n,stdout);
//      printf("]\n");

    if (parse_mode == 0)        // skipping over method
    {
      while (*ptr != ' ' && n) {
        ++ptr;
        --n;
      }
      if (n == 0)
        break;
      parse_mode = 1;
    } else if (parse_mode == 1) // looking for URL start
    {
      while (*ptr == ' ' && n) {
        ++ptr;
        --n;
      }
      if (n == 0)
        break;
      parse_mode = 2;
      url_end = url;
    } else if (parse_mode == 2) // looking for URL end
    {
      while (*ptr != ' ' && n) {
        *url_end++ = *ptr++;
        --n;
      }
      if (n == 0)
        break;
      *url_end = '\0';
//          printf("URL = '%s'\n",url);
      parse_mode = 3;
    } else if (parse_mode == 3) // looking for end of start line
    {
      while (*ptr != '\n' && n) {
        ++ptr;
        --n;
      }
      if (n == 0)
        break;
      ++ptr;
      --n;
      parse_mode = 4;
      hdr_end = hdr;
    } else if (parse_mode == 4) // read header until EOL
    {
      if (*ptr == '\r') {
        ++ptr;
        --n;
        parse_mode = 5;
        break;
      }
      while (*ptr != '\n' && n) {
        *hdr_end++ = *ptr++;
        --n;
      }
      if (n == 0)
        break;
      ++ptr;
      --n;
      if (*(hdr_end - 1) == '\r')
        --hdr_end;
    } else if (parse_mode == 5) // got empty line CR
    {
      ++ptr;
      --n;                      // consume LF
    }
  }

  reader->consume(orig_n - n);
  return (parse_mode == 5);
}

////////////////////////////////////////////////////////////////////////
//
//      StateMachine::responseDataEvent
//
//      Called back whenever there is an event and we are generating
//      response data.  The event can come from the read or write side.
//
////////////////////////////////////////////////////////////////////////

int
StateMachine::responseDataEvent(int event, void *data)
{
  int n;

  REMEMBER(event, (data) ? ((VIO *) data)->ndone : -1);

//    printf("responseDataEvent got %s\n",event_int_to_string(event));
  switch (event) {
  case VC_EVENT_WRITE_READY:
    fill();
    break;
  case VC_EVENT_WRITE_COMPLETE:
    killStateMachine();
    break;
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE:
    n = reader->read_avail();
    reader->consume(n);
    break;
  case VC_EVENT_ERROR:
    killStateMachine();
    break;
  default:
    printf("responseDataEvent got unexpected %s\n", event_int_to_string(event));
    break;
  }
  return (0);
}

////////////////////////////////////////////////////////////////////////
//
//      StateMachine::fill
//
//      This is the synthetic server routine that writes a header,
//      and a synthetically-generated body.
//
////////////////////////////////////////////////////////////////////////

int
StateMachine::fill()
{
  char *ptr;
  int n;
  int hdr_bytes_left, body_bytes_left, copy_size;

  // TODO: I don't know if this call to obuf->write_avail() is necessary...
  n = obuf->write_avail();        // used to grow blocks
  n = obuf->block_write_avail();
  ptr = obuf->end();

  hdr_bytes_left = header_size - header_size_written;
  body_bytes_left = body_size - body_size_written;

  // write header
  if (hdr_bytes_left) {
    copy_size = min(n, hdr_bytes_left);
    memcpy(ptr, scratch_space + header_size_written, copy_size);
    ptr += copy_size;
    header_size_written += copy_size;
    hdr_bytes_left -= copy_size;
    n -= copy_size;
    obuf->fill(copy_size);
  }
  // write body
  if ((hdr_bytes_left == 0) && body_bytes_left) {
    copy_size = min(n, body_bytes_left);
    memset(ptr, 'B', copy_size);
    ptr += copy_size;
    body_size_written += copy_size;
    body_bytes_left -= copy_size;
    n -= copy_size;
    obuf->fill(copy_size);
  }

  return ((hdr_bytes_left == 0) && (body_bytes_left == 0));
}

////////////////////////////////////////////////////////////////////////
//
//      StateMachine::startResponse
//
//      Called when the request is parsed, to set up the state
//      machine to generate response data.
//
////////////////////////////////////////////////////////////////////////

void
StateMachine::startResponse()
{
  ivio->done();                 // ignore future input data

  SET_HANDLER(&StateMachine::responseDataEvent);
  url_size = strlen(url);
  body_size = computeBodySize(url);
  snprintf(scratch_space, sizeof(scratch_space), "HTTP/1.0 200 OK\r\nContent-length: %d\r\n\r\n", body_size);
  header_size = strlen(scratch_space);

  total_size = header_size + body_size;
  header_size_written = 0;
  body_size_written = 0;
  fill();
  ovio = nvc->do_io(VIO::WRITE, this, total_size, obuf);
}

////////////////////////////////////////////////////////////////////////
//
//      StateMachine::killStateMachine
//
//      Called when the response is generated, to clean up and
//      destroy the state machine.
//
////////////////////////////////////////////////////////////////////////

void
StateMachine::killStateMachine()
{
  nvc->do_io(VIO::CLOSE);       // do I need to cancel vios first?
//    printf("*** State Machine Dying **\n");
  delete this;
}


////////////////////////////////////////////////////////////////////////
//
//      StateMachine::computeBodySize
//
//      Chooses a body size to send based on the URL.
//
////////////////////////////////////////////////////////////////////////

int
StateMachine::computeBodySize(char *url_str)
{
  int l;
  char *p;

  for (p = url_str + strlen(url_str); (*p != '/' && p > url_str); p--);
#ifdef SENDER_IS_JTEST
  // coverity[secure_coding]
  if ((*p == '/') && (sscanf(p, "/%d", &l) == 1)) {
    return (l);
  } else
    printf("Unable to get doc body size [%s]\n", url_str);
#else
  // coverity[secure_coding]
  if ((*p == '/') && (sscanf(p, "/length%d.html", &l) == 1))
    return (l);
  else
    return (G.default_body_size);
#endif
}


////////////////////////////////////////////////////////////////////////
//
//      AcceptContinuation
//
//      This is the continuation that is notified of connection
//      accepts.  It spawns a state machine to handle the transaction.
//      New transactions can be accepted and spawned while other
//      transactions are still in progress.
//
////////////////////////////////////////////////////////////////////////

class AcceptContinuation:public Continuation
{
public:
  int startEvent(int event, NetVConnection * nvc)
  {
    if (event == NET_EVENT_ACCEPT) {
//          printf("*** Got Request, %d Transactions Currently Open ***\n",
//                 state_machine_count);
      StateMachine *sm = new StateMachine(nvc);
        sm->ivio = nvc->do_io(VIO::READ, sm, INT64_MAX, sm->ibuf);
    } else
    {
      printf("AcceptContinuation error %d\n", event);
    }
    return (0);
  }

AcceptContinuation():Continuation(NULL)
                                // Allow multi-thread callers
  {
    SET_HANDLER(&AcceptContinuation::startEvent);
  }
};


////////////////////////////////////////////////////////////////////////
//
//      Main Entry Point
//
//      This is the main entry point to start the accepting server.
//
////////////////////////////////////////////////////////////////////////

int
run_TestHook()
{
  int i;
  Continuation *c;

  printf("*** BRIOCORE Server Running ***\n");
  for (i = 1; i <= G.accept_count; i++) {
    NetProcessor::AcceptOptions opt;
    c = new AcceptContinuation();
    opt.local_port = G.accept_port;
    // [amc] I have absolutely no idea what accept_spawn is supposed
    // to control.  I am just guessing it's accept_threads. Code
    // tracing indicated it ended up as the value for the address
    // family and then ignored. It's declared as an int but assigned a
    // bool value so I'm not even sure what type it's intended to be.
    opt.accept_threads = G.accept_spawn;
    (void) netProcessor.accept(c, opt);
  }
  return (0);
}
