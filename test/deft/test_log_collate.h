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

   test_log_collate.h

   Description:

   
 ****************************************************************************/

#ifndef _TEST_LOG_COLLATE_H_
#define _TEST_LOG_COLLATE_H_

#include "sio_loop.h"
#include "sio_buffer.h"
#include "sio_raf_server.h"

class LogAcceptHandler:public FD_Handler
{
public:
  LogAcceptHandler();
  ~LogAcceptHandler();

  void start(int port);
  void stop();
  void handle_accept(s_event_t, void *);
};

enum Log_Collator_Mode_t
{
  LC_RAF,
  LC_COLLATE
};

class LogCollateHandler:public SioRafServer
{
public:
  LogCollateHandler();
  virtual ~ LogCollateHandler();

  void LogCollateHandler::start(int new_fd);
  void handle_log_input(s_event_t, void *);
  void wait_for_shutdown_complete(s_event_t, void *);
  static int active_loggers;

protected:
    virtual void dispatcher();
  virtual void response_complete();

private:
  void process_cmd_shutdown();
  void process_cmd_log_roll();

  Log_Collator_Mode_t lc_mode;
  sio_buffer *input_buffer;

  // shutdown stuff
  S_Event *timer_event;
};


#endif
