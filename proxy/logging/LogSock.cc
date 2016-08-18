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

#include "ts/ink_inet.h"
#include "ts/ink_string.h"
#include "P_EventSystem.h"
#include "Error.h"

#include "LogSock.h"
#include "LogUtils.h"

static const int LS_SOCKTYPE = SOCK_STREAM;
static const int LS_PROTOCOL = 0;

/**
  LogSock

  The constructor establishes the connection table (ct) and initializes the
  first entry of the table (index 0) to be the port on which new
  connections are accepted.
*/
LogSock::LogSock(int max_connects) : ct((ConnectTable *)NULL), m_accept_connections(false), m_max_connections(max_connects + 1)
{
  ink_assert(m_max_connections > 0);

  //
  // allocate space for the connection table.
  //
  ct = new ConnectTable[m_max_connections];
  ink_assert(ct != NULL);
  for (int i = 0; i < m_max_connections; ++i) {
    init_cid(i, NULL, 0, -1, LogSock::LS_STATE_UNUSED);
  }

  Debug("log-sock", "LogSocket established");
}

/**
  LogSock::~LogSock

  Shut down all connections and delete memory for the tables.
*/
LogSock::~LogSock()
{
  Debug("log-sock", "shutting down LogSocket on [%s:%d]", ct[0].host, ct[0].port);

  this->close();  // close all connections
  this->close(0); // close accept socket
  delete[] ct;    // delete the connection table
}

/**
  LogSock::listen

  This routine sets up the LogSock to begin accepting connections on the
  given @param accept port.  A maximum number of connections is also specified,
  which is used to establish the size of the listen queue.

  @Return zero if all goes well, -1 otherwise.
*/
int
LogSock::listen(int accept_port, int family)
{
  IpEndpoint bind_addr;
  int size = sizeof(bind_addr);
  char this_host[MAXDNAME];
  int ret;
  ats_scoped_fd accept_sd;

  Debug("log-sock", "Listening ...");

  // Set up local address for bind.
  bind_addr.setToAnyAddr(family);
  if (!bind_addr.isValid()) {
    Warning("Could not set up socket - invalid address family %d", family);
    return -1;
  }
  bind_addr.port() = htons(accept_port);
  size             = ats_ip_size(&bind_addr.sa);

  //
  // create the socket for accepting new connections
  //
  accept_sd = ::socket(family, LS_SOCKTYPE, LS_PROTOCOL);
  if (accept_sd < 0) {
    Warning("Could not create a socket for family %d: %s", family, strerror(errno));
    return -1;
  }
  //
  // Set socket options (NO_LINGER, TCP_NODELAY, SO_REUSEADDR)
  //
  // CLOSE ON EXEC
  if ((ret = safe_fcntl(accept_sd, F_SETFD, 1)) < 0) {
    Warning("Could not set option CLOSE ON EXEC on socket (%d): %s", ret, strerror(errno));
    return -1;
  }
  // NO_LINGER
  struct linger l;
  l.l_onoff  = 0;
  l.l_linger = 0;
  if ((ret = safe_setsockopt(accept_sd, SOL_SOCKET, SO_LINGER, (char *)&l, sizeof(l))) < 0) {
    Warning("Could not set option NO_LINGER on socket (%d): %s", ret, strerror(errno));
    return -1;
  }
  // REUSEADDR
  if ((ret = safe_setsockopt(accept_sd, SOL_SOCKET, SO_REUSEADDR, SOCKOPT_ON, sizeof(int))) < 0) {
    Warning("Could not set option REUSEADDR on socket (%d): %s", ret, strerror(errno));
    return -1;
  }

  // Bind to local address.
  if ((ret = safe_bind(accept_sd, &bind_addr.sa, size)) < 0) {
    Warning("Could not bind port: %s", strerror(errno));
    return -1;
  }

  if ((ret = safe_setsockopt(accept_sd, IPPROTO_TCP, TCP_NODELAY, SOCKOPT_ON, sizeof(int))) < 0) {
    Warning("Could not set option TCP_NODELAY on socket (%d): %s", ret, strerror(errno));
    return -1;
  }

  if ((ret = safe_setsockopt(accept_sd, SOL_SOCKET, SO_KEEPALIVE, SOCKOPT_ON, sizeof(int))) < 0) {
    Warning("Could not set option SO_KEEPALIVE on socket (%d): %s", ret, strerror(errno));
    return -1;
  }

  //
  // if the accept_port argument was zero, then the system just picked
  // one for us, so we need to find out what it was and record it in the
  // connection table correctly.
  //
  if (accept_port == 0) {
    ret = safe_getsockname(accept_sd, &bind_addr.sa, &size);
    if (ret == 0) {
      accept_port = ntohs(bind_addr.port());
    }
  }
  //
  // establish the listen queue for incomming connections
  //
  if ((ret = safe_listen(accept_sd, m_max_connections)) < 0) {
    Warning("Could not establish listen queue: %s", strerror(errno));
    return -1;
  }
  //
  // initialize the first entry of the table for accepting incoming
  // connection requests.
  //
  if (gethostname(&this_host[0], MAXDNAME) != 0) {
    snprintf(this_host, sizeof(this_host), "unknown-host");
  }
  init_cid(0, this_host, accept_port, accept_sd, LogSock::LS_STATE_INCOMING);

  m_accept_connections = true;
  Debug("log-sock", "LogSocket established on [%s:%d]", this_host, accept_port);

  accept_sd.release();
  return 0;
}

/**
  LogSock::accept

  Accept a new connection.  This is a blocking operation, so you may want
  to use one of the non-blocking pending_XXX calls to see if there is a
  connection first.
  @return This returns the table index for the new connection.
*/
int
LogSock::accept()
{
  int cid, connect_sd;
  IpEndpoint connect_addr;
  socklen_t size = sizeof(connect_addr);
  in_port_t connect_port;

  if (!m_accept_connections || ct[0].sd < 0) {
    return LogSock::LS_ERROR_NO_CONNECTION;
  }

  cid = new_cid();
  if (cid < 0) {
    return LogSock::LS_ERROR_CONNECT_TABLE_FULL;
  }

  Debug("log-sock", "waiting to accept a new connection");

  connect_sd = ::accept(ct[0].sd, &connect_addr.sa, &size);
  if (connect_sd < 0) {
    return LogSock::LS_ERROR_ACCEPT;
  }
  connect_port = ntohs(connect_addr.port());

  init_cid(cid, NULL, connect_port, connect_sd, LogSock::LS_STATE_INCOMING);

  Debug("log-sock", "new connection accepted, cid = %d, port = %d", cid, connect_port);

  return cid;
}

/**
  LogSock::connect

  Establish a new connection to another machine [host:port], and place this
  information into the connection and poll tables.
*/
int
LogSock::connect(sockaddr const *ip)
{
  int cid, ret;
  ats_scoped_fd connect_sd;
  uint16_t port;

  if (!ats_is_ip(ip)) {
    Note("Invalid host IP or port number for connection");
    return LogSock::LS_ERROR_NO_SUCH_HOST;
  }
  port = ntohs(ats_ip_port_cast(ip));

  ip_port_text_buffer ipstr;
  Debug("log-sock", "connecting to [%s:%d]", ats_ip_nptop(ip, ipstr, sizeof(ipstr)), port);

  // get an index into the connection table
  cid = new_cid();
  if (cid < 0) {
    Note("No more connections allowed for this socket");
    return LogSock::LS_ERROR_CONNECT_TABLE_FULL;
  }
  // initialize a new socket descriptor
  connect_sd = ::socket(ip->sa_family, LS_SOCKTYPE, LS_PROTOCOL);
  if (connect_sd < 0) {
    Note("Error initializing socket for connection: %d", static_cast<int>(connect_sd));
    return LogSock::LS_ERROR_SOCKET;
  }

  if ((ret = safe_setsockopt(connect_sd, IPPROTO_TCP, TCP_NODELAY, SOCKOPT_ON, sizeof(int))) < 0) {
    Note("Could not set option TCP_NODELAY on socket (%d): %s", ret, strerror(errno));
    return -1;
  }

  if ((ret = safe_setsockopt(connect_sd, SOL_SOCKET, SO_KEEPALIVE, SOCKOPT_ON, sizeof(int))) < 0) {
    Note("Could not set option SO_KEEPALIVE on socket (%d): %s", ret, strerror(errno));
    return -1;
  }

  // attempt to connect
  if (::connect(connect_sd, ip, ats_ip_size(ip)) != 0) {
    Note("Failure to connect");
    return LogSock::LS_ERROR_CONNECT;
  }

  init_cid(cid, ipstr, port, connect_sd, LogSock::LS_STATE_OUTGOING);

  Debug("log-sock", "outgoing connection to [%s:%d] established, fd  = %d", ipstr, port, cid);

  connect_sd.release();
  return cid;
}

/**
  LogSock::pending_data

  This private routine checks for incoming data on some of the socket
  descriptors.
  @return Returns true if there is something incoming, with *cid
  set to the index corresponding to the incoming socket.
*/
bool
LogSock::pending_data(int *cid, int timeout_msec, bool include_connects)
{
  int start_index, ret, n_poll_fds, i;
  static struct pollfd fds[LS_CONST_CLUSTER_MAX_MACHINES];
  int fd_to_cid[LS_CONST_CLUSTER_MAX_MACHINES];

  ink_assert(m_max_connections <= (LS_CONST_CLUSTER_MAX_MACHINES + 1));
  ink_assert(cid != NULL);
  ink_assert(timeout_msec >= 0);

  //
  // we'll use the poll() routine, which replaces the select routine
  // to support a larger number of socket descriptors.  to use poll,
  // we need to set-up a pollfd array for the socket descriptors
  // that will be polled.
  //

  if (*cid >= 0) { // look for data on this specific socket

    ink_assert(*cid < m_max_connections);
    fds[0].fd      = ct[*cid].sd;
    fds[0].events  = POLLIN;
    fds[0].revents = 0;
    fd_to_cid[0]   = *cid;
    n_poll_fds     = 1;

  } else { // look for data on any INCOMING socket

    if (include_connects) {
      start_index = 0;
    } else {
      start_index = 1;
    }
    n_poll_fds = 0;
    for (i = start_index; i < m_max_connections; i++) {
      if (ct[i].state == LogSock::LS_STATE_INCOMING) {
        fds[n_poll_fds].fd      = ct[i].sd;
        fds[n_poll_fds].events  = POLLIN;
        fds[n_poll_fds].revents = 0;
        fd_to_cid[n_poll_fds]   = i;
        n_poll_fds++;
      }
    }
  }

  if (n_poll_fds == 0) {
    return false;
  }

  ret = ::poll(fds, n_poll_fds, timeout_msec);

  if (ret == 0) {
    return false; // timeout
  } else if (ret < 0) {
    Debug("log-sock", "error on poll");
    return false; // error
  }
  //
  // a positive return value indicates how many descriptors had something
  // waiting on them.  We only care about finding one of them, so we'll
  // look for the first one with an revents flag set to POLLIN.
  //

  for (i = 0; i < n_poll_fds; i++) {
    if (fds[i].revents & POLLIN) {
      *cid = fd_to_cid[i];
      Debug("log-sock", "poll successful on index %d", *cid);
      return true;
    }
  }

  Debug("log-sock", "invalid revents in the poll table");
  return false;
}

/**
  LogSock::pending_any

  Check for incomming data on any of the INCOMING sockets.
*/
bool
LogSock::pending_any(int *cid, int timeout_msec)
{
  ink_assert(cid != NULL);
  *cid = -1;
  if (m_accept_connections) {
    return pending_data(cid, timeout_msec, true);
  } else {
    return pending_data(cid, timeout_msec, false);
  }
}

/*-------------------------------------------------------------------------
  LogSock::pending_message_any

  Check for an incomming message on any of the INCOMING sockets, aside from
  the socket reserved for accepting new connections.
  -------------------------------------------------------------------------*/

bool
LogSock::pending_message_any(int *cid, int timeout_msec)
{
  ink_assert(cid != NULL);
  *cid = -1;
  return pending_data(cid, timeout_msec, false);
}

/**
  LogSock::pending_message_on

  Check for incomming data on the specified socket.
*/
bool
LogSock::pending_message_on(int cid, int timeout_msec)
{
  return pending_data(&cid, timeout_msec, false);
}

/**
  LogSock::pending_connect

  Check for an incoming connection request on the socket reserved for that
  (cid = 0).
*/
bool
LogSock::pending_connect(int timeout_msec)
{
  int cid = 0;
  if (m_accept_connections) {
    return pending_data(&cid, timeout_msec, true);
  } else {
    return false;
  }
}

/**
  LogSock::close

  Close one (cid specified) or all (no argument) sockets, except for the
  incomming connection socket.
*/
void
LogSock::close(int cid)
{
  ink_assert(cid >= 0 && cid < m_max_connections);

  Debug("log-sock", "closing connection for cid %d", cid);

  if (ct[cid].state != LogSock::LS_STATE_UNUSED) {
    ::close(ct[cid].sd);
    delete ct[cid].host;
    ct[cid].state = LogSock::LS_STATE_UNUSED;
  }
}

void
LogSock::close()
{
  for (int i = 1; i < m_max_connections; i++) {
    this->close(i);
  }
}

/**
  LogSock::write

  Write data onto the socket corresponding to the given cid.  Return the
  number of bytes actually written.
*/
int
LogSock::write(int cid, void *buf, int bytes)
{
  LogSock::MsgHeader header = {0};
  header.msg_bytes          = 0;
  int ret;

  ink_assert(cid >= 0 && cid < m_max_connections);

  if (buf == NULL || bytes == 0) {
    return 0;
  }

  if (ct[cid].state != LogSock::LS_STATE_OUTGOING) {
    return LogSock::LS_ERROR_STATE;
  }

  Debug("log-sock", "Sending %d bytes to cid %d", bytes, cid);

  //
  // send the message header
  //
  Debug("log-sock", "   sending header (%zu bytes)", sizeof(LogSock::MsgHeader));
  header.msg_bytes = bytes;
  ret              = ::send(ct[cid].sd, (char *)&header, sizeof(LogSock::MsgHeader), 0);
  if (ret != sizeof(LogSock::MsgHeader)) {
    return LogSock::LS_ERROR_WRITE;
  }
  //
  // send the actual data
  //
  Debug("log-sock", "   sending data (%d bytes)", bytes);
  return ::send(ct[cid].sd, (char *)buf, bytes, 0);
}

/**
  LogSock::read

  Read data from the specified connection.  This is a blocking call, so you
  may want to use one of the pending_XXX calls to see if there is anything
  to read first.  Returns number of bytes read.
*/
int
LogSock::read(int cid, void *buf, unsigned maxsize)
{
  LogSock::MsgHeader header;
  unsigned size;

  ink_assert(cid >= 0 && cid < m_max_connections);
  ink_assert(buf != NULL);

  if (ct[cid].state != LogSock::LS_STATE_INCOMING) {
    return LogSock::LS_ERROR_STATE;
  }

  Debug("log-sock", "reading data from cid %d", cid);

  if (read_header(ct[cid].sd, &header) < 0) {
    return LogSock::LS_ERROR_READ;
  }

  size = ((unsigned)header.msg_bytes < maxsize) ? (unsigned)header.msg_bytes : maxsize;
  return read_body(ct[cid].sd, buf, size);
}

/**
  LogSock::read_alloc

  This routine reads data from the specified connection, and returns a
  pointer to newly allocated space (allocated with new) containing the
  data.  The number of bytes read is set in the argument size, which is
  expected to be a pointer to an int.
*/
void *
LogSock::read_alloc(int cid, int *size)
{
  LogSock::MsgHeader header;
  char *data;

  ink_assert(cid >= 0 && cid < m_max_connections);

  if (ct[cid].state != LogSock::LS_STATE_INCOMING) {
    return NULL;
  }

  Debug("log-sock", "reading data from cid %d", cid);

  if (read_header(ct[cid].sd, &header) < 0) {
    return NULL;
  }

  data = new char[header.msg_bytes];
  ink_assert(data != NULL);

  if ((*size = read_body(ct[cid].sd, data, header.msg_bytes)) < 0) {
    delete[] data;
    data = NULL;
  }

  return data;
}

/**
*/
bool
LogSock::is_connected(int cid, bool ping) const
{
  int i, j, flags;

  ink_assert(cid >= 0 && cid < m_max_connections);

  if (ct[cid].state == LogSock::LS_STATE_UNUSED) {
    return false;
  }

  if (ping) {
    flags = fcntl(ct[cid].sd, F_GETFL);
    ::fcntl(ct[cid].sd, F_SETFL, O_NONBLOCK);
    j = ::recv(ct[cid].sd, (char *)&i, sizeof(int), MSG_PEEK);
    ::fcntl(ct[cid].sd, F_SETFL, flags);
    if (j != 0) {
      return true;
    } else {
      return false;
    }
  } else {
    return (ct[cid].sd >= 0);
  }
}

/**
*/
void
LogSock::check_connections()
{
  for (int i = 1; i < m_max_connections; i++) {
    if (ct[i].state == LogSock::LS_STATE_INCOMING) {
      if (!is_connected(i, true)) {
        Debug("log-sock", "Connection %d is no longer connected", i);
        close(i);
      }
    }
  }
}

/**
  This routine will check to ensure that the client connecting is
  authorized to use the log collation port.  To authorize, the client is
  expected to send the logging secret string.
*/
bool
LogSock::authorized_client(int cid, char *key)
{
  //
  // Wait for up to 5 seconds for the client to authenticate
  //
  if (!pending_message_on(cid, 5000)) {
    return false;
  }
  //
  // Ok, the client has a pending message, so check to see if it matches
  // the given key.
  //
  char buf[1024];
  int size = this->read(cid, buf, 1024);
  ink_assert(size >= 0 && size <= 1024);

  if (strncmp(buf, key, size) == 0) {
    return true;
  }

  return false;
}

/**
*/
char *
LogSock::connected_host(int cid)
{
  ink_assert(cid >= 0 && cid < m_max_connections);
  return ct[cid].host;
}

/**
*/
int
LogSock::connected_port(int cid)
{
  ink_assert(cid >= 0 && cid < m_max_connections);
  return ct[cid].port;
}

/*-------------------------------------------------------------------------
  LOCAL ROUTINES
  -------------------------------------------------------------------------*/

/**
  LogSock::new_cid
*/
int
LogSock::new_cid()
{
  int cid = -1;

  for (int i = 1; i < m_max_connections; i++) {
    if (ct[i].state == LogSock::LS_STATE_UNUSED) {
      cid = i;
      break;
    }
  }

  return cid;
}

/**
  LogSock::init_cid
*/
void
LogSock::init_cid(int cid, char *host, int port, int sd, LogSock::State state)
{
  ink_assert(cid >= 0 && cid < m_max_connections);
  // host can be NULL if it's not known
  ink_assert(port >= 0);
  // sd can be -1 to indicate no connection yet
  ink_assert(state >= 0 && state < LogSock::LS_N_STATES);

  if (host != NULL) {
    const size_t host_size = strlen(host) + 1;
    ct[cid].host           = new char[host_size];
    ink_strlcpy(ct[cid].host, host, host_size);
  } else {
    ct[cid].host = NULL;
  }

  ct[cid].port  = port;
  ct[cid].sd    = sd;
  ct[cid].state = state;
}

/**
*/
int
LogSock::read_header(int sd, LogSock::MsgHeader *header)
{
  ink_assert(sd >= 0);
  ink_assert(header != NULL);

  int bytes = ::recv(sd, (char *)header, sizeof(LogSock::MsgHeader), 0);
  if (bytes != sizeof(LogSock::MsgHeader)) {
    return -1;
  }

  return bytes;
}

/**
*/
int
LogSock::read_body(int sd, void *buf, int bytes)
{
  ink_assert(sd >= 0);
  ink_assert(buf != NULL);
  ink_assert(bytes >= 0);

  if (bytes == 0) {
    return 0;
  }

  unsigned bytes_left = bytes;
  unsigned bytes_read;
  char *to = (char *)buf;

  while (bytes_left) {
    bytes_read = ::recv(sd, to, bytes_left, 0);
    to += bytes_read;
    bytes_left -= bytes_read;
  }

  return bytes;
}
