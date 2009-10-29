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

#ifndef _DETAILEDLOG_H_
#define _DETAILEDLOG_H_

#include <string>
#include <list>
#include <inktomi++.h>
#include <P_EventSystem.h>
#include <pthread.h>

//----------------------------------------------------------------------------
class Message
{
public:
  Message(const char *message):_message(message)
  {
    _time = ink_get_hrtime();
  }

  std::string _message;
  ink_hrtime _time;
};


//----------------------------------------------------------------------------
class DetailedLog
{
public:
  DetailedLog():_start(0), _last(0), _size(0)
  {
    pthread_mutex_init(&_modifyMutex, NULL);
  }

  void add(const char *message);
  void print();

  ink_hrtime totalTime() const
  {
    return (ink_get_hrtime() - _start);
  }
  void clear();

private:
  std::list<Message> _messages;
  ink_hrtime _start;
  ink_hrtime _last;
  int _size;
  pthread_mutex_t _modifyMutex;
};

#endif // _DETAILEDLOG_H_
