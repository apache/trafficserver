/** @file

  SpdySessionAccept

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

#ifndef SpdySessionAccept_H_
#define SpdySessionAccept_H_

#include "P_Net.h"
#include "P_EventSystem.h"
#include "P_UnixNet.h"
#include "I_IOBuffer.h"
#include "SpdyDefs.h"

class SpdySessionAccept : public SessionAccept
{
public:
  explicit SpdySessionAccept(spdy::SessionVersion vers);
  ~SpdySessionAccept() {}
  void accept(NetVConnection *, MIOBuffer *, IOBufferReader *);

private:
  int mainEvent(int event, void *netvc);
  SpdySessionAccept(const SpdySessionAccept &);            // disabled
  SpdySessionAccept &operator=(const SpdySessionAccept &); // disabled

  spdy::SessionVersion version;
};

#endif /* SpdySessionAccept_H_ */
