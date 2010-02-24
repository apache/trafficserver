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

  Protected Queue

  
 ****************************************************************************/
#ifndef _P_ProtectedQueue_h_
#define _P_ProtectedQueue_h_

#include "I_EventSystem.h"


inline
ProtectedQueue::ProtectedQueue():write_pipe_fd(-1),read_pipe_fd(-1)
#if defined(USE_OLD_EVENTFD)
:write_pipe_fd(-1),read_pipe_fd(-1)
#endif
{
  Event e;
  ink_mutex_init(&lock, "ProtectedQueue");
  ink_atomiclist_init(&al, "ProtectedQueue", (char *) &e.link.next - (char *) &e);
  ink_cond_init(&might_have_data);
}

inline void
ProtectedQueue::signal()
{
  // Need to get the lock before you can signal the thread
#if defined(USE_OLD_EVENTFD)
  if(write_pipe_fd!=-1) {
    int retVal = socketManager.write(write_pipe_fd,(void*)"W",1);
    if(retVal <= 0) {
      int fd = write_pipe_fd;
      socketManager.close(fd);
    }
  } else {
#endif
    ink_mutex_acquire(&lock);
    ink_cond_signal(&might_have_data);
    ink_mutex_release(&lock);
#if defined(USE_OLD_EVENTFD)
  }
#endif
}

inline int
ProtectedQueue::try_signal()
{
  // Need to get the lock before you can signal the thread
#if defined(USE_OLD_EVENTFD)
  if(write_pipe_fd!=-1) {
    int retVal = socketManager.write(write_pipe_fd,(void*)"W",1);
    if(retVal <= 0) {
      int fd = write_pipe_fd;
      write_pipe_fd = read_pipe_fd = -1;
      socketManager.close(fd);
    }
    return 1;
  } else {
#endif
    if (ink_mutex_try_acquire(&lock)) {
      ink_cond_signal(&might_have_data);
      ink_mutex_release(&lock);
      return 1;
    } else {
      return 0;
    }
#if defined(USE_OLD_EVENTFD)
  }
#endif
}

// Called from the same thread (don't need to signal)
inline void
ProtectedQueue::enqueue_local(Event * e)
{
  ink_assert(!e->in_the_prot_queue && !e->in_the_priority_queue);
  e->in_the_prot_queue = 1;
  localQueue.enqueue(e);
}

inline void
ProtectedQueue::remove(Event * e)
{
  ink_assert(e->in_the_prot_queue);
  if (!ink_atomiclist_remove(&al, e))
    localQueue.remove(e);
  e->in_the_prot_queue = 0;
}

inline Event *
ProtectedQueue::dequeue_local()
{
  Event *e = localQueue.dequeue();
  if (e) {
    ink_assert(e->in_the_prot_queue);
    e->in_the_prot_queue = 0;
  }
  return e;
}

#if defined(USE_OLD_EVENTFD)
INK_INLINE void 
ProtectedQueue::setReadFd(int fd)
{
  read_pipe_fd = fd;
}

INK_INLINE void 
ProtectedQueue::setWriteFd(int fd)
{
  write_pipe_fd = fd;
}

INK_INLINE int 
ProtectedQueue::getReadFd()
{
  int pfd[2] = {-1,-1};
  ink_create_pipe(pfd);
  setReadFd(pfd[0]);
  setWriteFd(pfd[1]);
  return pfd[0];
}
#endif /* USE_OLD_EVENTFD */

#endif
