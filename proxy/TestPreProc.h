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

#if !defined(_TestPreProc_h_)
#define _TestPreProc_h_

class IOBuffer;

class RequestInput
{
public:
  RequestInput(const char *str, IOBuffer *cb);
  ~RequestInput();

  void run();
  bool
  isDone() const
  {
    return (m_len == 0);
  }

private:
  RequestInput(const RequestInput &);
  RequestInput &operator=(const RequestInput &);

  char *m_sp;
  unsigned m_len;
  IOBuffer *m_cb;
};

#endif
