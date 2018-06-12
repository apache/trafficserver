#include "IPAddress.h"

#include "ts/remap.h"

#include <cstring>
#include <arpa/inet.h>

IPAddress :: IPAddress
  ( sockaddr const * const ipin
  )
  : sock(nullptr)
{

  if (nullptr != ipin)
  {
    this->sock = new sockaddr_storage;

    // Zero out memory
    memset(this->sock, 0, sizeof(sockaddr_storage));
    memset(this->charbuf, 0, sizeof(INET6_ADDRSTRLEN));

    if (AF_INET == ipin->sa_family)
    {
      sockaddr_in * const in4 = (sockaddr_in*)this->sock;
      memcpy(in4, ipin, sizeof(sockaddr_in));
      inet_ntop
        ( AF_INET
        , (void*)(&in4->sin_addr)
        , this->charbuf
        , INET_ADDRSTRLEN );
    }
    else
    if (AF_INET6 == ipin->sa_family)
    {
      sockaddr_in6 * const in6 = (sockaddr_in6*)this->sock;
      memcpy(in6, ipin, sizeof(sockaddr_in6));
      inet_ntop
        ( AF_INET6
        , (void*)(&in6->sin6_addr)
        , this->charbuf
        , INET6_ADDRSTRLEN );
    }
    else
    {
      delete this->sock;
      this->sock = nullptr;
    }
  }
}

IPAddress :: ~IPAddress()
{
  if (nullptr != sock)
  {
    delete sock;
    sock = nullptr;
  }
}

bool
IPAddress :: isValid
  () const
{
  return nullptr != sock;
}

const char *
IPAddress :: string
  () const
{
  return charbuf;
}

sockaddr_storage const *
IPAddress :: ip
  () const
{
  return sock;
}
