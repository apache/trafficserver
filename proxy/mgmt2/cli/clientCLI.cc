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
 *  clientCLI.cc - A simple client to communicate to local manager
 *
 * 
 ****************************************************************************/

#include "ink_unused.h"      /* MAGIC_EDITING_TAG */

#include "ink_args.h"
#include "ink_sock.h"
#include "Tokenizer.h"
#include "TextBuffer.h"
#include "CliUtils.h"           /* cli_read_timeout(), cli_write_timeout(),GetTSDirectory() */
#include "clientCLI.h"

const char *
  clientCLI::CliResultStr[] = {
  "no error",
  "traffic_manager refusing onnection",
  "unable to connect to traffic_manager",
  "invalid response from traffic_manager",
  "system error"
};

#ifndef _WIN32
const char *
  clientCLI::defaultSockPath = "./conf/yts/cli";
#else
const int
  clientCLI::defaultCliPort = 9000;
#endif

clientCLI::clientCLI(void):
socketFD(0)
{
#ifndef _WIN32
  ink_strncpy(sockPath, clientCLI::defaultSockPath, sizeof(sockPath));
#else
  cliPort = clientCLI::defaultCliPort;
#endif
  //coverity[uninit_member]
}                               // end clientCLI(const char*)

//
// Default destructor
//
clientCLI::~clientCLI(void)
{                               // nothing
}                               // end ~clientCLI()

#ifndef _WIN32
//fix BZ48417
void
clientCLI::readTSdir()
{
  char sPath[512];
  if (GetTSDirectory(sPath)) {
    ink_strncpy(sockPath, clientCLI::defaultSockPath, sizeof(sockPath));
  } else {
    ink_snprintf(sockPath, sizeof(sockPath), "%s/conf/yts/cli", sPath);
  }
}
void
clientCLI::setSockPath(const char *path)
{
  ink_strncpy(sockPath, path, sizeof(sockPath));
}
#else
void
clientCLI::setCliPort(int port)
{
  cliPort = port;
}
#endif

clientCLI::CliResult clientCLI::disconnectFromLM(void)
{
  ink_close_socket(socketFD);
  socketFD = 0;
  return err_none;
}                               // end disconnectFromLM()

#ifndef _WIN32

//
//  Attempts to connect to the LocalManager
//    process via the UNIX domain socket
//    referenced by sockPath
//
clientCLI::CliResult clientCLI::connectToLM(void)
{
  struct sockaddr_un
    clientS;
  int
    sockaddrLen;

  // create stream socket
  socketFD = socket(AF_UNIX, SOCK_STREAM, 0);
  if (socketFD < 0) {
    return err_system;
  }
  // setup UNIX domain socket
  memset(&clientS, 0, sizeof(sockaddr_un));
  clientS.sun_family = AF_UNIX; // UNIX domain socket
  ink_strncpy(clientS.sun_path, sockPath, sizeof(clientS.sun_path));
  sockaddrLen = sizeof(clientS.sun_family) + strlen(clientS.sun_path);;

  // make socket non-blocking
  if (safe_nonblocking(socketFD) < 0) {
    fprintf(stderr, "Unable to set non-blocking flags on socket : %s\n", strerror(errno));
  }

  if (connect(socketFD, (struct sockaddr *) &clientS, sockaddrLen) < 0) {
    // Since this a non-blocking socket the connect may be in progress
    if (errno != EINPROGRESS) {
      if (errno == ECONNRESET) {
        return err_tm_refuse_conn;
      } else {
        return err_tm_cannot_conn;
      }
    }
  }

  return err_none;

}                               // end connectToLM()

#else

clientCLI::CliResult clientCLI::connectToLM(void)
{
  struct sockaddr_in
    clientS;
  int
    sockaddrLen;

  // create stream socket
  socketFD = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (socketFD < 0) {
    return err_system;
  }

  memset(&clientS, 0, sizeof(sockaddr_in));
  clientS.sin_family = AF_INET;
  clientS.sin_addr.s_addr = inet_addr("127.0.0.1");
  clientS.sin_port = htons(cliPort);
  sockaddrLen = sizeof(sockaddr_in);

  if (connect(socketFD, (struct sockaddr *) &clientS, sockaddrLen) == SOCKET_ERROR) {
    return err_tm_cannot_conn;
  }

  return err_none;
}

#endif /* !_WIN32 */

//
//    Reads a response from the Local Manager process
//       the response is placed into parameter output
//       returns the number of bytes read
//    Timeout is in milliseconds
//
int
clientCLI::readResponse(textBuffer * output, ink_hrtime timeout)
{
  int readBytes = 0;            // number of bytes read
  int readResult = 0;           // status of read
  char buf[8192];
  ink_hrtime timeLeft = timeout;
  ink_hrtime endTime = milliTime() + timeout;

  // read response
  do {

    readResult = cli_read_timeout(socketFD, buf, 8192, timeLeft);

    if (timeout > 0) {
      timeLeft = endTime - milliTime();
      if (timeLeft < 0)
        timeLeft = 0;
    }

    if (readResult > 0) {
      output->copyFrom(buf, readResult);
      readBytes += readResult;
    }
  } while (readResult == 8192);

  // check status of read and number of bytes read
  if (readResult < 0 || readBytes == 0) {
    return 0;
  }

  return readBytes;
}                               // end readResponse()

int
clientCLI::sendCommand(const char *cmd, textBuffer * response, ink_hrtime timeout)
{
  // Timeout vars
  ink_hrtime timeLeft, endTime = 0;

  // Check timeout
  if (timeout > 0) {
    timeLeft = timeout * 1000;
    endTime = milliTime() + timeLeft;
  } else {
    timeLeft = -1;
  }

  cli_write_timeout(socketFD, cmd, strlen(cmd), timeLeft);

  if (timeout > 0) {
    timeLeft = endTime - milliTime();
    if (timeLeft < 0)
      timeLeft = 0;
  }
  // read and get response string from Local Manager 
  return readResponse(response, timeLeft);
}

clientCLI::CliResult clientCLI::startupLocal(void)
{
  textBuffer
  response(512);                // textBuffer object to hold response
  //char*       responseStr;   // actual response string
  Tokenizer
  respTok(";");
  const char *
    status = NULL;
  //const char* prompt = NULL;
  //const char* resp = NULL;

  sendCommand("b 5", &response);
  //responseStr = response.bufPtr();

  // parse response request from server into 3 tokens:
  // status, prompt and response
  respTok.setMaxTokens(3);      //<status>;<prompt>;<response>
  respTok.Initialize(response.bufPtr(), COPY_TOKS);
  status = respTok[0];
  //prompt = respTok[1]; // we ignore printing this for batch mode
  //resp   = respTok[2];

  if (status && *status == '1') {       // OK
    return err_none;
  }

  return err_tm_invalid_resp;
}

clientCLI::CliResult clientCLI::shutdownLocal(void)
{
  textBuffer
  response(512);                // textBuffer object to hold response
  //char*       responseStr;   // actual response string
  Tokenizer
  respTok(";");
  const char *
    status = NULL;
  //const char* prompt = NULL;
  //const char* resp = NULL;

  sendCommand("b 4", &response);
  //responseStr = response.bufPtr();

  // parse response request from server into 3 tokens:
  // status, prompt and response
  respTok.setMaxTokens(3);      //<status>;<prompt>;<response>
  respTok.Initialize(response.bufPtr(), COPY_TOKS);
  status = respTok[0];
  //prompt = respTok[1]; // we ignore printing this for batch mode
  //resp   = respTok[2];

  if (status && *status == '1') {       // OK
    return err_none;
  }

  return err_tm_invalid_resp;
}

clientCLI::CliResult clientCLI::probeLocal(bool * running)
{
  textBuffer
  response(512);                // textBuffer object to hold response
  //char*       responseStr;   // actual response string
  CliResult
    result = err_tm_invalid_resp;
  Tokenizer
  respTok(";");
  const char *
    status = NULL;
  //const char* prompt = NULL;
  const char *
    resp = NULL;

  sendCommand("b get proxy.node.proxy_running", &response);
  //responseStr = response.bufPtr();

  // parse response request from server into 3 tokens:
  // status, prompt and response
  respTok.setMaxTokens(3);      //<status>;<prompt>;<response>
  respTok.Initialize(response.bufPtr(), COPY_TOKS);
  status = respTok[0];
  //prompt = respTok[1]; // we ignore printing this for batch mode
  resp = respTok[2];

  if (status && *status == '1') {       // OK
    if (resp != NULL) {
      result = err_none;
      if (resp[0] == '0') {
        *running = false;
      } else {
        *running = true;
      }
    }
  }

  return result;
}

clientCLI::CliResult clientCLI::getVariable(const char *name, char **value)
{
  textBuffer
  response(512);                // textBuffer object to hold response
  //char*       responseStr;   // actual response string
  CliResult
    result = err_tm_invalid_resp;
  Tokenizer
  respTok(";");
  const char *
    status = NULL;
  //const char* prompt = NULL;
  const char *
    resp = NULL;
  char
    requestStr[512];

  ink_snprintf(requestStr, sizeof(requestStr), "b get %s", name);
  sendCommand(requestStr, &response);
  //responseStr = response.bufPtr();

  // parse response request from server into 3 tokens:
  // status, prompt and response
  respTok.setMaxTokens(3);      //<status>;<prompt>;<response>
  respTok.Initialize(response.bufPtr(), COPY_TOKS);
  status = respTok[0];
  //prompt = respTok[1]; // we ignore printing this for batch mode
  resp = respTok[2];

  if (status && *status == '1') {       // OK
    if (resp != NULL) {
      result = err_none;
      *value = strdup(resp);
    } else {
      *value = NULL;
    }
  }

  return result;
}
