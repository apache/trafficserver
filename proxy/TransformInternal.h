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

#include "HttpSM.h"
#include "MIME.h"
#include "Transform.h"
#include "P_EventSystem.h"

class TransformVConnection;

class TransformTerminus : public VConnection
{
public:
  TransformTerminus(TransformVConnection *tvc);

  int handle_event(int event, void *edata);

  VIO *do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf) override;
  VIO *do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner = false) override;
  void do_io_close(int lerrno = -1) override;
  void do_io_shutdown(ShutdownHowTo_t howto) override;

  void reenable(VIO *vio) override;

public:
  TransformVConnection *m_tvc;
  VIO m_read_vio;
  VIO m_write_vio;
  int m_event_count;
  int m_deletable;
  int m_closed;
  int m_called_user;
};

class TransformVConnection : public TransformVCChain
{
public:
  TransformVConnection(Continuation *cont, APIHook *hooks);
  ~TransformVConnection();

  int handle_event(int event, void *edata);

  VIO *do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf);
  VIO *do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner = false);
  void do_io_close(int lerrno = -1);
  void do_io_shutdown(ShutdownHowTo_t howto);

  void reenable(VIO *vio);

  /** Compute the backlog.
      @return The actual backlog, or a value at least @a limit.
  */
  virtual uint64_t backlog(uint64_t limit = UINT64_MAX);

public:
  VConnection *m_transform;
  Continuation *m_cont;
  TransformTerminus m_terminus;
  int m_closed;
};

class TransformControl : public Continuation
{
public:
  TransformControl();

  int handle_event(int event, void *edata);

public:
  APIHooks m_hooks;
  VConnection *m_tvc;
  IOBufferReader *m_read_buf;
  MIOBuffer *m_write_buf;
};

class NullTransform : public INKVConnInternal
{
public:
  NullTransform(ProxyMutex *mutex);
  ~NullTransform();

  int handle_event(int event, void *edata);

public:
  MIOBuffer *m_output_buf;
  IOBufferReader *m_output_reader;
  VIO *m_output_vio;
};

class RangeTransform : public INKVConnInternal
{
public:
  RangeTransform(ProxyMutex *mutex, RangeRecord *ranges, int num_fields, HTTPHdr *transform_resp, const char *content_type,
                 int content_type_len, int64_t content_length);
  ~RangeTransform();

  // void parse_range_and_compare();
  int handle_event(int event, void *edata);

  void transform_to_range();
  void add_boundary(bool end);
  void add_sub_header(int index);
  void change_response_header();
  void calculate_output_cl();

public:
  MIOBuffer *m_output_buf;
  IOBufferReader *m_output_reader;

  // MIMEField *m_range_field;
  HTTPHdr *m_transform_resp;
  VIO *m_output_vio;
  int64_t m_range_content_length;
  int m_num_chars_for_cl;
  int m_num_range_fields;
  int m_current_range;
  const char *m_content_type;
  int m_content_type_len;
  RangeRecord *m_ranges;
  int64_t m_output_cl;
  int64_t m_done;
};

#ifdef PREFETCH
class PrefetchProcessor
{
public:
  void start();
};

extern PrefetchProcessor prefetchProcessor;
#endif // PREFETCH
