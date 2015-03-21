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

#ifndef LOG_BUFFER_SINK
#define LOG_BUFFER_SINK

#include "LogBuffer.h"

/**********************************************************************
The LogBufferSink class in an abstract class that provides an interface
for classes that take a LogBuffer and write it to disk or send it over
the network
**********************************************************************/

class LogBufferSink
{
public:
  //
  // The preproc_and_try_delete() function should be responsible for
  // freeing memory pointed to by _buffer_ parameter.
  //
  // Of course, this function may not free memory directly, it
  // can delegate another function to do it.
  //
  // return 0 if success, -1 on error.
  //
  virtual int preproc_and_try_delete(LogBuffer *buffer) = 0;
  virtual ~LogBufferSink(){};
};

#endif
