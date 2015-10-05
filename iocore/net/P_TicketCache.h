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

#ifndef IOCORE_NET_P_TICKETCACHE_H_
#define IOCORE_NET_P_TICKETCACHE_H_

#include <time.h>
#include <stdint.h>

#include <unordered_map>
#include <string>

/* ST_SESSION_TICKET_MAX_LENGTH: For most implementations this should be excessive,
                though it's a servers perogative, and max is technically not defined in protocol */
#define ST_SESSION_TICKET_MAX_LENGTH 1024

//----
/* ST_HOSTNAME_MAX_SIZE: this is just a threshold number for bucket collisions, and not meant
                                        to necessarily hold whole hostname */
#define ST_HOSTNAME_MAX_SIZE 32

// using namespace std;

class TicketCache
{
private:
  typedef struct {
    char hostname[ST_HOSTNAME_MAX_SIZE + 1]; /* hostname stored enough for bucket collision check */
    time_t expTime;                          /* current time of storage + expiration hint => when it expires */
    unsigned int ticketLength;
    unsigned char ticket[ST_SESSION_TICKET_MAX_LENGTH];
  } SessionTicket;

  std::unordered_map<std::string, SessionTicket *> cache;

  void clear(SessionTicket *s);
  void save(SessionTicket *s, const char *hostname, time_t expTime, const unsigned char *ticket, unsigned int ticketLength);
  bool enabled;
  pthread_mutex_t ticket_cache_mutex;

public:
  TicketCache(bool enable);
  ~TicketCache();

  void enableCache(bool enable);
  int lookup(const char *hostname, unsigned char *ticketBuff, unsigned int ticketBuffSize);
  void store(const char *hostname, uint64_t expireHint, const unsigned char *ticket, unsigned int ticketLength);
};
#endif
