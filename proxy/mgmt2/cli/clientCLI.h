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

/***************************************/
/****************************************************************************
 *
 *  clientCLI.h - A simple client to communicate to local manager
 *
 *
 ****************************************************************************/

#ifndef _CLIENT_CLI_H_
#define _CLIENT_CLI_H_

#include "ink_platform.h"
#include "libts.h"

class textBuffer;

/* Client side part of CLI */
class clientCLI
{
public:

  typedef enum
  {
    err_none = 0,               /* no error, everything is ok */
    err_tm_refuse_conn,         /* traffic manager refusing connection */
    err_tm_cannot_conn,         /* unable to connect to traffic manager */
    err_tm_invalid_resp,        /* invalid response from traffic manager */
    err_system                  /* system error, check errno */
  } CliResult;

  static const char *CliResultStr[];

  clientCLI(void);
  ~clientCLI(void) {}

#ifndef _WIN32
   void setSockPath(const char *path);
#else
  void setCliPort(int port);
#endif

  /* send command to traffic manager */
  /* call readResponse()             */
  /* return result of readResponse   */
  int sendCommand(const char *cmd, textBuffer * response, ink_hrtime timeout = -1);

  /* read response from manager */
  int readResponse(textBuffer * output, ink_hrtime timeout = -1);

  /* connects to manager using:
     - UNIX domain socket on UNIX
     - named pipe on NT */
  CliResult connectToLM(void);

  /* disconnect from manger */
  CliResult disconnectFromLM(void);

  /* interface for CDS integration */
  CliResult startupLocal(void);
  CliResult shutdownLocal(void);
  CliResult probeLocal(bool * running);
  /* caller needs to free() the returned value string */
  CliResult getVariable(const char *name, char **value);

#ifndef _WIN32
  char sockPath[PATH_NAME_MAX + 1];
  static const char *defaultSockPath;
#else
  int cliPort;
  static const int defaultCliPort;
#endif

private:

  int socketFD;

  /* copy constructor and assignment operator are private */
  clientCLI(const clientCLI &);
  clientCLI & operator =(const clientCLI &);
};

#endif /* _CLIENT_CLI_H_ */
