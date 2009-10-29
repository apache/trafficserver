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

#include "P_log.h"
#include "I_EventSystem.h"

#include "assert.h"


// --
// used by external methods
// --

void
XactLog::startXact(XactId xactid)
{
  char buf[16];
  LogRecord *rec = newLog(e_start_transaction, buf, sizeof(buf));
  rec->curXact = xactid;
  rec->length = LOGRECORD_MIN;
  // add to list of active transactions

  // write to log
  appendLog(rec);
  freeLog(rec);
}

void
XactLog::commitXact(XactId xactid)
{
  char buf[16];
  LogRecord *rec = newLog(e_commit_transaction, buf, sizeof(buf));
  rec->curXact = xactid;
  rec->length = LOGRECORD_MIN;
  // remove from list of active transactions

  // write to log
  appendLog(rec);
  freeLog(rec);
}

void
XactLog::abortXact(XactId xactid)
{
  char buf[16];
  LogRecord *rec = newLog(e_abort_transaction, buf, sizeof(buf));
  rec->curXact = xactid;
  rec->length = LOGRECORD_MIN;

  // remove from list of active transactions

  // write to log
  appendLog(rec);
  freeLog(rec);
}

Action *
XactLog::flush(Continuation * c)
{
  // async IO of iobufferblock chain via AIO functions
  // call back continuation when done.
  (void) c;
  return ACTION_RESULT_NONE;
}

// -----------------------------------------------------------------
// internal functions
// -----------------------------------------------------------------
LogRecord *
XactLog::newLog(XactLog::XactLogType t, void *allocedMem, int len)
{
  (void) len;
  LogRecord *mem = (LogRecord *) allocedMem;
  if (mem)
    mem->dynalloc = 0;
  // XXX: allocate storage for log entry if not already alloced or
  // not big enough.
  switch (t) {
  case e_start_transaction:
  case e_commit_transaction:
  case e_abort_transaction:
    mem->recordType = t;
    break;
  default:
    assert(!"bad type");
    break;
  }
  return mem;
}

void
XactLog::freeLog(LogRecord * rec)
{
  if (rec->dynalloc) {
    // free record
  }
}

void
XactLog::appendLog(LogRecord * rec)
{
  (void) rec;

  // spin lock
  if (rec fits into current iobuffer block) {
    // write rec into current IOBufferBlock
    // unlock
  } else {
    // unlock
    // allocate new iobuffer block
    // spin lock
    if (rec fits into current iobuffer block) {
      // write rec into current IOBufferBlock
      // unlock
      // discard newly allocated block
    } else {
      // chain onto end of current chain
      // make it the current block
      // write rec into block
      // unlock
    }
  }
}

// set region on disk volume that will be used for logging
// initialize end points
//  0,0 to start with
