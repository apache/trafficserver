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

#ifndef _MGMT_SOCKET_H_
#define _MGMT_SOCKET_H_

#include "ts/ink_platform.h"
#include "ts/ink_apidefs.h"

#include <vector>
#include <unordered_set>

/**
 *  An epoll instance to monitor socket fds. epoll_wait can be woken up with
 *   a poke(). This prevents situations where both the client and server are
 *   both trying to read messages and unable to write messages themselves. 
 *   SocketPoller allows for active wakeup rather than a passive timeout. 
 * 
 *  SocketPoller only maintains fds for wakeupfd and epoll_fd. Any fd registered
 *   should be removed or you will continue to recieve events for it. Calling 
 *   cleanup is important because it will unregister any fds that the
 *   caller forgot to remove.
 *
 *  If system doesn't have eventfd, poke does nothing and timeouts are relied on.
 */
class SocketPoller
{
    typedef struct epoll_event PollEvent;

public:
    SocketPoller(int fds = 10);
    ~SocketPoller();

    /**
     * readSocketTimeout. 
     * 
     * polls fd registered to epoll instance 
     *
     * input: timeout in ms
     * output: returns 0 if there is a timeout or an external trigger. basically, stop polling 
     *            socket, go do something else.
     *         returns < 0 if error. check system errno
     *         returns > 0 for the number of ready fds. This should be used with getReadyFileDescriptors() 
     *            and getReadyFileDescriptorAt() to avoid going out of bounds.
     */ 
    int readSocketTimeout(unsigned int timeout_ms);

    /**
     * getReadyFileDescriptorAt.
     * readSocketTimeout returns the number of ready fds. In order to see which fd was triggered 
     *
     * input: index: event index, num_ready: total number of ready events
     * output: return > 0 for the fd at index. if index correct, should not error here. 
     */
    int getReadyFileDescriptorAt(int index, int num_ready) const;

    /**
     * getReadyFileDescriptors. Get all the fds that are ready. 
     */
    std::vector<int> getReadyFileDescriptors(int num_ready) const;

    /**
     * poke.
     *
     * To avoid relying on timeouts when both sides are polling for messages, we can poke the epoll_wait 
     *    to force it to stop polling and to go do something else. An external event, such as adding to 
     *    the write queue, should poke to get things moving quicker.
     *
     * poke() maintains an internal mutex and is thread safe.
     */
    void poke();

    /**
     * cleanup.
     *
     * purpose: deallocates memory, closes internally managed fds and unregisters all fds
     */
    void cleanup();

    /**
     * registerFileDescriptor. adds a fd to the epoll_fd to watch. 
     *
     * input: fd to add
     * output: return 0 if successful or already registered. 
     *         return < 0 if there is an error. check errno. 
     *             Note: some places use ret = -1 and errno == EEXIST to check if fd already registered. In SocketPoller,
     *             fds are internally managed so this should never happen. if return < 0, there is a epoll_ctl error. 
     */ 
    int registerFileDescriptor(int fd);

    /**
     * removeFileDescriptor. removes fd. does not close file descriptor.
     */
    int removeFileDescriptor(int fd);

    /**
     * purgeDescriptors. remove all fds, including the wakeup_event.
     */
    void purgeDescriptors();

    /** 
     * isRegistered. checks if fd is being monitored. 
     */
    bool isRegistered(int fd) const;

    /**
     * wakeupDescriptor. get the fd for the wakeup_event.
     */
    int wakeupDescriptor() const;

private:

    int epfd;             // epoll_fd

    PollEvent* events;    // internal buffer for ready fd events
    std::unordered_set<int> registered_fds; // track all fds. helps to prevent the case where the caller forgets to remove fds.

    bool setup;
#if HAVE_EVENTFD
    int wakeup_event;     // eventfd used to wakeup epoll_wait
#endif

};


//-------------------------------------------------------------------------
// system calls (based on implementation from UnixSocketManager);
//-------------------------------------------------------------------------

//-------------------------------------------------------------------------
// transient_error
//-------------------------------------------------------------------------

bool mgmt_transient_error();

//-------------------------------------------------------------------------
// mgmt_accept
//-------------------------------------------------------------------------

int mgmt_accept(int s, struct sockaddr *addr, socklen_t *addrlen);

//-------------------------------------------------------------------------
// mgmt_fopen
//-------------------------------------------------------------------------

FILE *mgmt_fopen(const char *filename, const char *mode);

//-------------------------------------------------------------------------
// mgmt_open
//-------------------------------------------------------------------------

int mgmt_open(const char *path, int oflag);

//-------------------------------------------------------------------------
// mgmt_open_mode
//-------------------------------------------------------------------------

int mgmt_open_mode(const char *path, int oflag, mode_t mode);

//-------------------------------------------------------------------------
// mgmt_open_mode_elevate
//-------------------------------------------------------------------------

int mgmt_open_mode_elevate(const char *path, int oflag, mode_t mode, bool elevate_p = false);

//-------------------------------------------------------------------------
// mgmt_select
//-------------------------------------------------------------------------

int mgmt_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *errorfds, struct timeval *timeout);

//-------------------------------------------------------------------------
// mgmt_sendto
//-------------------------------------------------------------------------

int mgmt_sendto(int fd, void *buf, int len, int flags, struct sockaddr *to, int tolen);

//-------------------------------------------------------------------------
// mgmt_socket
//-------------------------------------------------------------------------

int mgmt_socket(int domain, int type, int protocol);

//-------------------------------------------------------------------------
// mgmt_write_timeout
//-------------------------------------------------------------------------
int mgmt_write_timeout(int fd, int sec, int usec);

//-------------------------------------------------------------------------
// mgmt_read_timeout
//-------------------------------------------------------------------------
int mgmt_read_timeout(int fd, int sec, int usec);

// Do we support passing Unix domain credentials on this platform?
bool mgmt_has_peereid(void);

// Get the Unix domain peer credentials.
int mgmt_get_peereid(int fd, uid_t *euid, gid_t *egid);

#endif // _MGMT_SOCKET_H_
