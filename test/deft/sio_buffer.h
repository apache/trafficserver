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
   Description:
      A simple single reader io buffer which keeps data continguous
         by copying it

 ****************************************************************************/

#ifndef _SIO_BUFFER_H_
#define _SIO_BUFFER_H_

#define DEFAULT_SIO_SIZE 2048

#include <limits.h>

class sio_buffer
{
public:
  sio_buffer();
  sio_buffer(int init_size);
   ~sio_buffer();

  // we make write_avail at least size
  int expand_to(int size);

  int fill(const char *data, int data_len);
  int fill(int n);

  int read_avail();
  int write_avail();

  char *start();
  char *end();

  void consume(int n);
  void reset();

  char *memchr(int c, int len = INT_MAX, int offset = 0);

  // consume data 
  int read(char *buf, int len);

  // does not consume data
  char *memcpy(char *buf, int len = INT_MAX, int offset = 0);

private:

  void init_buffer(int size);

  // No gratuitous copies!
    sio_buffer(const sio_buffer & m);
    sio_buffer & operator =(const sio_buffer & m);

  char *raw_start;
  char *raw_end;

  char *data_start;
  char *data_end;
};

#endif
