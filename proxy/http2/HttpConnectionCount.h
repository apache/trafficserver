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

// 

#ifndef _HTTP_CONNECTION_COUNT_H_

#define _BACKWARD_BACKWARD_WARNING_H    // needed for gcc 4.3
#include <ext/hash_map>
#undef _BACKWARD_BACKWARD_WARNING_H
// XXX - had to include map to get around "max" begin defined as a macro
// in the traffic server code, really odd
#include <map>
#include <ink_mutex.h>


using namespace __gnu_cxx;
using namespace std;


/**
 * Singleton class to keep track of the number of connections per host
 */
class ConnectionCount
{
public:
  /**
   * Static method to get the instance of the class
   * @return Returns a pointer to the instance of the class
   */
  static ConnectionCount *getInstance()
  {
    ink_mutex_acquire(&_mutex);
    if (_connectionCount == NULL) {
      _connectionCount = new ConnectionCount;
    }
    ink_mutex_release(&_mutex);

    return _connectionCount;
  }

  /**
   * Gets the number of connections for the host
   * @param ip IP address of the host
   * @return Number of connections
   */
  int getCount(const unsigned int ip)
  {
    ink_mutex_acquire(&_mutex);
    hash_map<unsigned int, int>::const_iterator it = _hostCount.find(ip);
    ink_mutex_release(&_mutex);

    if (it != _hostCount.end()) {
      return it->second;
    } else {
      return 0;
    }
  }

  /**
   * Change (increment/decrement) the connection count
   * @param ip IP address of the host
   * @param delta Default is +1, can be set to negative to decrement
   */
  void incrementCount(const unsigned int ip, const int delta = 1) {
    ink_mutex_acquire(&_mutex);
    hash_map<unsigned int, int>::iterator it = _hostCount.find(ip);
    if (it != _hostCount.end()) {
      it->second += delta;
    } else {
      _hostCount[ip] = delta;
    }
    ink_mutex_release(&_mutex);
  }

private:
  // Hide the constructor and copy constructor
  ConnectionCount() {
  }
  ConnectionCount(const ConnectionCount & x)
  {
  }

  static ConnectionCount *_connectionCount;
  hash_map<unsigned int, int>_hostCount;
  static ink_mutex _mutex;
};

#endif
