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

  NTAIO.cc


 ****************************************************************************/

#include "P_AIO.h"

extern Continuation *aio_err_callbck;

// AIO Stats
extern uint64_t aio_num_read;
extern uint64_t aio_bytes_read;
extern uint64_t aio_num_write;
extern uint64_t aio_bytes_written;

NTIOCompletionPort aio_completion_port(1);


static inline void
init_op_sequence(AIOCallback * op, int opcode)
{

  // zero aio_result's and init opcodes
  AIOCallback *cur_op;
  for (cur_op = op; cur_op; cur_op = cur_op->then) {
    cur_op->aio_result = 0;
    cur_op->aiocb.aio_lio_opcode = opcode;
    // set the last op to point to the first op
    if (cur_op->then == NULL)
      ((AIOCallbackInternal *) cur_op)->first = op;
  }
}

static inline void
cache_op(AIOCallback * op)
{

  DWORD bytes_trans;

  // make op continuation share op->action's mutex
  op->mutex = op->action.mutex;

  // construct a continuation to handle the io completion
  NTCompletionEvent *ce = NTCompletionEvent_alloc(op);
  OVERLAPPED *overlapped = ce->get_overlapped();
  overlapped->Offset = (unsigned long) (op->aiocb.aio_offset & 0xFFFFFFFF);
  overlapped->OffsetHigh = (unsigned long)
    (op->aiocb.aio_offset >> 32) & 0xFFFFFFFF;
  // do the io
  BOOL ret;
  switch (op->aiocb.aio_lio_opcode) {
  case LIO_READ:
    ret = ReadFile((HANDLE) op->aiocb.aio_fildes,
                   op->aiocb.aio_buf, (unsigned long) op->aiocb.aio_nbytes, &bytes_trans, overlapped);
    break;
  case LIO_WRITE:
    ret = WriteFile((HANDLE) op->aiocb.aio_fildes,
                    op->aiocb.aio_buf, (unsigned long) op->aiocb.aio_nbytes, &bytes_trans, overlapped);
    break;
  default:
    ink_debug_assert(!"unknown aio_lio_opcode");
  }
  DWORD lerror = GetLastError();
  if (ret == FALSE && lerror != ERROR_IO_PENDING) {

    op->aio_result = -((int) lerror);
    eventProcessor.schedule_imm(op);
  }

}

int
ink_aio_read(AIOCallback * op)
{
  init_op_sequence(op, LIO_READ);
  cache_op(op);
  return 1;
}

int
ink_aio_write(AIOCallback * op)
{
  init_op_sequence(op, LIO_WRITE);
  cache_op(op);
  return 1;
}


struct AIOMissEvent:Continuation
{
  AIOCallback *cb;

  int mainEvent(int event, Event * e)
  {
    if (!cb->action.cancelled)
      cb->action.continuation->handleEvent(AIO_EVENT_DONE, cb);
    delete this;
      return EVENT_DONE;
  }

  AIOMissEvent(ProxyMutex * amutex, AIOCallback * acb)
  : Continuation(amutex), cb(acb)
  {
    SET_HANDLER(&AIOMissEvent::mainEvent);
  }
};

int
AIOCallbackInternal::io_complete(int event, void *data)
{

  int lerror;
  NTCompletionEvent *ce = (NTCompletionEvent *) data;

  // if aio_result is set, the original Read/Write call failed
  if (!aio_result) {
    lerror = ce->lerror;
    aio_result = lerror ? -lerror : ce->_bytes_transferred;
  }
  // handle io errors
  if ((lerror != 0) && aio_err_callbck) {
    // schedule aio_err_callbck to be called-back
    // FIXME: optimization, please... ^_^
    AIOCallback *op = NEW(new AIOCallbackInternal());
    op->aiocb.aio_fildes = aiocb.aio_fildes;
    op->action = aio_err_callbck;
    eventProcessor.schedule_imm(NEW(new AIOMissEvent(op->action.mutex, op)));
  } else {
    ink_debug_assert(ce->_bytes_transferred == aiocb.aio_nbytes);
  }

  if (then) {
    // more op's in this sequence
    cache_op(then);
  } else {
    // we're done! callback action
    if (!first->action.cancelled) {
      first->action.continuation->handleEvent(AIO_EVENT_DONE, first);
    }
  }

  return 0;
}
