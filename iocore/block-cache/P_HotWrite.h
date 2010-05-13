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



#ifndef _P_HotWrite_H_
#define _P_HotWrite_H_

#include "I_MTInteractor.h"

/**
  Pair of classes implementing the multi-reader, single writer,
  fastest-reader-flow-controlled write functionality.  This is
  similar in spirit to the MIOBuffer/IOBufferReader interfaces, but
  this is intended for coordination among continuations which don't
  share the same lock.

  HotWritePsuedoVC is not a true VConnection -- it only implements
  VC_EVENT_READ_READY and VC_EVENT_WRITE_READY events.  The caller
  must implement the rest to provide true VConnection functionality
  to higher layers.

  Example usage in central data structure:

  @code
  HotWrite hw(new_ProxyMutex());
  @endcode

  Example usage in edge connection (writer or reader):

  @code
  HotWriteClient c(My_Continuation);

  c.startAttach();

  My_Continuation::handleEvent(int event, void *data) {
    if (event == VC_EVENT_READ_READY) {
      // fill data into callers MIOBuffer
      // call caller with VC_EVENT_READ_READY
      // reflect MIOBuffer write position back into HotWrite
    } else if (event == VC_EVENT_WRITE_READY) {
      // call caller with VC_EVENT_WRITE_READY

      // reflect IOBufferReaders last read position back into
      // HotWrite
    }
  }

  My_Continuation::close() {
    c.startDetach();
  }
  @endcode

 */

class HotWrite:public MTInteractor
{
public:
  enum
  {
    e_read_ready,
    e_write_ready,
  };
    HotWrite(ProxyMutex * m);
    virtual ~ HotWrite();

  // implements the MTInteractor pattern
  virtual int attachClient(MTClient * c);
  virtual int detachClient(MTClient * c);

private:
};

class HotWritePseudoVC:public MTClient
{
public:
  HotWritePseudoVC(Continuation * c);
  virtual ~ HotWritePseudoVC();

  // handles events in attached and detached state
  virtual int handleAttached(int event, void *data);
  virtual int handleDetached(int event, void *data);

private:
};

#endif
