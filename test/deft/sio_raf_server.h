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

   sio_raf_server.h

   Description:

   
 ****************************************************************************/

#ifndef _SIO_RAF_SERVER_
#define _SIO_RAF_SERVER_

#include "sio_loop.h"

class sio_buffer;
class RafCmd;
class SioRafServer;

enum Raf_Exit_Mode_t
{
  RAF_EXIT_NONE,
  RAF_EXIT_CONN,
  RAF_EXIT_PROCESS
};

class SioRafServer:public FD_Handler
{
public:
  SioRafServer();
  virtual ~ SioRafServer();

  virtual void start(int new_fd);
  void process_cmd(char *end);

  void handle_read_cmd(s_event_t, void *);
  void handle_write_resp(s_event_t, void *);

protected:
    virtual void dispatcher();
  virtual void response_complete();

  void send_raf_resp(RafCmd * reply);
  void send_raf_resp(RafCmd * cmd, int result_code, const char *msg_fmt, ...);

  RafCmd *raf_cmd;
  Raf_Exit_Mode_t exit_mode;
  sio_buffer *cmd_buffer;
  sio_buffer *resp_buffer;
};


#endif
