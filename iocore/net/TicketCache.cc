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

/**************************************************************************
  TicketCache.cc
   Created On      : 06/2015

   Description:
	Session Ticket Overivew: 

   	TLS session tickets is a mechanism to allow clients and servers to 
	reconnect with abbreviated TLS handshakes, saving network round trips,
	and expensive computation required in key generation/exchange and authentication.
	While it's role is very similar to SSL/TLS Session ID, it's implementation
	and limitations are very different.  SSL/TLS Session ID are cached on both client and 
	server side though are awkward at best for  multiple servers behind a VIP or similar as 
	a bank of TLS servers must immediately share client connection information amongst 
	themselves for Session ID resumption to work.
	Session tickets are cached only on the client side, with no requirements of 
	storage or sharing on server side for resumption.  The ticket presented by client 
	in client-hello contains all of the necessary information to resume a previous 
	connection with abreviated handshake.

	TLS Session tickets require TLS-extensions, which means they are only available
	with TLS 1.0+.   Session ID's have been around since SSL 2.0.   This tidbit of info
	is relevant to understanding limitations of current OpenSSL API implementation.


	Module Description:

	This module contains the session ticket cache storage for ATS when acting as a client
	connecting to an origin server over TLS.

	General operation works like this: ATS initiates TLS connection to origin server,
	in preparation for initial clien-hello, a TicketCache.lookup() to cache is performed to see if 
	we have a session ticket for the given hostname.  If so, it's added to the TLS-extension
	of the Client-Hello message.    Upon completion of TLS handshake, the server will have sent a
	TLS session-ticket.  If it does, then TicketCache.store() is called for later retrieval.


 ****************************************************************************/

#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include <string.h>

#include <unordered_map>
#include <iostream>
#include <string>

#include "P_TicketCache.h"
#include "P_Net.h"

//-----------------------------------------------------------------------------------------

void TicketCache::clear(SessionTicket *s)
{
	ink_zero(*s);
}

void TicketCache::save(SessionTicket *s, const char *hostname, time_t expTime, const unsigned char *ticket, unsigned int ticketLength)
{

	if (ticketLength > ST_SESSION_TICKET_MAX_LENGTH) { 
		return; // reject 
		}

	clear(s);
	memcpy(s->ticket, ticket, ticketLength);
        s->ticketLength = ticketLength;
        s->expTime = expTime + time(NULL);;
	strncpy(s->hostname, hostname, ST_HOSTNAME_MAX_SIZE);
	s->hostname[ST_HOSTNAME_MAX_SIZE-1]='\0';

	Debug("ssl.ticket","save ticket for %s: expire hint time: %d, currentTime: %d, new expTime: %d\n", 
		hostname, (int) expTime, (int) time(NULL), (int) s->expTime);
}

void TicketCache::enableCache(bool enable)
{
	enabled = enable;
}


int TicketCache::lookup(const char *hostname, unsigned char *ticketBuff, unsigned int ticketBuffSize)
{
	/* Do we have a non-expired ticket for this hostname, then return it in the buffer provided */
	/* return value is the size of the ticket,  or 0 on none. */

	SessionTicket *s;

	if (! enabled) 
		return 0;

	if (!hostname)
		return 0;

	if (NULL == (s = cache[hostname])) {
		Debug("ssl.ticket","ticket lookup failed(1) no entry for host %s\n",hostname);
		return 0; // didn't find it
	}

	if (strncmp(hostname, s->hostname, ST_HOSTNAME_MAX_SIZE)) {
		Debug("ssl.ticket","ticket lookup failed(2), comparing %s with %s, max %d\n",
				hostname, s->hostname, ST_HOSTNAME_MAX_SIZE);
		return 0; // didn't find it
	}
	
	Debug("ssl.ticket","ticket lookup success, %s: s->expTime is %d, time is %d\n",
			s->hostname, (int) s->expTime, (int) time(NULL));
	if (s->expTime < time(NULL)) {
		/* entry expired, clear cache entry and return notfound */
		Debug("ssl.ticket","though ticket expired, %s==%s: expTime is %d, time is %d, s->ticketLength=%d\n", 
			hostname, s->hostname, (int) s->expTime, (int) time(NULL),s->ticketLength);
		clear(s);
		return 0; // expired
	}


	if (s->ticketLength > ticketBuffSize) {
		Debug("ssl.ticket","Ticket too large for buff, dropping.");
		// Debug("Return Ticket buff size is too small");
		return 0; //
	}
	else  {
		memcpy(ticketBuff, s->ticket, s->ticketLength);
		return s->ticketLength;
	}

}

void TicketCache::store(const char *hostname, uint64_t expireHint, const unsigned char *ticket, unsigned int ticketLength)
{
	SessionTicket *s = NULL;

	if (! enabled) 
		return;

	if (!hostname)
		return;
	
	Debug("ssl.ticket", "Storing session ticket for host \"%s\", length=%d bytes, expireHint=%d",hostname,
			ticketLength, (int) expireHint);

	if (ticketLength > ST_SESSION_TICKET_MAX_LENGTH) {

		/* we don't dynamically allocate to size for speed, assuming all legit tickets should
			be within a certain size.  If we trip here often for a legit site, may consider
                        adjusting size.   As ticket size is only defined in a server implementation , there is
                        no way to know for sure what the cap is, though it's reasonable to assume legit
			servers would optimize for size.  */
                /* ST_SESSION_TICKET_MAX_LENGTH is large enough by a wide margin that this should never 
 		happen, unless we're being abused by a baddy, or found a poorly implemented server. We'll 
		drop storing it here, and log that we rejected it. */

		Debug("ssl.ticket", "Not caching oversized session ticket (%d bytes).  Max we store is %d bytes",
				ticketLength, ST_SESSION_TICKET_MAX_LENGTH);
		return; 
	}

	if (NULL == (s = cache[hostname])) {
        if (!(s = (SessionTicket *) ats_malloc(sizeof(SessionTicket)))) {
                return;
		}
		save(s, hostname, (time_t) expireHint , ticket, ticketLength);
		cache[hostname] = s;
	}
	else {
		/* overwrite existing entry */
		save(s, hostname, (time_t) expireHint , ticket, ticketLength);
	}

	return;
}



TicketCache::TicketCache(bool enable)
{

	enabled = enable;

	Debug("ssl.ticket", "Initializing session ticket cache");
	cache.max_load_factor(1.0);

	return;
}


TicketCache::~TicketCache()
{
SessionTicket *s;

	/* walk the buckets to clear and deallocate all entries.*/
	for (auto it = cache.begin(); it != cache.end(); it++) {
		s = it->second;
		if (!s) {
			clear(s);
			ats_free(s);
		}
	}
	cache.clear();
}

