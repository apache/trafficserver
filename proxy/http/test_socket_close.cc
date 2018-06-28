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

#include <iostream.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/filio.h>
#include <fcntl.h>
#include <errno.h>
#include "/usr/include/netdb.h"
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fstream.h>
#include <strstream.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

enum Task_t {
  TASK_NONE = 0,
  TASK_DONE,
  TASK_CONNECT,
  TASK_LISTEN_SETUP,
  TASK_ACCEPT,
  TASK_SHUTDOWN_OUTPUT,
  TASK_SHUTDOWN_INPUT,
  TASK_SHUTDOWN_BOTH,
  TASK_TRY_READ,
  TASK_TRY_WRITE,
  TASK_TRY_WRITE_THEN_SHUTDOWN_OUTPUT,
  TASK_TRY_WRITE_THEN_SHUTDOWN_BOTH,
  TASK_COUNT
};

enum State_t {
  STATE_IDLE = 0,
  STATE_DONE,
  STATE_ERROR,
};

enum Connection_t {
  CONNECTION_CLIENT,
  CONNECTION_SERVER,
};

enum Scenario_t {
  SERVER_WRITE_CLIENT_READ,

  SERVER_SHUTDOWN_OUTPUT_CLIENT_TRY_READ,
  SERVER_SHUTDOWN_INPUT_CLIENT_TRY_READ,
  SERVER_SHUTDOWN_BOTH_CLIENT_TRY_READ,
  SERVER_SHUTDOWN_OUTPUT_CLIENT_TRY_WRITE,
  SERVER_SHUTDOWN_INPUT_CLIENT_TRY_WRITE,
  SERVER_SHUTDOWN_BOTH_CLIENT_TRY_WRITE,

  CLIENT_SHUTDOWN_OUTPUT_SERVER_TRY_READ,
  CLIENT_SHUTDOWN_INPUT_SERVER_TRY_READ,
  CLIENT_SHUTDOWN_BOTH_SERVER_TRY_READ,
  CLIENT_SHUTDOWN_OUTPUT_SERVER_TRY_WRITE,
  CLIENT_SHUTDOWN_INPUT_SERVER_TRY_WRITE,
  CLIENT_SHUTDOWN_BOTH_SERVER_TRY_WRITE,

  SERVER_WRITE_IMMIDIATE_SHUTDOWN_CLIENT_WRITE
};

struct State {
  State_t state;
  int tasks_count;
  Task_t tasks[100];
  int64_t nbytes_write; // number of bytes to write
  intte_t nbytes_read;  // number of bytes to read

  State() : state(STATE_IDLE), tasks_count(0) {}
};

struct Conn {
  Connection_t connection_type;
  int listen_s;
  int s;
  struct sockaddr_in addr;
  State state;
  // State_t            state;
  int state_delay_ms;
};

Conn client, server;
int port_number;
Task_t server_set_next_client_task[TASK_COUNT];
Task_t client_set_next_server_task[TASK_COUNT];
char write_buf[10];
char read_buf[10];
int state_delay_ms = 0;

#define IS_DONE(c) (c.state.state == STATE_DONE || c.state.state == STATE_ERROR)

void main_loop();
void state_act(Conn *c);
void state_act_task(Conn *c);
int do_connect(Conn *from, Conn *to);
int do_listen_setup(Conn *c, int port_number);
int do_accept(Conn *c);
int create_nonblocking_socket();
int set_nonblocking_socket(int s);
int do_shutdown(int s, Task_t task);
int do_try_read(int s, char *buf, int length);
int do_try_write(int s, char *buf, int length);
void setup_scenario(Scenario_t scenario);
void dequeue_task(Conn *c);
///////////////////////////////////////////////////////////
//
//  main()
//
///////////////////////////////////////////////////////////
int
main(int argc, char **argv)
{
  if (argc < 2) {
    cout << "test_socket_close <port number> <state delay ms>" << endl;
    return (0);
  }
  port_number = atoi(argv[1]);

  if (argc >= 3) {
    state_delay_ms = atoi(argv[2]);
  }

  memset(&client, '\0', sizeof(client));
  memset(&server, '\0', sizeof(server));

  memset(&write_buf, 'B', sizeof(write_buf));
  memset(&read_buf, '\0', sizeof(read_buf));

  client.connection_type = CONNECTION_CLIENT;
  server.connection_type = CONNECTION_SERVER;

  client.state.state = STATE_IDLE;
  server.state.state = STATE_IDLE;

  client.state.tasks_count = 0;
  server.state.tasks_count = 1;
  server.state.tasks[0]    = TASK_LISTEN_SETUP;

  client.state_delay_ms = state_delay_ms;
  server.state_delay_ms = state_delay_ms;

  ////////////////////
  // set next state //
  ////////////////////
  setup_scenario(SERVER_WRITE_IMMIDIATE_SHUTDOWN_CLIENT_WRITE);
  // setup_scenario(SERVER_WRITE_CLIENT_READ);

  main_loop();

  return (0);
}

///////////////////////////////////////////////////////////
//
//  main_loop()
//
///////////////////////////////////////////////////////////
void
main_loop()
{
  while (!IS_DONE(client) || !IS_DONE(server)) {
    if (client.state.tasks_count > 0 && !IS_DONE(client)) {
      state_act(&client);
    }
    if (server.state.tasks_count > 0 && !IS_DONE(server)) {
      state_act(&server);
    }
  }
  return;
}

///////////////////////////////////////////////////////////
//
//  state_act()
//
///////////////////////////////////////////////////////////
void
state_act(Conn *c)
{
  Task_t saved_task = c->state.tasks[0];

  Conn &cr = *c;

  while (c->state.tasks_count > 0 && !IS_DONE(cr)) {
    if (c->state_delay_ms) {
      poll(0, 0, c->state_delay_ms);
    }
    state_act_task(c);
  }

  if (IS_DONE(cr)) {
    cr.state.tasks_count = 1;
    cr.state.tasks[0]    = TASK_DONE;
  }

  if ((c == &client && IS_DONE(server)) || (c == &server && IS_DONE(client))) {
    cr.state.tasks_count = 1;
    cr.state.tasks[0]    = saved_task;
  } else if (c == &client) {
    server.state.tasks_count = 1;
    server.state.tasks[0]    = client_set_next_server_task[saved_task];
  } else {
    client.state.tasks_count = 1;
    client.state.tasks[0]    = server_set_next_client_task[saved_task];
  }
  return;
}

///////////////////////////////////////////////////////////
//
//  state_act_task()
//
///////////////////////////////////////////////////////////
void
state_act_task(Conn *c)
{
  int error;
  char write_ch = 'T', read_ch;
  int r;

  Task_t saved_task = c->state.tasks[0];

  switch (c->state.tasks[0]) {
  case TASK_CONNECT:
    assert(c == &client);
    do_connect(&client, &server);
    dequeue_task(&client);
    break;

  case TASK_SHUTDOWN_OUTPUT:
  case TASK_SHUTDOWN_INPUT:
  case TASK_SHUTDOWN_BOTH:
    if (do_shutdown(c->s, c->state.tasks[0]) < 0)
      c->state.state = STATE_ERROR;
    else
      c->state.state = STATE_DONE;

    dequeue_task(c);
    break;

  case TASK_TRY_READ:
    r = do_try_read(c->s, &read_ch, 1);
    if (r > 0)
      c->state.state = STATE_IDLE;
    else if (r == 0)
      c->state.state = STATE_DONE; // EOS
    else if (r != -EAGAIN)
      c->state.state = STATE_ERROR; // error
    dequeue_task(c);
    break;

  case TASK_TRY_WRITE:
    r = do_try_write(c->s, &write_ch, 1);
    if (r <= 0 && r != -EAGAIN)
      c->state.state = STATE_ERROR; // error
    else
      c->state.state = STATE_IDLE;
    dequeue_task(c);
    break;

  case TASK_TRY_WRITE_THEN_SHUTDOWN_OUTPUT:
  case TASK_TRY_WRITE_THEN_SHUTDOWN_BOTH:
    r = do_try_write(c->s, write_buf, c->state.nbytes_write);
    if (r <= 0 && r != -EAGAIN)
      c->state.state = STATE_ERROR; // error
    else {
      c->state.nbytes_write -= r;
      if (c->state.nbytes_write == 0) {
        // do shutdown
        if (do_shutdown(c->s, c->state.tasks[0]) < 0)
          c->state.state = STATE_ERROR;
        else
          c->state.state = STATE_DONE;

        dequeue_task(c);
      }
    }
    break;

  case TASK_LISTEN_SETUP:
    assert(c == &server);
    if (do_listen_setup(&server, port_number) > 0) {
      dequeue_task(&server);
    }
    break;

  case TASK_ACCEPT:
    assert(c == &server);
    if (do_accept(&server) > 0) {
      dequeue_task(&server);
    }
    break;
  }

  return;
}

///////////////////////////////////////////////////////////
//
//  do_connect()
//
//  'to' must be listening
///////////////////////////////////////////////////////////
int
do_connect(Conn *from, Conn *to)
{
  assert(to->listen_s > 0);
  int error;

  // create a non-blocking socket
  if ((from->s = create_nonblocking_socket()) < 0) {
    from->state.state = STATE_ERROR;
    return (from->s);
  }
  // connect
  if (connect(from->s, (struct sockaddr *)&to->addr, sizeof(to->addr)) < 0) {
    error = -errno;
    if (error != -EINPROGRESS) {
      ::close(from->s);
      from->state.state = STATE_ERROR;
      cout << "connect failed (" << error << ")" << endl;
      return (error);
    }
  }
  // success
  cout << "connect is done" << endl;
  from->state.state = STATE_IDLE;
  return (from->s);
}

///////////////////////////////////////////////////////////
//
//  do_listen_setup()
//
///////////////////////////////////////////////////////////
int
do_listen_setup(Conn *c, int port)
{
  int error;

  c->addr.sin_family = AF_INET;
  memset(&c->addr.sin_zero, 0, 8);
  c->addr.sin_addr.s_addr = htonl(INADDR_ANY);
  c->addr.sin_port        = htons(port);

  // create a non-blocking socket
  if ((c->listen_s = create_nonblocking_socket()) < 0) {
    c->state.state = STATE_ERROR;
    return (c->listen_s);
  }
  // bind socket to port
  if (bind(c->listen_s, (struct sockaddr *)&c->addr, sizeof(c->addr)) < 0) {
    error = -errno;
    ::close(c->listen_s);
    c->state.state = STATE_ERROR;
    cout << "bind failed (" << error << ")" << endl;
    return (error);
  }
  // listen
  if (listen(c->listen_s, 5) < 0) {
    error = -errno;
    ::close(c->listen_s);
    c->state.state = STATE_ERROR;
    cout << "listen failed (" << error << ")" << endl;
    return (-1);
  }
  // success
  cout << "listen is done" << endl;
  c->state.state = STATE_IDLE;

  return (c->listen_s);
}

///////////////////////////////////////////////////////////
//
//  do_accept()
//
///////////////////////////////////////////////////////////
int
do_accept(Conn *c)
{
  assert(c->listen_s > 0);

  // check if socket is ready for read
  fd_set readfds;
  struct timeval timeout;
  int addrlen;

  FD_ZERO(&readfds);
  FD_SET(c->listen_s, &readfds);
  timeout.tv_sec  = 0;
  timeout.tv_usec = 10; /* 0.01 ms */

  if (select(c->listen_s + 1, &readfds, 0, 0, &timeout) > 0) {
    addrlen = sizeof(c->addr);
    c->s    = accept(c->listen_s, (struct sockaddr *)&c->addr, &addrlen);
    if (c->s < 0) {
      c->s = -errno;
      cout << "accept failed (" << c->s << ")" << endl;
      c->state.state = STATE_ERROR;
      return (c->s);
    }
    if ((c->s = set_nonblocking_socket(c->s)) < 0) {
      c->state.state = STATE_ERROR;
      return (c->s);
    }
    c->state.state = STATE_IDLE;
  }
  cout << "accept is done" << endl;
  return (c->s);
}

///////////////////////////////////////////////////////////
//
//  create_nonblocking_socket()
//
///////////////////////////////////////////////////////////
int
create_nonblocking_socket()
{
  int s;

  if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    s = -errno;
    cout << "socket failed (" << s << ")" << endl;
    return (s);
  }
  return (set_nonblocking_socket(s));
}

///////////////////////////////////////////////////////////
//
//  set_nonblocking_socket()
//
///////////////////////////////////////////////////////////
int
set_nonblocking_socket(int s)
{
  int error = 0;
  int on    = 1;

  if (fcntl(s, F_SETFL, O_NDELAY) < 0) {
    error = -errno;
    ::close(s);
    cout << "fcntl F_SETFD O_NDELAY failed (" << error << ")" << endl;
    return (error);
  }

  return (s);
}

///////////////////////////////////////////////////////////
//
//  do_shutdown()
//
///////////////////////////////////////////////////////////
int
do_shutdown(int s, Task_t task)
{
  int howto, error;

  switch (task) {
  case TASK_SHUTDOWN_OUTPUT:
  case TASK_TRY_WRITE_THEN_SHUTDOWN_OUTPUT:
    howto = 1;
    break;
  case TASK_SHUTDOWN_INPUT:
    howto = 0;
    break;
  case TASK_SHUTDOWN_BOTH:
  case TASK_TRY_WRITE_THEN_SHUTDOWN_BOTH:
    howto = 2;
    break;
  default:
    assert(!"expected a shutdown state");
    break;
  }
  if (shutdown(s, howto) < 0) {
    error = -errno;
    cout << "shutdown failed (" << error << ")" << endl;
    return (error);
  }
  // success
  cout << "sutdowm is done" << endl;
  return (0);
}

///////////////////////////////////////////////////////////
//
//  do_try_read()
//
///////////////////////////////////////////////////////////
int
do_try_read(int s, char *buf, int length)
{
  int r;

  if ((r = read(s, buf, length)) < 0) {
    r = -errno;
    if (r != -EWOULDBLOCK) // EWOULDBLOCK == EAGAIN
    {
      cout << "read failed (" << r << ")" << endl;
    }
  } else if (r == 0) {
    cout << "connection closed" << endl;
  } else {
    // read is successful
    for (int i = 0; i < r; i++) {
      cout << buf[i] << ' ';
    }
  }
  return (r);
}

///////////////////////////////////////////////////////////
//
//  do_try_write()
//
///////////////////////////////////////////////////////////
int
do_try_write(int s, char *buf, int length)
{
  int r;

  if ((r = write(s, buf, length)) <= 0) {
    r = -errno;
    if (r != -EWOULDBLOCK) {
      cout << "write failed (" << r << ")" << endl;
    }
  }
  return (r);
}

///////////////////////////////////////////////////////////
//
//  setup_scenario()
//
///////////////////////////////////////////////////////////
void
setup_scenario(Scenario_t scenario)
{
  switch (scenario) {
  case SERVER_WRITE_CLIENT_READ:
    server_set_next_client_task[TASK_LISTEN_SETUP] = TASK_CONNECT;
    client_set_next_server_task[TASK_CONNECT]      = TASK_ACCEPT;
    server_set_next_client_task[TASK_ACCEPT]       = TASK_TRY_READ;
    client_set_next_server_task[TASK_TRY_READ]     = TASK_TRY_WRITE;
    server_set_next_client_task[TASK_TRY_WRITE]    = TASK_TRY_READ;
    break;

  case SERVER_SHUTDOWN_OUTPUT_CLIENT_TRY_READ:
    server_set_next_client_task[TASK_LISTEN_SETUP]    = TASK_CONNECT;
    server_set_next_client_task[TASK_ACCEPT]          = TASK_TRY_READ;
    server_set_next_client_task[TASK_SHUTDOWN_OUTPUT] = TASK_TRY_READ;

    client_set_next_server_task[TASK_CONNECT]  = TASK_ACCEPT;
    client_set_next_server_task[TASK_TRY_READ] = TASK_SHUTDOWN_OUTPUT;
    break;

  case SERVER_SHUTDOWN_INPUT_CLIENT_TRY_READ:
    break;
  case SERVER_SHUTDOWN_BOTH_CLIENT_TRY_READ:
    break;
  case SERVER_SHUTDOWN_OUTPUT_CLIENT_TRY_WRITE:
    break;
  case SERVER_SHUTDOWN_INPUT_CLIENT_TRY_WRITE:
    break;
  case SERVER_SHUTDOWN_BOTH_CLIENT_TRY_WRITE:
    break;
  case CLIENT_SHUTDOWN_OUTPUT_SERVER_TRY_READ:
    break;
  case CLIENT_SHUTDOWN_INPUT_SERVER_TRY_READ:
    break;
  case CLIENT_SHUTDOWN_BOTH_SERVER_TRY_READ:
    break;
  case CLIENT_SHUTDOWN_OUTPUT_SERVER_TRY_WRITE:
    break;
  case CLIENT_SHUTDOWN_INPUT_SERVER_TRY_WRITE:
    break;
  case CLIENT_SHUTDOWN_BOTH_SERVER_TRY_WRITE:
    break;
  case SERVER_WRITE_IMMIDIATE_SHUTDOWN_CLIENT_WRITE:
    server_set_next_client_task[TASK_LISTEN_SETUP]                 = TASK_CONNECT;
    client_set_next_server_task[TASK_CONNECT]                      = TASK_ACCEPT;
    server_set_next_client_task[TASK_ACCEPT]                       = TASK_TRY_READ;
    client_set_next_server_task[TASK_TRY_READ]                     = TASK_TRY_WRITE_THEN_SHUTDOWN_BOTH;
    server_set_next_client_task[TASK_TRY_WRITE_THEN_SHUTDOWN_BOTH] = TASK_TRY_READ;
    server_set_next_client_task[TASK_DONE]                         = TASK_TRY_READ;
    server.state.nbytes_write                                      = sizeof(write_buf);
    break;
  }
  return;
}

void
dequeue_task(Conn *c)
{
  if (c->state.tasks_count == 0)
    return;

  c->state.tasks_count--;
  for (int i = 0; i < c->state.tasks_count; i++) {
    c->state.tasks[i] = c->state.tasks[i + 1];
  }
  return;
}
