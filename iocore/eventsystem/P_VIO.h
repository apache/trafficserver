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

#pragma once
#include "I_VIO.h"

TS_INLINE
VIO::VIO(int aop) : cont(nullptr), nbytes(0), ndone(0), op(aop), buffer(), vc_server(nullptr), mutex(nullptr) {}

/////////////////////////////////////////////////////////////
//
//  VIO::VIO()
//
/////////////////////////////////////////////////////////////
TS_INLINE
VIO::VIO() : cont(nullptr), nbytes(0), ndone(0), op(VIO::NONE), buffer(), vc_server(nullptr), mutex(nullptr) {}

TS_INLINE Continuation *
VIO::get_continuation()
{
  return cont;
}
TS_INLINE void
VIO::set_writer(MIOBuffer *writer)
{
  buffer.writer_for(writer);
}
TS_INLINE void
VIO::set_reader(IOBufferReader *reader)
{
  buffer.reader_for(reader);
}
TS_INLINE MIOBuffer *
VIO::get_writer()
{
  return buffer.writer();
}
TS_INLINE IOBufferReader *
VIO::get_reader()
{
  return (buffer.reader());
}
TS_INLINE int64_t
VIO::ntodo()
{
  return nbytes - ndone;
}
TS_INLINE void
VIO::done()
{
  if (buffer.reader()) {
    nbytes = ndone + buffer.reader()->read_avail();
  } else {
    nbytes = ndone;
  }
}

/////////////////////////////////////////////////////////////
//
//  VIO::set_continuation()
//
/////////////////////////////////////////////////////////////
TS_INLINE void
VIO::set_continuation(Continuation *acont)
{
  if (vc_server) {
    vc_server->set_continuation(this, acont);
  }
  if (acont) {
    mutex = acont->mutex;
    cont  = acont;
  } else {
    mutex = nullptr;
    cont  = nullptr;
  }
  return;
}

/////////////////////////////////////////////////////////////
//
//  VIO::reenable()
//
/////////////////////////////////////////////////////////////
TS_INLINE void
VIO::reenable()
{
  if (vc_server) {
    vc_server->reenable(this);
  }
}

/////////////////////////////////////////////////////////////
//
//  VIO::reenable_re()
//
/////////////////////////////////////////////////////////////
TS_INLINE void
VIO::reenable_re()
{
  if (vc_server) {
    vc_server->reenable_re(this);
  }
}
