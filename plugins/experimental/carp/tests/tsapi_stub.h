/////////////////////////////////////////////////////////////////////////
// This file contains stub routines that need need to exist in order to
// do some level of unit testing.  We can't use the "real" INK routines
// since they are not in a library.  Since our plugin is a library that
// contains both low level IO routins as well as the plugin "glue" needed
// to make them work, we'll have refereces to some of these routines
// even if we want to only unit test our low level routines.
/////////////////////////////////////////////////////////////////////////

#include <ts/ts.h>
#include <ts/remap.h>

#include <map>
#include <vector>
#include <string>
#include <iostream>
#include <arpa/inet.h>

struct UrlStruct
{
  std::string scheme;
  unsigned port;
  std::string host;
  std::string path;
  std::string query;
  std::string params;  // matrix params
  std::string url;
  
  void dump()
  {
    std::cerr << "UrlStruct[" << std::endl;
    std::cerr << "  scheme:"<<scheme<<std::endl;
    std::cerr << "  host:"<<host<<std::endl;
    std::cerr << "  port:"<<port<<std::endl;
    std::cerr << "  path:"<<path<<std::endl;
    std::cerr << "  query:"<<query<<std::endl;
    std::cerr << "  params:"<<params<<std::endl;
    std::cerr << "  url:"<<url<<std::endl;
    std::cerr << "]UrlStruct" << std::endl;
  }
};

typedef std::map<std::string, std::string> Headers;

struct TSMBufferStruct
{
  std::string method;
  UrlStruct url;
  Headers clientReqHeaders;
  std::string body;
  
  void dump()
  {
    std::cerr << "method=[" << method << "]method" << std::endl;
    url.dump();
    std::map<std::string, std::string>::iterator it;
    std::cerr << "clientReqHeaders=["<<std::endl;
    for(it=clientReqHeaders.begin();it!=clientReqHeaders.end();it++)
      std::cerr << it->first << ":" << it->second << std::endl;
    std::cerr << "]clientReqHeaders"<<std::endl;
    std::cerr << "body=[" << body << "]body" << std::endl;
  }
};

struct TxnStruct
{
   TSMBufferStruct clientRequest;
   TSMBufferStruct clientResponse;
   sockaddr incomingClientAddr;
   void dump() 
   { 
     std::cerr << "TxnStruct@"<<this<<"["<<std::endl;
     
     std::cerr << "clientRequest["<<std::endl;
     clientRequest.dump();
     std::cerr << "]clientRequest"<<std::endl;
     
     std::cerr << "clientResponse["<<std::endl;
     clientResponse.dump();
     std::cerr << "]clientResponse"<<std::endl;
     
     sockaddr_in* sa = (sockaddr_in*)&incomingClientAddr;
     sockaddr_in6* sa6 = (sockaddr_in6*)&incomingClientAddr;
     char out[INET6_ADDRSTRLEN];
    std::cerr << "incomingClientAddr[";
    switch (sa->sin_family) {
    case AF_INET: std::cerr << "AF_INET," << inet_ntop(AF_INET, (void *)&(sa->sin_addr), out, sizeof (out)) << ":" << ntohs(sa->sin_port) << std::endl;
      break;
    case AF_INET6: std::cerr << "AF_INET6," << inet_ntop(AF_INET6, (void *)&(sa6->sin6_addr), out, sizeof (out)) << ":" << ntohs(sa6->sin6_port) << std::endl;
      break;
    default: std::cerr << "unknown";
      break;
    }
     std::cerr << "]incomingClientAddr"<<std::endl;

     std::cerr << "]TxnStruct"<<std::endl;
   }
};


void EnableTSDebug(bool b);

