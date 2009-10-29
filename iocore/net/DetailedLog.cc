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

#include "DetailedLog.h"

static pthread_mutex_t logMutex = PTHREAD_MUTEX_INITIALIZER;


//----------------------------------------------------------------------------
void
DetailedLog::add(const char *message)
{
  if (pthread_mutex_trylock(&_modifyMutex) != 0) {
    Error("************ Someone is already here %llu", this);
    pthread_mutex_lock(&_modifyMutex);
  }
  // limit the log to the last 1000 messages
  if (_size >= 1000) {
    _messages.pop_front();
    --_size;
  }
  Message x(message);
  _messages.push_back(x);
  ++_size;
  if (_start == 0) {
    _start = _messages.back()._time;
  }
  _last = _messages.back()._time;

  pthread_mutex_unlock(&_modifyMutex);
}


//----------------------------------------------------------------------------
void
DetailedLog::print()
{
  pthread_mutex_lock(&_modifyMutex);
  if (_messages.size() <= 0) {
    pthread_mutex_unlock(&_modifyMutex);
    return;
  }

  std::string errorMessage = "";
  char buffer[256];
  for (std::list<Message>::const_iterator it = _messages.begin(); it != _messages.end(); ++it) {
    if (errorMessage.length() > 0) {
      errorMessage += ", ";
    }
    snprintf(buffer, 256, "(time: %.3f - %s)", (double) (it->_time - _start) / 1000000000LL, it->_message.c_str());
    errorMessage += buffer;
  }
  pthread_mutex_unlock(&_modifyMutex);
  pthread_mutex_lock(&logMutex);
  Error("Detailed Request: %s", errorMessage.c_str());
  pthread_mutex_unlock(&logMutex);
}


//----------------------------------------------------------------------------
void
DetailedLog::clear()
{
  pthread_mutex_lock(&_modifyMutex);
  _messages.clear();
  _start = 0;
  _last = 0;
  _size = 0;
  pthread_mutex_unlock(&_modifyMutex);
}
