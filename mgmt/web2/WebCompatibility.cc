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
 *
 *  WebCompatibility.cc - cross platform issues dealt with here
 *
 *
 ****************************************************************************/

#include "WebCompatibility.h"
#include "MgmtSocket.h"

//-------------------------------------------------------------------------
// WebGetHostname_Xmalloc
//-------------------------------------------------------------------------

char *
WebGetHostname_Xmalloc(sockaddr_in * client_info)
{
  ink_gethostbyaddr_r_data data;
  char *hostname_tmp;

  struct hostent *r = ink_gethostbyaddr_r((char *) &client_info->sin_addr.s_addr,
                                          sizeof(client_info->sin_addr.s_addr),
                                          AF_INET,
                                          &data);

  hostname_tmp = r ? r->h_name : inet_ntoa(client_info->sin_addr);
  return ats_strdup(hostname_tmp);

}

//-------------------------------------------------------------------------
// WebFileOpenR
//-------------------------------------------------------------------------

WebHandle
WebFileOpenR(const char *file)
{

  WebHandle h_file;
  if ((h_file = mgmt_open(file, O_RDONLY)) < 0) {
    return WEB_HANDLE_INVALID;
  }
  return h_file;
}

//-------------------------------------------------------------------------
// WebFileOpenW
//-------------------------------------------------------------------------

WebHandle
WebFileOpenW(const char *file)
{

  WebHandle h_file;

  if ((h_file = mgmt_open_mode(file, O_WRONLY | O_APPEND | O_CREAT, 0644)) < 0) {
    return WEB_HANDLE_INVALID;
  }
  fcntl(h_file, F_SETFD, 1);

  return h_file;

}

//-------------------------------------------------------------------------
// WebFileClose
//-------------------------------------------------------------------------

void
WebFileClose(WebHandle h_file)
{

  close(h_file);
}

//-------------------------------------------------------------------------
// WebFileRead
//-------------------------------------------------------------------------

int
WebFileRead(WebHandle h_file, char *buf, int size, int *bytes_read)
{

  if ((*bytes_read =::read(h_file, buf, size)) < 0) {
    *bytes_read = 0;
    return WEB_HTTP_ERR_FAIL;
  }
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// WebFileWrite
//-------------------------------------------------------------------------

int
WebFileWrite(WebHandle h_file, char *buf, int size, int *bytes_written)
{

  if ((*bytes_written =::write(h_file, buf, size)) < 0) {
    *bytes_written = 0;
    return WEB_HTTP_ERR_FAIL;
  }
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// WebFileImport_Xmalloc
//-------------------------------------------------------------------------

int
WebFileImport_Xmalloc(const char *file, char **file_buf, int *file_size)
{

  int err = WEB_HTTP_ERR_OKAY;
  WebHandle h_file = WEB_HANDLE_INVALID;
  int bytes_read;

  *file_buf = 0;
  *file_size = 0;

  if ((h_file = WebFileOpenR(file)) == WEB_HANDLE_INVALID)
    goto Lerror;
  *file_size = WebFileGetSize(h_file);
  *file_buf = (char *)ats_malloc(*file_size + 1);
  if (WebFileRead(h_file, *file_buf, *file_size, &bytes_read) == WEB_HTTP_ERR_FAIL)
    goto Lerror;
  if (bytes_read != *file_size)
    goto Lerror;
  (*file_buf)[*file_size] = '\0';

  goto Ldone;

Lerror:

  ats_free(*file_buf);
  *file_buf = 0;
  *file_size = 0;
  err = WEB_HTTP_ERR_FAIL;

Ldone:
  if (h_file != WEB_HANDLE_INVALID)
    WebFileClose(h_file);

  return err;

}

//-------------------------------------------------------------------------
// WebFileGetSize
//-------------------------------------------------------------------------

int
WebFileGetSize(WebHandle h_file)
{

  int size;

  struct stat fileStats;
  fstat(h_file, &fileStats);
  size = fileStats.st_size;
  return size;

}

//-------------------------------------------------------------------------
// WebFileGetDateGmt
//-------------------------------------------------------------------------

time_t
WebFileGetDateGmt(WebHandle h_file)
{

  time_t date;

  struct stat fileStats;
  fstat(h_file, &fileStats);
  date = fileStats.st_mtime + ink_timezone();
  return date;

}

//-------------------------------------------------------------------------
// WebSeedRand
//-------------------------------------------------------------------------

void
WebSeedRand(long seed)
{
  srand48(seed);
  return;

}

//-------------------------------------------------------------------------
// WebRand
//-------------------------------------------------------------------------

long
WebRand()
{
  // we may want to fix this later
  // coverity[secure_coding]
  return lrand48();
}
