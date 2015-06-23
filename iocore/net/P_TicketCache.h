#ifndef TICKETCAHE_BEEN_INCLUDED_BEFORE
#define TICKETCAHE_BEEN_INCLUDED_BEFORE

#include <time.h>
#include <stdint.h>

#include <unordered_map>
#include <string>

/* ST_SESSION_TICKET_MAX_LENGTH: For most implementations this should be excessive, 
                though it's a servers perogative, and max is technically not defined in protocol */
#define ST_SESSION_TICKET_MAX_LENGTH  1024 

//----
/* ST_HOSTNAME_MAX_SIZE: this is just a threshold number for bucket collisions, and not meant 
                                        to necessarily hold whole hostname */
#define ST_HOSTNAME_MAX_SIZE    32 

//using namespace std;

class TicketCache {

private:
	typedef struct {
	    char hostname[ST_HOSTNAME_MAX_SIZE+1]; /* hostname stored enough for bucket collision check */
            time_t expTime; /* current time of storage + expiration hint => when it expires */
            unsigned int ticketLength;
            unsigned char ticket[ST_SESSION_TICKET_MAX_LENGTH];
        } SessionTicket;

        std::unordered_map<std::string, SessionTicket *> cache;

	void clear(SessionTicket *s);
	void save(SessionTicket *s, char *hostname, time_t expTime, unsigned char *ticket, unsigned int ticketLength);
	bool enabled;

public:

	TicketCache(bool enable);
	~TicketCache();

	int lookup(char *hostname, unsigned char *ticketBuff, unsigned int ticketBuffSize);
	void store(char *hostname, uint64_t expireHint, unsigned char *ticket, unsigned int ticketLength);
};
#endif
