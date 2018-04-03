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
  fcntl(h_file, F_SETFD, FD_CLOEXEC);
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
