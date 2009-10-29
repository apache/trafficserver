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

  Disk.cc

  This file implements an I/O Processor for disk I/O.

  
 ****************************************************************************/

#include "I_Disk.h"


//
// Configuration Parameters
//   Design notes are in Memo.DiskDesign
//

#define DISK_BUCKETS            100
#define DISK_PERIOD             HRTIME_MSECONDS(10)
#define DISK_INITIAL_PRIORITY   1


//
// DiskProcessor
//

DiskProcessor diskProcessor;

//
// Handlings for one shot operations
//

ClassAllocator<DiskVConnection> diskVCAllocator("diskVCAllocator");

//
// Continuation to handle one shot operations
//
struct DiskContinuation:Continuation
{
  char *path;
  int oflag;
  int filedes;
  mode_t mode;
  void *buf;
  int nbytes;
  int data;
  int offset;
  ink_hrtime submit_time;
  Action action_;

  int open_fdEvent(int event, Event * data);
  int closeEvent(int event, Event * data);
  int readEvent(int event, Event * data);
  int writeEvent(int event, Event * data);
  int lseekEvent(int event, Event * data);
  int preadEvent(int event, Event * data);
  int pwriteEvent(int event, Event * data);
  int fstatEvent(int event, Event * data);

  void free();

    DiskContinuation():Continuation(NULL), path(0), oflag(0), filedes(0), mode(0), buf(0), nbytes(0), data(0), offset(0)
  {
  }
};

typedef int (DiskContinuation::*DiskContHandler) (int, void *);
typedef int (DiskVConnection::*DiskVConnHandler) (int, void *);
typedef int (DiskHandler::*DiskCHandler) (int, void *);


static ClassAllocator<DiskContinuation> diskContAllocator("diskContAllocator");


//
// Initialization
//

DiskProcessor::DiskProcessor()
{
}

unsigned int
  DiskProcessor::pagesize = getpagesize();


void
initialize_thread_for_disk(EThread * thread)
{
  thread->diskHandler = NEW(new DiskHandler);
  thread->schedule_every(thread->diskHandler, DISK_PERIOD);
}


//
// See Net.cc for a description of the manipulation of priority
//
// Essentially, the DiskVConnection is moved into a bucket which
// depends on the "priority".  Like unix nice(1) values, high
// priority is a *low* value.
//
static void
set_priority(DiskHandler * dh, DiskVConnection * vc, int new_priority)
{
  if (new_priority >= DISK_BUCKETS)
    vc->priority = DISK_BUCKETS - 1;
  else if (new_priority > 0)
    vc->priority = new_priority;
  else
    vc->priority = 1;

  int new_bucket = dh->cur_vcs + vc->priority;
  new_bucket %= DISK_BUCKETS;

  dh->vcs[new_bucket].push(vc, vc->disk_link);
}

static void
bump(DiskHandler * dh, DiskVConnection * vc)
{
  int new_bucket = (dh->cur_vcs + 1) % DISK_BUCKETS;
  dh->vcs[new_bucket].push(vc, vc->disk_link);
}

static inline void
lower_priority(DiskHandler * dh, DiskVConnection * vc)
{
  int offset = vc->priority / 4;
  if (!offset)
    offset = 1;
  set_priority(dh, vc, vc->priority + offset);
}

static inline void
raise_priority(DiskHandler * dh, DiskVConnection * vc)
{
  int offset = vc->priority / 2;
  if (!offset)
    offset = 1;
  set_priority(dh, vc, vc->priority - offset);
}

static inline void
disable(DiskHandler * dh, DiskVConnection * vc)
{
  vc->enabled = 0;
  raise_priority(dh, vc);
}

static inline void
reschedule(DiskHandler * dh, DiskVConnection * vc)
{
  set_priority(dh, vc, vc->priority);
}

static void
update_priority(DiskHandler * dh, DiskVConnection * vc, int ndone, int nbytes)
{
  if (!vc->enabled) {
    disable(dh, vc);
    return;
  }
  int tsize = vc->vio.buffer.size();
  if (tsize > nbytes)
    tsize = nbytes;
  if (ndone > tsize / 2)
    raise_priority(dh, vc);
  else {
    if (ndone < tsize / 4)
      lower_priority(dh, vc);
    else
      reschedule(dh, vc);
  }
}

//
// Open DiskVConnection from pathname
// Allocate a new DiskVConnection and enqueue it for execution on
// a thread which supports disk activity.
//
Action *
DiskProcessor::open_vc(Continuation * acont, char *apath, int aoflag, mode_t amode)
{
  DiskVConnection *vc = diskVCAllocator.alloc();
  vc->submit_time = ink_get_hrtime();
  vc->action_ = acont;
  vc->mutex = acont->mutex;
  vc->req_open_type = 1;        // open a pathname
  vc->req_path = apath;
  vc->req_oflag = aoflag;
  vc->req_mode = amode;
  SET_CONTINUATION_HANDLER(vc, (DiskVConnHandler) & DiskVConnection::startEvent);
  eventProcessor.schedule_imm(vc, ET_DISK);
  return &vc->action_;
}

//
// Open DiskVConnection from file descriptor
// Allocate a new DiskVConnection and enqueue it for execution on
// a thread which supports disk activity.
//
Action *
DiskProcessor::open_vc(Continuation * acont, int afd)
{
  DiskVConnection *vc = diskVCAllocator.alloc();
  vc->submit_time = ink_get_hrtime();
  vc->action_ = acont;
  vc->mutex = acont->mutex;
  vc->req_open_type = 2;        // open a file descriptor
  vc->req_fd = afd;
  SET_CONTINUATION_HANDLER(vc, (DiskVConnHandler) & DiskVConnection::startEvent);
  eventProcessor.schedule_imm(vc, ET_DISK);
  return &vc->action_;
}

//
// Open file one shot operation
// Processor enqueues an event with a continuation which does the operation
//
Action *
DiskProcessor::open_fd(Continuation * acont, char *apath, int aoflag, mode_t amode)
{
  DiskContinuation *c = diskContAllocator.alloc();
  c->submit_time = ink_get_hrtime();
  c->action_ = acont;
  c->mutex = acont->mutex;
  c->path = apath;
  c->oflag = aoflag;
  c->mode = amode;
  SET_CONTINUATION_HANDLER(c, (DiskContHandler) & DiskContinuation::open_fdEvent);
  eventProcessor.schedule_imm(c, ET_DISK);
  return &c->action_;
}

int
DiskContinuation::open_fdEvent(int event, Event * e)
{
  (void) event;
  (void) e;
  filedes = socketManager.open(path, oflag, mode);
  if (filedes >= 0) {
    Continuation *temp_cont = action_.continuation;
    action_.continuation = NULL;
    // The calling continuation, after it gets
    //DISK_EVENT_OPEN, may choose to set diskvconnection->cont to some
    //continuation. At the time of close, diskVConnection will check
    //if cont is null. If it is not, then it will call back the
    //continuation so that it can do cleanup
    temp_cont->handleEvent(DISK_EVENT_OPEN, (void *) filedes);
    if (action_.continuation)
      action_.mutex = action_.continuation->mutex;
  } else
    action_.continuation->handleEvent(DISK_EVENT_OPEN_FAILED, (void *) filedes);
  free();
  return EVENT_DONE;
}

//
// Close file one shot operation
// Processor enqueues an event with a continuation which does the operation
//
Action *
DiskProcessor::close(Continuation * acont, int afiledes)
{
  DiskContinuation *c = diskContAllocator.alloc();
  c->submit_time = ink_get_hrtime();
  c->action_ = acont;
  c->mutex = acont->mutex;
  c->filedes = afiledes;
  SET_CONTINUATION_HANDLER(c, (DiskContHandler) & DiskContinuation::closeEvent);
  eventProcessor.schedule_imm(c, ET_DISK);
  return &c->action_;
}

int
DiskContinuation::closeEvent(int event, Event * e)
{
  (void) event;
  (void) e;
  int res = socketManager.close(filedes, keFile);
  action_.continuation->handleEvent(DISK_EVENT_CLOSE_COMPLETE, (void *) res);
  free();
  return EVENT_DONE;
}

//
// Read from file one shot operation
// Processor enqueues an event with a continuation which does the operation
//
Action *
DiskProcessor::read(Continuation * acont, int afiledes, void *abuf, int anbytes)
{
  DiskContinuation *c = diskContAllocator.alloc();
  c->submit_time = ink_get_hrtime();
  c->action_ = acont;
  c->mutex = acont->mutex;
  c->filedes = afiledes;
  c->buf = abuf;
  c->nbytes = anbytes;
  SET_CONTINUATION_HANDLER(c, (DiskContHandler) & DiskContinuation::readEvent);
  eventProcessor.schedule_imm(c, ET_DISK);
  return &c->action_;
}

int
DiskContinuation::readEvent(int event, Event * e)
{
  (void) event;
  (void) e;
  int res = socketManager.read(filedes, buf, nbytes);
  action_.continuation->handleEvent(DISK_EVENT_READ_COMPLETE, (void *) res);
  free();
  return EVENT_DONE;
}

//
// Read from file one shot operation at a particular offset
// Processor enqueues an event with a continuation which does the operation
//
Action *
DiskProcessor::pread(Continuation * acont, int afiledes, void *abuf, int anbytes, off_t offset)
{
  DiskContinuation *c = diskContAllocator.alloc();
  c->submit_time = ink_get_hrtime();
  c->action_ = acont;
  c->mutex = acont->mutex;
  c->filedes = afiledes;
  c->buf = abuf;
  c->nbytes = anbytes;
  c->data = (int) offset;
  SET_CONTINUATION_HANDLER(c, (DiskContHandler) & DiskContinuation::preadEvent);
  eventProcessor.schedule_imm(c, ET_DISK);
  return &c->action_;
}

int
DiskContinuation::preadEvent(int event, Event * e)
{
  (void) event;
  (void) e;
  int res =
    socketManager.read_from_middle_of_file(filedes, buf, nbytes, (off_t) data, "[DiskContinuation::preadEvent]");
  action_.continuation->handleEvent(DISK_EVENT_READ_COMPLETE, (void *) res);
  free();
  return EVENT_DONE;
}

//
// Write to file one shot operation
// Processor enqueues an event with a continuation which does the operation
//
Action *
DiskProcessor::write(Continuation * acont, int afiledes, void *abuf, int anbytes)
{
  DiskContinuation *c = diskContAllocator.alloc();
  c->submit_time = ink_get_hrtime();
  c->action_ = acont;
  c->mutex = acont->mutex;
  c->filedes = afiledes;
  c->buf = abuf;
  c->nbytes = anbytes;
  SET_CONTINUATION_HANDLER(c, (DiskContHandler) & DiskContinuation::writeEvent);
  eventProcessor.schedule_imm(c, ET_DISK);
  return &c->action_;
}

int
DiskContinuation::writeEvent(int event, Event * e)
{
  (void) event;
  (void) e;
  int res = socketManager.write(filedes, buf, nbytes);
  action_.continuation->handleEvent(DISK_EVENT_WRITE_COMPLETE, (void *) res);
  free();
  return EVENT_DONE;
}

//
// Write to file one shot operation at a particular offset
// Processor enqueues an event with a continuation which does the operation
//
Action *
DiskProcessor::pwrite(Continuation * acont, int afiledes, void *abuf, int anbytes, off_t offset)
{
  DiskContinuation *c = diskContAllocator.alloc();
  c->submit_time = ink_get_hrtime();
  c->action_ = acont;
  c->mutex = acont->mutex;
  c->filedes = afiledes;
  c->buf = abuf;
  c->nbytes = anbytes;
  c->data = (int) offset;
  SET_CONTINUATION_HANDLER(c, (DiskContHandler) & DiskContinuation::pwriteEvent);
  eventProcessor.schedule_imm(c, ET_DISK);
  return &c->action_;
}

int
DiskContinuation::pwriteEvent(int event, Event * e)
{
  (void) event;
  (void) e;
  int res =
    socketManager.write_to_middle_of_file(filedes, buf, nbytes, (off_t) data, "[DiskContinuation::pwriteEvent]");
  action_.continuation->handleEvent(DISK_EVENT_WRITE_COMPLETE, (void *) res);
  free();
  return EVENT_DONE;
}

//
// Seek in file one shot operation
// Processor enqueues an event with a continuation which does the operation
//
Action *
DiskProcessor::lseek(Continuation * acont, int afiledes, off_t aoffset, int awhence)
{
  DiskContinuation *c = diskContAllocator.alloc();
  c->submit_time = ink_get_hrtime();
  c->action_ = acont;
  c->mutex = acont->mutex;
  c->filedes = afiledes;
  c->offset = aoffset;
  c->data = awhence;
  SET_CONTINUATION_HANDLER(c, (DiskContHandler) & DiskContinuation::lseekEvent);
  eventProcessor.schedule_imm(c, ET_DISK);
  return &c->action_;
}

int
DiskContinuation::lseekEvent(int event, Event * e)
{
  (void) event;
  (void) e;
  int res = socketManager.lseek(filedes, offset, data);
  action_.continuation->handleEvent(DISK_EVENT_SEEK_COMPLETE, (void *) res);
  free();
  return EVENT_DONE;
}

//
// Stat a file one shot operation
// Processor enqueues an event with a continuation which does the operation
//
Action *
DiskProcessor::fstat(Continuation * acont, int afiledes, struct stat * abuf)
{
  DiskContinuation *c = diskContAllocator.alloc();
  c->submit_time = ink_get_hrtime();
  c->action_ = acont;
  c->mutex = acont->mutex;
  c->filedes = afiledes;
  c->buf = (void *) abuf;
  SET_CONTINUATION_HANDLER(c, (DiskContHandler) & DiskContinuation::fstatEvent);
  eventProcessor.schedule_imm(c, ET_DISK);
  return &c->action_;
}

int
DiskContinuation::fstatEvent(int event, Event * e)
{
  (void) event;
  (void) e;
  int res = socketManager.fstat(filedes, (struct stat *) buf);
  action_.continuation->handleEvent(DISK_EVENT_STAT_COMPLETE, (void *) res);
  free();
  return EVENT_DONE;
}

//
// Handling for DiskVConnection operations
//

DiskVConnection::DiskVConnection()
:VConnection(NULL),
req_open_type(0), req_path(NULL), req_fd(0), req_oflag(0), req_mode(0), enabled(0), vio(VIO::NONE), closed(0)
{
  SET_HANDLER((DiskVConnHandler) & DiskVConnection::startEvent);
}

//
// do_io
// Assign operation to the VIO.
// This is thread safe only when the VIO is disabled.
//
VIO *
DiskVConnection::do_io(int op, Continuation * c, int nbytes, MIOBuffer * buf, int data)
{
  switch (op) {
  default:
    vio.buffer.clear();
    break;
  case VIO::READ:
  case VIO::PREAD:
  case VIO::STAT:
    vio.buffer.writer_for(buf);
    break;
  case VIO::WRITE:
  case VIO::PWRITE:
    vio.buffer.reader_for(buf);
    if (buf) {
      if (data)
        vio.buffer.consume(data);
    } else {
      ink_assert(!data);
    }
    break;
  case VIO::ABORT:
    // cout << "Doing an ABORT on diskvconnection\n";
    vio.buffer.clear();
    INK_WRITE_MEMORY_BARRIER;
    closed = 2;                 // 2 means aborted, 1 means closed
    enabled = 1;
    break;
  case VIO::CLOSE:
    // cout << "Doing a CLOSE on diskvconnection\n";
    vio.buffer.clear();
    INK_WRITE_MEMORY_BARRIER;
    closed = 1;
    enabled = 1;
    return NULL;
  }
  vio.op = op;
  vio.set_continuation(c);
  vio.nbytes = nbytes;
  vio.data = data;
  vio.ndone = 0;
  vio.vc_server = (VConnection *) this;
  enabled = 1;

  return &vio;
}


//
// Start up a DiskVConnection.
// Open the file descriptor.
// Call back the the creator.
// Schedule with the DiskHandler for this thread.
//
int
DiskVConnection::startEvent(int event, Event * e)
{
  (void) event;
  MUTEX_TRY_LOCK_FOR(lock, action_.mutex, e->ethread, action_.continuation);
  if (!lock) {
    e->schedule_in(DISK_PERIOD);
    return EVENT_CONT;
  }

  switch (req_open_type) {
  case DISK_OPEN_TYPE_PATH:
    con.fd = socketManager.open(req_path, req_oflag, req_mode);
    break;
  case DISK_OPEN_TYPE_FD:
    con.fd = req_fd;
    break;
  default:
    ink_assert(!"bad case");
    con.fd = -1;
    break;
  }

  if (con.fd < 0) {
    action_.continuation->handleEvent(DISK_EVENT_OPEN_FAILED, (void *) con.fd);
    diskVCAllocator.free(this);
  } else {
    Continuation *temp_cont = action_.continuation;
    action_.continuation = NULL;
    // The calling continuation, after it gets
    // DISK_EVENT_OPEN, may choose to set diskvconnection->cont to some
    // continuation. At the time of close, diskVConnection will check
    // if cont is null. If it is not, then it will call back the
    // continuation so that it can do cleanup
    ink_assert(EVENT_CONT == temp_cont->handleEvent(DISK_EVENT_OPEN, this));
    if (action_.continuation)
      action_.mutex = action_.continuation->mutex;
    set_priority(e->ethread->diskHandler, this, DISK_INITIAL_PRIORITY);
  }
  return EVENT_DONE;
}


void
DiskVConnection::reenable(VIO * avio)
{
  ink_assert(avio == &vio);
  enabled = 1;
}

DiskHandler::DiskHandler():
Continuation(NULL), vcs(NULL), cur_vcs(0)
{
  SET_HANDLER((DiskCHandler) & DiskHandler::mainEvent);
  vcs = NEW(new SLL<DiskVConnection>[DISK_BUCKETS]);
}


int
DiskVConnection::closeEvent(int event, Event * e)
{
  ink_assert(event == EVENT_INTERVAL);
  ink_assert(action_.continuation);
  MUTEX_TRY_LOCK_FOR(lock, action_.mutex, this_ethread(), action_.continuation);
  if (!lock) {
    e->schedule_in(DISK_PERIOD);
    return EVENT_CONT;
  }
  action_.continuation->handleEvent(DISK_EVENT_CLOSE_COMPLETE, this);
  con.close();
  free();
  return EVENT_DONE;
}

void
DiskVConnection::free()
{
  vio.mutex = 0;
  mutex = 0;
  action_ = 0;
  diskVCAllocator.free(this);
}

void
DiskContinuation::free()
{
  mutex = 0;
  action_ = 0;
  diskContAllocator.free(this);
}

//
// Private functions
//

static void
close_DiskVConnection(DiskVConnection * vc)
{
  if (vc->action_.continuation) {
    MUTEX_TRY_LOCK_FOR(lock, vc->action_.mutex, this_ethread(), vc->action_.continuation);
    if (!lock) {
      SET_CONTINUATION_HANDLER(vc, (DiskVConnHandler) & DiskVConnection::closeEvent);
      eventProcessor.schedule_in(vc, DISK_PERIOD, ET_DISK);
      return;
    }
    vc->action_.continuation->handleEvent(DISK_EVENT_CLOSE_COMPLETE, vc);
  }
  vc->con.close();
  vc->free();
}

static inline int
signal_and_update(int event, DiskVConnection * vc)
{
  vc->vio._cont->handleEvent(event, &vc->vio);
  if (vc->closed) {
    close_DiskVConnection(vc);
    return EVENT_DONE;
  }
  return EVENT_CONT;
}

static inline int
signal_done(int event, DiskHandler * dh, DiskVConnection * vc)
{
  vc->enabled = 0;
  if (signal_and_update(event, vc) == EVENT_DONE)
    return EVENT_DONE;
  else {
    reschedule(dh, vc);
    return EVENT_CONT;
  }
}

//
// Signal an event
//
static inline int
signal_error_and_update(DiskVConnection * vc, int lerrno)
{
  vc->lerrno = lerrno;
  return signal_and_update(VC_EVENT_ERROR, vc);
}

//
// Read from disk.  Handle all signaling to the Continuation
// associated with the operation.
//
static void
read_from_disk(DiskHandler * dh, DiskVConnection * vc)
{
  int r = 0;

  // If there is no buffer, ask the user for one.
  // if the buffer does not arrive disable this operation.
  //
  MIOBufferAccessor & buf = vc->vio.buffer;
  if (!buf) {
    if (signal_and_update(VC_EVENT_READ_READY, vc) != EVENT_CONT)
      return;
    if (!buf) {
      disable(dh, vc);
      return;
    }
  }
  // If we have a watermark, the user wants at least that much data
  //
  if (vc->vio.buffer.water_mark()) {

    // If there is high_water, delay (disks are fast)
    //
    if (vc->vio.buffer.high_water()) {
      lower_priority(dh, vc);
      return;
    }
    //
    //  If possible, force alignment
    //
    unsigned int pagesize = diskProcessor.pagesize;
    char *start = vc->vio.buffer.start();
    unsigned long i = (long) start;
    char *astart = (char *) (i - (i % pagesize));
    page_align_start(vc->vio.buffer.mbuf, start - astart, (int) pagesize);

    // fall through

  } else {

    // If there is no watermark, and no room
    //
    if (!vc->vio.buffer.free()) {

      // If there is no data, reset the start, end and add
      //
      if (!vc->vio.buffer.mbuf->size())
        vc->vio.buffer.mbuf->reset();
      else {

        // Otherwise, lower the priority
        //
        lower_priority(dh, vc);
        return;
      }
    }
  }

  //
  // This is a mirror of Net.cc
  //
  int toread = buf.free();
  int ntodo = vc->vio.ntodo();
  if (toread > ntodo)
    toread = ntodo;
  int done = ntodo == toread;

  if (!ntodo) {
    signal_done(VC_EVENT_READ_COMPLETE, dh, vc);
    return;
  }
  //
  // Round to pagesize if in progress and more than pagesize of data
  //
  if (!done && toread > (int) diskProcessor.pagesize)
    toread = toread - (int) ((unsigned int) toread % diskProcessor.pagesize);

  //
  // Again follows Net.cc
  //
  if (toread) {

    r = socketManager.read(vc->con.fd, buf.end(), toread);

    if (r <= 0) {
      if (!r) {
        vc->enabled = 0;
        signal_and_update(VC_EVENT_EOS, vc);
      } else
        signal_error_and_update(vc, -r);
      reschedule(dh, vc);
      return;
    }

    buf.fill(r);
    vc->vio.ndone += r;
  } else
    r = 0;


  if (buf.size()) {
    if (signal_and_update(VC_EVENT_READ_READY, vc) != EVENT_CONT)
      return;

    if (vc->vio.ntodo() <= 0)
      if (signal_and_update(VC_EVENT_READ_COMPLETE, vc) != EVENT_CONT)
        return;
  }

  if (!buf.free()) {
    disable(dh, vc);
    return;
  }

  update_priority(dh, vc, r, vc->vio.nbytes);
  return;
}

//
// Write to the disk.  Handle all signaling to the Continuation
// associated with the operation.
//
static void
write_to_disk(DiskHandler * dh, DiskVConnection * vc)
{
  // If there is no buffer, ask the user for one.
  // if the buffer does not arrive disable this operation.
  //
  MIOBufferAccessor & buf = vc->vio.buffer;
  if (!buf) {
    if (signal_and_update(VC_EVENT_WRITE_READY, vc) != EVENT_CONT)
      return;
    if (!buf) {
      disable(dh, vc);
      return;
    }
  }
  //
  // Calculate amount to write
  // Follows Net.cc
  //
  int towrite = buf.size();
  int ntodo = vc->vio.ntodo();
  if (towrite > ntodo)
    towrite = ntodo;

  if (ntodo <= 0) {
    signal_and_update(VC_EVENT_WRITE_COMPLETE, vc);
    reschedule(dh, vc);
    return;
  }

  if (buf.free() && towrite != ntodo) {
    if (signal_and_update(VC_EVENT_WRITE_READY, vc) != EVENT_CONT)
      return;
    towrite = buf.size();
    ntodo = vc->vio.ntodo();
    if (towrite > ntodo)
      towrite = ntodo;
    if (ntodo <= 0) {
      if (signal_and_update(VC_EVENT_WRITE_COMPLETE, vc) != EVENT_CONT)
        return;
      reschedule(dh, vc);
    }
  }

  if (!towrite) {
    disable(dh, vc);
    return;
  }

  int done = ntodo == towrite;

  //
  // Round to pagesize if in progress and more than pagesize of data
  //
  if (!done && towrite > (int) diskProcessor.pagesize)
    towrite = towrite - (int) ((unsigned int) towrite % diskProcessor.pagesize);

  //
  // Again follows Net.cc
  //
  int r = socketManager.write(vc->con.fd, buf.start(), towrite);

  if (r <= 0) {
    signal_error_and_update(vc, -r);
    reschedule(dh, vc);
    return;
  } else {
    buf.consume(r);
    vc->vio.ndone += r;
    if (vc->vio.ntodo() <= 0) {
      signal_done(VC_EVENT_WRITE_COMPLETE, dh, vc);
      return;
    }
    update_priority(dh, vc, r, vc->vio.nbytes);
    return;
  }
}

//
// The main event loop for the DiskHandler.
// Go through all DiskVConnections managed by this DiskHandler.
//
int
DiskHandler::mainEvent(int event, Event * e)
{
  (void) event;
  (void) e;

  // Move list of operations in this bucket to a temporary
  //
  SLL<DiskVConnection> sll = vcs[cur_vcs];
  vcs[cur_vcs].head = NULL;

  // For each operation (SUNCC is weak)
  //
  DiskVConnection *vc = NULL;
  while ((vc = sll.pop(sll.head, sll.head->disk_link))) {

    if (!vc->enabled) {
      lower_priority(this, vc);
      continue;
    }
#ifdef PURIFY
    MUTEX_TRY_LOCK_FOR(lock, vc->vio.mutex, e->ethread, ((Continuation *) 0));
#else
    MUTEX_TRY_LOCK_FOR(lock, vc->vio.mutex, e->ethread, vc->vio._cont);
#endif
    if (!lock || !lock.m) {
      bump(this, vc);
      continue;
    }

    if (vc->closed) {
      close_DiskVConnection(vc);
      continue;
    }

    if (!vc->enabled || vc->vio.op == VIO::NONE) {
      vc->enabled = 0;
      lower_priority(this, vc);
      continue;
    }

    switch (vc->vio.op) {
    default:
      ink_assert(!"bad case");
      break;

    case VIO::READ:
    case VIO::PREAD:
      read_from_disk(this, vc);
      break;

    case VIO::WRITE:
    case VIO::PWRITE:
      write_to_disk(this, vc);
      break;

    case VIO::SEEK:{
        int res = socketManager.lseek(vc->con.fd, vc->vio.nbytes, vc->vio.data);
        if (res < 0)
          signal_error_and_update(vc, -res);
        else {
          vc->enabled = 0;
          signal_and_update(DISK_EVENT_SEEK_COMPLETE, vc);
        }
        reschedule(this, vc);
        break;
      }

    case VIO::STAT:{
        ink_assert((unsigned int) vc->vio.buffer.free() >= sizeof(struct stat));
        int res = socketManager.fstat(vc->con.fd,
                                      (struct stat *) vc->vio.buffer.start());
        if (res < 0)
          signal_error_and_update(vc, -res);
        else {
          vc->enabled = 0;
          signal_and_update(DISK_EVENT_STAT_COMPLETE, vc);
        }
        reschedule(this, vc);
        break;
      }
    }
  }

  // skip to the next bucket
  //
  cur_vcs = (cur_vcs + 1) % DISK_BUCKETS;
  return EVENT_CONT;
}
