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

   sio_loop.cc

   Description:

   
 ****************************************************************************/


#include "sio_loop.h"

#include "Diags.h"

int num_active_fd = 0;
DLL<FD_Handler> fd_list;

int num_active_events = 0;
DLL<S_Event> event_list;

#define EXIT_FAILURE 1

const int default_poll_timeout = 500;

void
panic_perror(char *s)
{
  perror(s);
  SIO::do_exit(1);
}

int
SIO::open_server(unsigned short int port)
{
  struct linger lngr;
  int sock;
  int one = 1;
  int err = 0;

  /* Create the socket. */
  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    perror("socket");
    do_exit(EXIT_FAILURE);
  }
  struct sockaddr_in name;

  /* Give the socket a name. */
  name.sin_family = AF_INET;
  name.sin_port = htons(port);
  name.sin_addr.s_addr = htonl(INADDR_ANY);
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *) &one, sizeof(one)) < 0) {
    perror((char *) "setsockopt");
    do_exit(EXIT_FAILURE);
  }
  if ((err = bind(sock, (struct sockaddr *) &name, sizeof(name))) < 0) {
    if (errno == EADDRINUSE)
      return -EADDRINUSE;
    perror("bind");
    do_exit(EXIT_FAILURE);
  }

  int addrlen = sizeof(name);
  if ((err = getsockname(sock, (struct sockaddr *) &name,
#ifndef linux
                         &addrlen
#else
                         (socklen_t *) & addrlen
#endif
       )) < 0) {
    perror("getsockname");
    do_exit(EXIT_FAILURE);
  }
  ink_assert(addrlen);

  /* Tell the socket not to linger on exit */
  lngr.l_onoff = 0;
  lngr.l_linger = 0;
  if (setsockopt(sock, SOL_SOCKET, SO_LINGER, (char *) &lngr, sizeof(struct linger)) < 0) {
    perror("setsockopt");
    do_exit(EXIT_FAILURE);
  }

  if (listen(sock, 1024) < 0) {
    perror("listen");
    do_exit(EXIT_FAILURE);
  }

  /* put the socket in non-blocking mode */
  if (fcntl(sock, F_SETFL, O_NONBLOCK) < 0) {
    perror("fcntl");
    do_exit(EXIT_FAILURE);
  }

  Debug("socket", "opening server on %d port %d\n", sock, /*name.sin_port */ port);

  return sock;
}

int
SIO::accept_sock(int sock)
{
  struct sockaddr_in clientname;
  int size = sizeof(clientname);
  int new_fd = 0;

  do {
    new_fd = accept(sock, (struct sockaddr *) &clientname,
#ifndef linux
                    &size
#else
                    (socklen_t *) & size
#endif
      );

    if (new_fd < 0) {
      if (errno == EAGAIN || errno == ENOTCONN)
        return 0;
      if (errno == EINTR || errno == ECONNABORTED)
        continue;
      panic_perror("accept");
    }
  } while (new_fd < 0);

  if (fcntl(new_fd, F_SETFL, O_NONBLOCK) < 0)
    panic_perror("fcntl");

  int enable = 1;
  if (setsockopt(new_fd, IPPROTO_TCP, TCP_NODELAY, (const char *) &enable, sizeof(enable)) < 0) {
    perror("setsockopt");
  }

  return new_fd;
}

int
SIO::make_client(unsigned int addr, int port)
{
  struct linger lngr;

  int sock = socket(PF_INET, SOCK_STREAM, 0);
  if (sock < 0)
    panic_perror("socket");

  if (fcntl(sock, F_SETFL, O_NONBLOCK) < 0)
    panic_perror("fcntl");

  /* tweak buffer size so that remote end can't close connection too fast */

  int bufsize = 2048;           // FIX should not hardcode
  if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (const char *) &bufsize, sizeof(bufsize)) < 0)
    panic_perror("setsockopt");
  if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (const char *) &bufsize, sizeof(bufsize)) < 0)
    panic_perror("setsockopt");
  int enable = 1;
  if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char *) &enable, sizeof(enable)) < 0)
    panic_perror("setsockopt");

  /* Tell the socket not to linger on exit */
  lngr.l_onoff = 0;
  lngr.l_linger = 0;

  if (setsockopt(sock, SOL_SOCKET, SO_LINGER, (char *) &lngr, sizeof(struct linger)) < 0) {
    perror("setsockopt");
    exit(EXIT_FAILURE);
  }

  /* Give the socket a name. */
  struct sockaddr_in name;
  name.sin_family = AF_INET;
  name.sin_port = htons(port);
  name.sin_addr.s_addr = addr;

  Debug("socket", "connecting to %u.%u.%u.%u:%d\n",
        ((unsigned char *) &addr)[0], ((unsigned char *) &addr)[1],
        ((unsigned char *) &addr)[2], ((unsigned char *) &addr)[3], port);

  while (connect(sock, (struct sockaddr *) &name, sizeof(name)) < 0) {
    if (errno == EINTR)
      continue;
    if (errno == EINPROGRESS)
      break;
    Debug("socket", "connect failed errno = %d\n", errno);
    close(sock);
    return -1;
  }

  return sock;
}

S_Continuation::S_Continuation():
my_handler(NULL)
{
}

S_Continuation::~S_Continuation()
{
}

void
S_Continuation::handle_event(s_event_t event, void *data)
{
  SCont_Handler func_ptr = my_handler;
  (this->*func_ptr) (event, data);
}

FD_Handler::FD_Handler():
fd(-1), poll_interest(POLL_INTEREST_NONE), link(), S_Continuation()
{
}

FD_Handler::~FD_Handler()
{
  if (fd >= 0) {
    close(fd);
    fd = -1;
  }
}

int
FD_Handler::clear_non_block_flag()
{
  if (fd >= 0) {
    int flags;
    if ((flags = fcntl(fd, F_GETFL, 0)) < 0) {
      return -errno;
    }

    int mask = ~(O_NONBLOCK);
    flags = flags & mask;

    if ((flags = fcntl(fd, F_SETFL, flags)) < 0) {
      return -errno;
    }

    return 0;
  }

  return 1;
}

int
FD_Handler::set_linger(int on, int ltime)
{

  struct linger linfo;
  linfo.l_onoff = on;
  linfo.l_linger = ltime;

  if (fd >= 0) {

    if (setsockopt(fd, SOL_SOCKET, SO_LINGER, (char *) &linfo, sizeof(struct linger)) < 0) {
      return -errno;
    } else {
      return 0;
    }
  }

  return 1;
}



void
SIO::add_fd_handler(FD_Handler * fdh)
{
  fd_list.push(fdh);
  num_active_fd++;
}

void
SIO::remove_fd_handler(FD_Handler * fdh)
{
  fd_list.remove(fdh);
  num_active_fd--;
}

S_Action::S_Action():
cancelled(0), s_cont(NULL), action_link()
{
}

S_Action::~S_Action()
{
  s_cont = NULL;
}

void
S_Action::cancel()
{
  ink_debug_assert(cancelled == 0);
  cancelled = 1;
}

S_Event::S_Event():
when((ink_hrtime) 0), event_link(), S_Action()
{
}

S_Event::~S_Event()
{
}

S_Event *
SIO::schedule_in(S_Continuation * c, int ms)
{
  S_Event *e = new S_Event;

  e->when = ink_get_based_hrtime_internal() + HRTIME_MSECONDS(ms);
  e->s_cont = c;

  event_list.push(e, e->event_link);
  num_active_events++;

  return e;
}

void
remove_event(S_Event * e)
{

  event_list.remove(e, e->event_link);
  num_active_events--;
  delete e;

}

void
run_event(S_Event * e)
{

  ink_debug_assert(!e->cancelled);
  if (!e->cancelled) {
    Debug("event", "Calling back s_cont 0x%X with timer event", e->s_cont);
    e->s_cont->handle_event(SEVENT_TIMER, e);
  }

  remove_event(e);
}

static ink_hrtime next_etime = 0;
int
run_events(ink_hrtime now)
{
  int events_run = 0;

  S_Event *e = event_list.head;
  S_Event *next;

  while (e != NULL) {
    next = e->event_link.next;
    if (e->cancelled) {
      remove_event(e);
    } else if (e->when < now) {
      events_run++;
      run_event(e);
    } else if (next_etime == 0 || e->when < next_etime) {
      next_etime = e->when;
    }
    e = next;
  }

  return events_run;
}

void
update_next_etime(ink_hrtime now)
{

  S_Event *e = event_list.head;

  while (e != NULL) {
    if (e->when < now) {
      // We want to get to polling so prolong for 5 ms
      next_etime = now + HRTIME_MSECONDS(5);
    } else if (next_etime == 0 || e->when < next_etime) {
      next_etime = e->when;
    }
    e = e->event_link.next;
  }
}

void
SIO::run_loop_once()
{

  // Loop over and see if we have events that need processing
  ink_hrtime now = ink_get_based_hrtime_internal();

  next_etime = 0;
  if (run_events(now) > 0) {
    // We run some events and thus could have scheduled
    //   new events so up update next_etime
    next_etime = 0;
    update_next_etime(now);
  }

  int poll_timeout;
  if (next_etime == 0) {
    poll_timeout = default_poll_timeout;
  } else {
    poll_timeout = ink_hrtime_to_msec(next_etime - now);
    Debug("event", "%d ms to next event", poll_timeout);
    if (poll_timeout > default_poll_timeout) {
      poll_timeout = default_poll_timeout;

      if (poll_timeout <= 0) {
        poll_timeout = 5;
      }
    }
  }

  // Build our poll structure
  struct pollfd *pfd = (pollfd *) malloc(sizeof(struct pollfd) * num_active_fd);

  FD_Handler *cur = fd_list.head;
  int i = 0;
  while (cur != NULL) {
    pfd[i].events = 0;
    pfd[i].revents = 0;
    pfd[i].fd = cur->fd;

    switch (cur->poll_interest) {
    case POLL_INTEREST_READ:
      pfd[i].events = POLLIN;
      break;
    case POLL_INTEREST_WRITE:
      pfd[i].events = POLLOUT;
    case POLL_INTEREST_RW:
      pfd[i].events = POLLIN | POLLOUT;
      break;
    }
    cur = cur->link.next;
    i++;
  }

  ink_debug_assert(i == num_active_fd);
  //  Debug("poll", "Polling on %d fds for %d ms", i, poll_timeout);

  int r = poll(pfd, i, poll_timeout);

  if (r < 0) {
    if (0 && errno != EINTR) {
      Warning("main poll failed: %s", strerror(errno));
    }
  } else if (r > 0) {

    cur = fd_list.head;
    for (int j = 0; j < i; j++) {
      FD_Handler *next = cur->link.next;
      if (pfd[j].revents != 0) {
        cur->handle_event(SEVENT_POLL, pfd + j);
      }
      cur = next;
    }
  }

  free(pfd);
}

void
SIO::run_loop()
{
  while (1) {
    run_loop_once();
  }
}

static S_Continuation *exit_handler = NULL;

void
SIO::add_exit_handler(S_Continuation * c)
{
  exit_handler = c;
}

void
SIO::do_exit(int status)
{

  if (exit_handler) {
    exit_handler->handle_event(SEVENT_EXIT_NOTIFY, (void *) status);
    exit(status);
  } else {
    exit(status);
  }
}
