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

  Processor.cc

  Processor objects process requests which are placed in the Processor's
  input queue.  A Processor can contain multiple threads to process
  requests in the queue.  Requests in the queue are Continuations, which
  describe functions to run, and what to do when the function is complete
  (if anything).

  Basically, Processors should be viewed as multi-threaded schedulers which
  process request Continuations from their queue.  Requests can be made of
  a Processor either by directly adding a request Continuation to the queue,
  or more conveniently, by calling a method service call which synthesizes
  the appropriate request Continuation and places it in the queue.


 ****************************************************************************/

#include "P_EventSystem.h"
//////////////////////////////////////////////////////////////////////////////
//
//      Processor::Processor()
//
//      Constructor for a Processor.
//
//////////////////////////////////////////////////////////////////////////////

Processor::Processor()
{
} /* End Processor::Processor() */

//////////////////////////////////////////////////////////////////////////////
//
//      Processor::~Processor()
//
//      Destructor for a Processor.
//
//////////////////////////////////////////////////////////////////////////////

Processor::~Processor()
{
} /* End Processor::~Processor() */

//////////////////////////////////////////////////////////////////
//
//  Processor::create_thread()
//
//////////////////////////////////////////////////////////////////
Thread *
Processor::create_thread(int /* thread_index */)
{
  ink_release_assert(!"Processor::create_thread -- no default implementation");
  return ((Thread *)0);
}

//////////////////////////////////////////////////////////////////
//
//  Processor::get_thread_count()
//
//////////////////////////////////////////////////////////////////
int
Processor::get_thread_count()
{
  return (0);
}
