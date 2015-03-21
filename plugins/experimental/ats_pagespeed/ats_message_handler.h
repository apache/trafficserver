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

#ifndef NGX_MESSAGE_HANDLER_H_
#define NGX_MESSAGE_HANDLER_H_

#include <ts/ts.h>
#include <cstdarg>

#include "net/instaweb/util/public/basictypes.h"
#include "net/instaweb/util/public/google_message_handler.h"
#include "net/instaweb/util/public/message_handler.h"
#include "net/instaweb/util/public/scoped_ptr.h"
#include "net/instaweb/util/public/string.h"
#include "net/instaweb/util/public/string_util.h"

namespace net_instaweb
{
class AbstractMutex;
class SharedCircularBuffer;
class Timer;
class Writer;

class AtsMessageHandler : public GoogleMessageHandler
{
public:
  explicit AtsMessageHandler(AbstractMutex *mutex);

  void set_buffer(SharedCircularBuffer *buff);

  void
  SetPidString(const int64 pid)
  {
    pid_string_ = StrCat("[", Integer64ToString(pid), "]");
  }
  // Dump contents of SharedCircularBuffer.
  bool Dump(Writer *writer);

protected:
  virtual void MessageVImpl(MessageType type, const char *msg, va_list args);

  virtual void FileMessageVImpl(MessageType type, const char *filename, int line, const char *msg, va_list args);

private:
  GoogleString Format(const char *msg, va_list args);

  scoped_ptr<AbstractMutex> mutex_;
  GoogleString pid_string_;
  GoogleMessageHandler handler_;
  SharedCircularBuffer *buffer_;

  DISALLOW_COPY_AND_ASSIGN(AtsMessageHandler);
};

} // namespace net_instaweb

#endif // NGX_MESSAGE_HANDLER_H_
