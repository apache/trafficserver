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
	memset(s, 0, sizeof(SessionTicket));
}

void TicketCache::save(SessionTicket *s, char *hostname, time_t expTime, unsigned char *ticket, unsigned int ticketLength)
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



int TicketCache::lookup(char *hostname, unsigned char *ticketBuff, unsigned int ticketBuffSize)
{
	/* Do we have a non-expired ticket for this hostname, then return it in the buffer provided */
	/* return value is the size of the ticket,  or 0 on none. */

	SessionTicket *s;

	if (! enabled) 
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

void TicketCache::store(char *hostname, uint64_t expireHint, unsigned char *ticket, unsigned int ticketLength)
{
	SessionTicket *s = NULL;

	if (! enabled) 
		return;
	
	Debug("ssl.ticket", "Storing session ticket for host \"%s\", length=%d bytes, expireHint=%d",hostname,
			ticketLength, (int) expireHint);

	if (ticketLength > ST_SESSION_TICKET_MAX_LENGTH) {

		/* we don't dynamically allocate to size for speed, assuming all legit tickets should
			be within a certain size.  If we trip here often for a legit site, may consider
                        adjusting size.   As ticket size is only define in a server implementation , there is
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

