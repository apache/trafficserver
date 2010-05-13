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

   log_sender.h

   Description:


 ****************************************************************************/

#ifndef _LOG_SENDER_H_
#define _LOG_SENDER_H_

#include "sio_loop.h"
#include "sio_buffer.h"

class LogSender:public FD_Handler
{
public:
  LogSender();
  ~LogSender();

  void start_to_file(const char *filename);
  void start_to_net(unsigned int ip, int port);

  void handle_output(s_event_t, void *);
  void add_to_output_log(const char *start, const char *end);

  void flush_output();
  void close_output();

  const char *LogSender::roll_log_file(const char *roll_name);

private:
  char *log_file_name;
  sio_buffer *output_log_buffer;
};

#endif
