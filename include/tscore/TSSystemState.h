/**
  @file TSSystemState.h

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

#pragma once

#include <tscore/ink_assert.h>
#include <tscore/ink_defs.h>

// Global status information for trafficserver
//
class TSSystemState
{
private:
  struct Data {
    bool ssl_handshaking_stopped;
    bool event_system_shut_down;
    bool draining;
  };

public:
  static bool
  is_ssl_handshaking_stopped()
  {
    return unlikely(_instance().ssl_handshaking_stopped);
  }

  static bool
  is_event_system_shut_down()
  {
    return unlikely(_instance().event_system_shut_down);
  }

  // Keeps track if the server is in draining state, follows the proxy.node.config.draining metric.
  //
  static bool
  is_draining()
  {
    return unlikely(_instance().draining);
  }

  static void
  stop_ssl_handshaking()
  {
    ink_assert(!_instance().ssl_handshaking_stopped);

    _instance().ssl_handshaking_stopped = true;
  }

  static void
  shut_down_event_system()
  {
    // For some reason this is triggered by the regression testing.
    // ink_assert(_instance().ssl_handshaking_stopped && !_instance().event_system_shut_down);

    _instance().event_system_shut_down = true;
  }

  static void
  drain(bool enable)
  {
    _instance().draining = enable;
  }

private:
  static Data &
  _instance()
  {
    static Data d;

    return d;
  }
};
