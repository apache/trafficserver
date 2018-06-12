#pragma once

#include "ts/ts.h"

#include <arpa/inet.h>
#include <netinet/in.h>

struct IPAddress
{
  IPAddress(IPAddress const &) = delete; 
  IPAddress & operator=(IPAddress const &) = delete; 

  explicit
  IPAddress
    ( sockaddr const * const ipin
    );

  ~IPAddress();

  char const * string() const;
  sockaddr_storage const * ip() const;

  bool isValid() const;

private:

  char charbuf[INET6_ADDRSTRLEN];
  sockaddr_storage * sock;
};
