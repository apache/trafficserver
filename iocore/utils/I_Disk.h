/** @file

  This file implements an I/O Processor for disk I/O

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

  @section details Details

  Part of the utils library which contains classes that use multiple
  components of the IO-Core to implement some useful functionality. The
  classes also serve as good examples of how to use the IO-Core.

  - Library: libinkutils.a

 */

#ifndef _I_Disk_h_
#define _I_Disk_h_

#ifndef INK_INLINE
#define INK_INLINE
#endif

#include "I_EventSystem.h"
//
// Events
//

#define DISK_EVENT_OPEN                 (DISK_EVENT_EVENTS_START+0)
#define DISK_EVENT_OPEN_FAILED          (DISK_EVENT_EVENTS_START+1)
#define DISK_EVENT_READ_COMPLETE        VC_EVENT_READ_COMPLETE
#define DISK_EVENT_WRITE_COMPLETE       VC_EVENT_WRITE_COMPLETE
#define DISK_EVENT_CLOSE_COMPLETE       (DISK_EVENT_EVENTS_START+4)
#define DISK_EVENT_STAT_COMPLETE        (DISK_EVENT_EVENTS_START+5)
#define DISK_EVENT_SEEK_COMPLETE        (DISK_EVENT_EVENTS_START+6)

#define DISK_OPEN_TYPE_PATH              1
#define DISK_OPEN_TYPE_FD                2


struct DiskVConnection:VConnection
{
  //
  // Public Interface
  //

  // Initiate an IO operation.
  // Only one operation may be active at one time.
  // "data" is whence for SEEK, offset for PREAD, PWRITE, otherwise unused
  //   THREAD-SAFE, may be called when not handling an event from 
  //                the DiskVConnection, or the DiskVConnection 
  //                creation callback.
  // must NOT be called when another operation is active.
  //
  virtual VIO *do_io(int op, Continuation * c = NULL, int nbytes = INT_MAX, MIOBuffer * buf = 0, int data = 0);

  virtual VIO *do_io_write(Continuation * c = NULL, int nbytes = INT_MAX, IOBufferReader * buf = 0, bool owner = false) {
    return NULL;
  }
  //
  // Private
  //

  DiskVConnection();

  void reenable(VIO * vio);

  int startEvent(int event, Event * data);
  int closeEvent(int event, Event * e);

  void free();

  Action action_;
  Action *action()
  {
    return &action_;
  }

  int req_open_type;            // DISK_OPEN_TYPE_{PATH,FD}
  char *req_path;               // requested pathname to open 
  int req_fd;                   // requested file descriptor to open 
  int req_oflag;                // requested open flags
  mode_t req_mode;              // requested file permissions mode

  Connection con;
  volatile int enabled;
  int priority;
  VIO vio;
  int closed;
  int lerrno;

  ink_hrtime submit_time;

  Link<DiskVConnection> disk_link;
};

//
// DiskHandler
// 
// A DiskHandler handles the Disk IO operations.  It maintains
// lists of operations at multiples of it''s periodicity.
//
struct DiskHandler:Continuation
{
  //
  // Private
  //
  SLL<DiskVConnection> *vcs;
  int cur_vcs;

  int mainEvent(int event, Event * data);

    DiskHandler();
};

extern ClassAllocator<DiskVConnection> diskVCAllocator;

struct DiskProcessor:Processor
{
  //
  // Public Interface
  //

  // Open a DiskVConnection given a filename
  // calls: cont->handleEvent( DISK_EVENT_OPEN, DiskVConnection *) on success
  //        cont->handleEvent( DISK_EVENT_OPEN_FAILED, 0) on failure
  //
  Action *open_vc(Continuation * cont, char *path, int oflag, mode_t mode = 0666);

  // Open a DiskVConnection given a file descriptor
  // calls: cont->handleEvent( DISK_EVENT_OPEN, DiskVConnection *) on success
  //        cont->handleEvent( DISK_EVENT_OPEN_FAILED, 0) on failure
  //
  Action *open_vc(Continuation * cont, int fd);

  // Open a file descriptor given a filename
  // calls: cont->handleEvent( DISK_EVENT_OPEN, fd) on success
  //        cont->handleEvent( DISK_EVENT_OPEN_FAILED, -errno,) on failure
  //
  Action *open_fd(Continuation * cont, char *path, int oflag, mode_t mode = 0666);

  // Other operations
  // cont->handleEvent(DISK_EVENT_XXXX_COMPLETE,0) on success 
  //                  (-errno,0) on failure
  //
  Action *close(Continuation * cont, int fildes);
  Action *read(Continuation * cont, int fildes, void *buf, int nbyte = INT_MAX);
  Action *write(Continuation * cont, int fildes, void *buf, int nbyte = INT_MAX);
  Action *lseek(Continuation * cont, int fildes, off_t offset, int whence);
  Action *pread(Continuation * cont, int fildes, void *buf, int nbytes = INT_MAX, off_t offset = 0);
  Action *pwrite(Continuation * cont, int fildes, void *buf, int nbytes = INT_MAX, off_t offset = 0);
  Action *fstat(Continuation * cont, int fildes, struct stat *abuf);

  static unsigned int pagesize;

    DiskProcessor();
};

//
// Set up a thread to receive events from the DiskProcessor
// This function should be called for all threads created to
// accept such events by the EventProcesor.
//
void initialize_thread_for_disk(EThread * thread);

extern DiskProcessor diskProcessor;

//
//  Because the OS can "page-flip" (reuse IO buffer pages for
//  the user process) if the user process buffers are page aligned with
//  the disk buffers, it can be beneficial to try to force such an
//  alignment.  This function attempts to do just that while ensuring
//  that we are still making progress and reading enough data to
//  fulfill any watermark requirements (see Memo.IOBuffers) set by
//  the user.
//
//  Try to force alignment.
//  delta a positive delta to move the buffer *backward* 
//  (pagesize - delta) is the delta to move the buffer *forward*
//
inline void
page_align_start(MIOBuffer * mbuf, int delta, int pagesize)
{

  // Check if there is too much stuff to move
  //
  if (mbuf->size() >= pagesize)
    return;
  int consumable = 0;
  char *start = mbuf->start();
  char *astart = start - delta;

  //  Start is not page aligned
  //
  if (start != astart) {

    // Try to move back
    //
    if (start - consumable <= astart)
      mbuf->move_start(astart);

    // Try to move forward
    //
    else if (mbuf->free() > ((astart + pagesize) - start) + mbuf->water_mark)
      mbuf->move_start(astart + pagesize);
  }
}

#endif
