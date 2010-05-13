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

   test_utils.h

   Description:


 ****************************************************************************/

#ifndef _TEST_UTILS_H_
#define _TEST_UTILS_H_

char **build_argv(const char *arg0, const char *rest);
char **build_argv_v(const char *arg0, ...);
char **append_argv(char **argv1, char **argv2);

void destroy_argv(char **argv);

int check_package_file_extension(const char *file_name, const char **ext_ptr);
const char *create_or_verify_dir(const char *dir, int *error_code);

// Caller frees return value
char *get_arch_str();

class sio_buffer;

const char *write_buffer(int fd, sio_buffer * buf, int *timeout_ms);
const char *read_until(int fd, sio_buffer * read_buffer, char end_chr, int *timeout_ms);

const char *read_to_buffer(int fd, sio_buffer * read_buffer, int nbytes, int *eof, int *timeout_ms);

class RafCmd;
const char *send_raf_cmd(int fd, RafCmd * request, int *timeout_ms);
const char *read_raf_resp(int fd, sio_buffer * read_buffer, RafCmd * response, int *timeout_ms);

class FreeOnDestruct
{
public:
  FreeOnDestruct(void *p);
   ~FreeOnDestruct();
private:
  void *ptr;
};

inline
FreeOnDestruct::FreeOnDestruct(void *p):
ptr(p)
{
}


inline
FreeOnDestruct::~
FreeOnDestruct()
{
  free(ptr);
}


#endif
