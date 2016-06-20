/** @file

  Record compatibility definitions

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

#include "ts/ink_platform.h"
#include "ts/ink_string.h"
#include "P_RecFile.h"
#include "P_RecDefs.h"
#include "P_RecUtils.h"

//-------------------------------------------------------------------------
// RecFileOpenR
//-------------------------------------------------------------------------

RecHandle
RecFileOpenR(const char *file)
{
  RecHandle h_file;
  return ((h_file = ::open(file, O_RDONLY)) < 0) ? REC_HANDLE_INVALID : h_file;
}

//-------------------------------------------------------------------------
// RecFileOpenW
//-------------------------------------------------------------------------

RecHandle
RecFileOpenW(const char *file)
{
  RecHandle h_file;

  if ((h_file = ::open(file, O_WRONLY | O_TRUNC | O_CREAT, 0600)) < 0) {
    return REC_HANDLE_INVALID;
  }
  fcntl(h_file, F_SETFD, 1);
  return h_file;
}

//-------------------------------------------------------------------------
// RecFileSync
//-------------------------------------------------------------------------

int
RecFileSync(RecHandle h_file)
{
  return (fsync(h_file) == 0) ? REC_ERR_OKAY : REC_ERR_FAIL;
}

//-------------------------------------------------------------------------
// RecFileClose
//-------------------------------------------------------------------------

int
RecFileClose(RecHandle h_file)
{
  return (close(h_file) == 0) ? REC_ERR_OKAY : REC_ERR_FAIL;
}

//-------------------------------------------------------------------------
// RecFileRead
//-------------------------------------------------------------------------

int
RecFileRead(RecHandle h_file, char *buf, int size, int *bytes_read)
{
  if ((*bytes_read = ::read(h_file, buf, size)) <= 0) {
    *bytes_read = 0;
    return REC_ERR_FAIL;
  }
  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// RecFileWrite
//-------------------------------------------------------------------------

int
RecFileWrite(RecHandle h_file, char *buf, int size, int *bytes_written)
{
  if ((*bytes_written = ::write(h_file, buf, size)) < 0) {
    *bytes_written = 0;
    return REC_ERR_FAIL;
  }
  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// RecFileGetSize
//-------------------------------------------------------------------------

int
RecFileGetSize(RecHandle h_file)
{
  struct stat fileStats;
  fstat(h_file, &fileStats);
  return (int)fileStats.st_size;
}

//-------------------------------------------------------------------------
// RecFileExists
//-------------------------------------------------------------------------

int
RecFileExists(const char *file)
{
  RecHandle h_file;
  if ((h_file = RecFileOpenR(file)) == REC_HANDLE_INVALID) {
    return REC_ERR_FAIL;
  }
  RecFileClose(h_file);
  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// RecPipeCreate
//-------------------------------------------------------------------------

RecHandle
RecPipeCreate(const char *base_path, const char *name)
{
  RecHandle listenfd;
  RecHandle acceptfd;
  struct sockaddr_un servaddr;
  struct sockaddr_un cliaddr;
  int servaddr_len;
  socklen_t cliaddr_len;

  // first, let's disable SIGPIPE (move out later!)
  struct sigaction act, oact;
  act.sa_handler = SIG_IGN;
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;
  act.sa_flags |= SA_RESTART;
  sigaction(SIGPIPE, &act, &oact);

  // construct a path/filename for the pipe
  char path[PATH_NAME_MAX];
  snprintf(path, sizeof(path), "%s/%s", base_path, name);
  if (strlen(path) > (sizeof(servaddr.sun_path) - 1)) {
    RecLog(DL_Warning, "[RecPipeCreate] Path name too long; exiting\n");
    return REC_HANDLE_INVALID;
  }

  unlink(path);

  if ((listenfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
    RecLog(DL_Warning, "[RecPipeCreate] socket error\n");
    return REC_HANDLE_INVALID;
  }
  // set so that child process doesn't inherit our fd
  if (fcntl(listenfd, F_SETFD, 1) < 0) {
    RecLog(DL_Warning, "[RecPipeCreate] fcntl error\n");
    close(listenfd);
    return REC_HANDLE_INVALID;
  }

  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sun_family = AF_UNIX;
  ink_strlcpy(servaddr.sun_path, path, sizeof(servaddr.sun_path));

  int optval = 1;
  if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (char *)&optval, sizeof(int)) < 0) {
    RecLog(DL_Warning, "[RecPipeCreate] setsockopt error\n");
    close(listenfd);
    return REC_HANDLE_INVALID;
  }

  servaddr_len = sizeof(servaddr.sun_family) + strlen(servaddr.sun_path);
  if ((bind(listenfd, (struct sockaddr *)&servaddr, servaddr_len)) < 0) {
    RecLog(DL_Warning, "[RecPipeCreate] bind error\n");
    close(listenfd);
    return REC_HANDLE_INVALID;
  }
  // listen, backlog of 1 (expecting only one client)
  if ((listen(listenfd, 1)) < 0) {
    RecLog(DL_Warning, "[RecPipeCreate] listen error\n");
    close(listenfd);
    return REC_HANDLE_INVALID;
  }
  // block until we get a connection from the other side
  cliaddr_len = sizeof(cliaddr);
  if ((acceptfd = accept(listenfd, (struct sockaddr *)&cliaddr, &cliaddr_len)) < 0) {
    close(listenfd);
    return REC_HANDLE_INVALID;
  }

  close(listenfd);

  return acceptfd;
}

//-------------------------------------------------------------------------
// RecPipeConnect
//-------------------------------------------------------------------------

RecHandle
RecPipeConnect(const char *base_path, const char *name)
{
  RecHandle sockfd;
  struct sockaddr_un servaddr;
  int servaddr_len;

  // construct a path/filename for the pipe
  char path[PATH_NAME_MAX];
  snprintf(path, sizeof(path), "%s/%s", base_path, name);
  if (strlen(path) > (sizeof(servaddr.sun_path) - 1)) {
    RecLog(DL_Warning, "[RecPipeConnect] Path name too long\n");
    return REC_HANDLE_INVALID;
  }
  // Setup Connection to LocalManager */
  memset((char *)&servaddr, 0, sizeof(servaddr));
  servaddr.sun_family = AF_UNIX;
  ink_strlcpy(servaddr.sun_path, path, sizeof(servaddr.sun_path));
  servaddr_len = sizeof(servaddr.sun_family) + strlen(servaddr.sun_path);

  if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
    RecLog(DL_Warning, "[RecPipeConnect] socket error\n");
    return REC_HANDLE_INVALID;
  }
  // set so that child process doesn't inherit our fd
  if (fcntl(sockfd, F_SETFD, 1) < 0) {
    RecLog(DL_Warning, "[RecPipeConnect] fcntl error\n");
    close(sockfd);
    return REC_HANDLE_INVALID;
  }
  // blocking connect
  if ((connect(sockfd, (struct sockaddr *)&servaddr, servaddr_len)) < 0) {
    RecLog(DL_Warning, "[RecPipeConnect] connect error\n");
    close(sockfd);
    return REC_HANDLE_INVALID;
  }

  return sockfd;
}

//-------------------------------------------------------------------------
// RecPipeRead
//-------------------------------------------------------------------------

int
RecPipeRead(RecHandle h_pipe, char *buf, int size)
{
  int bytes_read   = 0;
  int bytes_wanted = size;
  char *p          = buf;
  while (bytes_wanted > 0) {
    bytes_read = read(h_pipe, p, bytes_wanted);
    if (bytes_read < 0) {
      // FIXME: something more intelligent please!
      return REC_ERR_FAIL;
    }
    bytes_wanted -= bytes_read;
    p += bytes_read;
  }
  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// RecPipeWrite
//-------------------------------------------------------------------------

int
RecPipeWrite(RecHandle h_pipe, char *buf, int size)
{
  int bytes_written  = 0;
  int bytes_to_write = size;
  char *p            = buf;
  while (bytes_to_write > 0) {
    bytes_written = write(h_pipe, p, bytes_to_write);
    if (bytes_written < 0) {
      // FIXME: something more intelligent please!
      return REC_ERR_FAIL;
    }
    bytes_to_write -= bytes_written;
    p += bytes_written;
  }

  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// RecPipeClose
//-------------------------------------------------------------------------

int
RecPipeClose(RecHandle h_pipe)
{
  return (close(h_pipe) == 0) ? REC_ERR_OKAY : REC_ERR_FAIL;
}
