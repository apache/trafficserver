//
// Test Cass
//

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

#include <vector>
#include <netdb.h>

#include "tsapi_stub.h"

#include "Common.h"
#include "CarpConfigPool.h"

extern CarpConfigPool* g_CarpConfigPool;
extern int g_selectedHostArgIndex;
void TSPluginInit(int argc , const char * argv[] ) ;
int  carpPluginHook(TSCont contp, TSEvent event, void *edata);

using namespace std;

class Testcarp : public CppUnit::TestFixture
{
    CPPUNIT_TEST_SUITE( Testcarp );
    CPPUNIT_TEST( testTSPluginInit );
    CPPUNIT_TEST( testcarpPluginHook1 );
    CPPUNIT_TEST( testcarpPluginHook2 );
    CPPUNIT_TEST( testcarpPluginHook3 );
    CPPUNIT_TEST( testcarpPluginHook4 );
    CPPUNIT_TEST( testcarpPluginHook4_PURGE );
    CPPUNIT_TEST( testcarpPluginHook4_DELETE );
    CPPUNIT_TEST( testcarpPluginHook5 );
    CPPUNIT_TEST( testcarpPluginHook6 );
    CPPUNIT_TEST( testcarpPluginHook_Schemes_Preremap );
    CPPUNIT_TEST( testcarpPluginHook_Schemes_Postremap );
    CPPUNIT_TEST( testcarpPluginHook_Preremap_Schemes );

    
    CPPUNIT_TEST_SUITE_END();                                                     

public:

protected:
  void testTSPluginInit( void );   
  void testcarpPluginHook1( void );   
  void testcarpPluginHook2( void );   
  void testcarpPluginHook3( void ); 
  void testcarpPluginHook4( void ); 
  void testcarpPluginHook4_PURGE( void );
  void testcarpPluginHook4_DELETE( void );
  void testcarpPluginHook5( void ); 
  void testcarpPluginHook6( void );
  void testcarpPluginHook_Schemes_Preremap( void );
  void testcarpPluginHook_Schemes_Postremap( void );
  void testcarpPluginHook_Preremap_Schemes( void );

  
private:
  void createTestConfig(string& filename, bool bWhitelist, bool bBlacklist, bool bPostremap, bool bLongline, bool bHttps);
  void cleanup(string& filename );

};

// ======================================================================
void
Testcarp::createTestConfig(string& filename, bool bWhitelist, bool bBlacklist, bool bPostremap, bool bLongline, bool bHttps)
{
  string serversa(
                "[Servers]\r\n"
                "www.yahoo.com:4080 weight=2\r\n"
                "host2.cacheservice.com  \r\n"
                "www.yahoo2.com 4080 weight=2\r\n"
                "www.yahoo3.com:4080 weight=\r\n"
                );
  string serversb(
                "[Servers]\r\n"
                "www.yahoo.com:443 weight=2\r\n"
                "host2.cacheservice.com:443  \r\n"
                "www.yahoo2.com 4080 weight=2\r\n"
                "www.yahoo3.com:443 weight=\r\n"
                "https://www.yahoo4.com weight=2\r\n"
                "https://www.yahoo5.com:443 weight=2 \r\n"
                "https://www.yahoo7.com:4443 weight=2 \r\n"
                );
  string valuesa(
                "[Values]\r\n"
                "healthcheck=http://healthcheck.cacheservice.com:8080/status.html\r\n"
                "healthfreq=30\r\n"
                "allowfwdport=81\r\n"
               );
  string blacklist("blacklist=healthcheck.cacheservice.com,hc.cacheservice.com\r\n");
  string whitelist("whitelist=white.com\r\n");
  string postremap("mode=post-remap\r\n");
  string preremap("mode=pre-remap\r\n");
  string longline(
              // 1234567890123456789012345678901234567890123456789012345678901234567890
                "need a really long line (over 1024 bytes) and this is the one........." // 70
                "need a really long line (over 1024 bytes) and this is the one........." // 140
                "need a really long line (over 1024 bytes) and this is the one........." // 210
                "need a really long line (over 1024 bytes) and this is the one........." // 280
                "need a really long line (over 1024 bytes) and this is the one........." // 350
                "need a really long line (over 1024 bytes) and this is the one........." // 420
                "need a really long line (over 1024 bytes) and this is the one........." // 490
                "need a really long line (over 1024 bytes) and this is the one........." // 560
                "need a really long line (over 1024 bytes) and this is the one........." // 630
                "need a really long line (over 1024 bytes) and this is the one........." // 700
                "need a really long line (over 1024 bytes) and this is the one........." // 770
                "need a really long line (over 1024 bytes) and this is the one........." // 840
                "need a really long line (over 1024 bytes) and this is the one........." // 910
                "need a really long line (over 1024 bytes) and this is the one........." // 980
                "need a really long line (over 1024 bytes) and this is the one.........\n" // 1050
                "\r\n"
                );

  string config;
  
  if(!bHttps) config = serversa; else config = serversb;

  config += valuesa;

  if(bWhitelist) config += whitelist;
  if(bBlacklist) config += blacklist;
  if(bPostremap) config += postremap; else config += preremap;
  if(bLongline) config += longline;
    
  ofstream f;
  f.open(filename.c_str());
  f << config;
  f.close();
}

// ======================================================================
void
Testcarp::cleanup(string& filename)
{
  remove(filename.c_str());
}

// ======================================================================
void
Testcarp::testTSPluginInit(void)
{
  EnableTSDebug(false);
  string filename("test.config");
  createTestConfig(filename,true,true,true,true,false);
  
  const char *argv[2];
  argv[0] = "carp.so";
  argv[1] = filename.c_str();

  TSPluginInit(1 , argv ) ;
  CPPUNIT_ASSERT(g_CarpConfigPool);
  CPPUNIT_ASSERT(g_CarpConfigPool->getGlobalConfig() == NULL);
  CPPUNIT_ASSERT(g_CarpConfigPool->getGlobalHashAlgo() == NULL);
  
  TSPluginInit(2 , argv ) ;

  CPPUNIT_ASSERT(g_CarpConfigPool);
  CPPUNIT_ASSERT(g_CarpConfigPool->getGlobalConfig() != NULL);
  CPPUNIT_ASSERT(g_CarpConfigPool->getGlobalHashAlgo() != NULL);

  CarpConfig* c = g_CarpConfigPool->getGlobalConfig();
  HashAlgorithm* h = g_CarpConfigPool->getGlobalHashAlgo();
  
  CPPUNIT_ASSERT(c->getHealthCheckPort() == 8080);
  CPPUNIT_ASSERT(c->isBlackListed(string("hc.cacheservice.com")));
  CPPUNIT_ASSERT(c->isWhiteListed(string("white.com")));
  CPPUNIT_ASSERT(c->getMode() == CarpConfig::POST);
  CPPUNIT_ASSERT(c->getAllowedForwardPort() == 81);
  CPPUNIT_ASSERT(c->getHealthCheckUrl().compare("http://healthcheck.cacheservice.com:8080/status.html") == 0);
 
  CPPUNIT_ASSERT(h);

  cleanup(filename);
}

// ======================================================================
void
Testcarp::testcarpPluginHook1(void)
{
  TxnStruct txn;
  TSHttpTxn txnp = (TSHttpTxn)&txn;
  
  // setup the txn
  txn.clientRequest.method = TS_HTTP_METHOD_GET;
  txn.clientRequest.clientReqHeaders[TS_MIME_FIELD_HOST] = "white.com";
  txn.clientRequest.url.host = "white.com";
  txn.clientRequest.url.port = 80;
  txn.clientRequest.url.scheme = "http";
  txn.clientRequest.url.params = "";
  txn.clientRequest.url.path = "/a";
  txn.clientRequest.url.query = "";

  // put hosts online so hash will select one
  HashAlgorithm* h = g_CarpConfigPool->getGlobalHashAlgo();
  h->setStatus("www.yahoo.com",80,true,time(NULL),500);
  h->setStatus("host2.cacheservice.com",80,true,time(NULL),500);
  carpPluginHook(NULL, TS_EVENT_HTTP_READ_REQUEST_HDR, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_OS_DNS, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_SEND_RESPONSE_HDR, txnp);
  //CPPUNIT_ASSERT(txn.clientRequest.clientReqHeaders.find(CARP_FORWARD_HEADER) != txn.clientRequest.clientReqHeaders.end() );

}

// ======================================================================
void
Testcarp::testcarpPluginHook2(void)
{
  TxnStruct* txn = new TxnStruct;
  TSHttpTxn txnp = (TSHttpTxn)txn;
  
  // setup the txn
  txn->clientRequest.method = TS_HTTP_METHOD_GET;
  txn->clientRequest.clientReqHeaders[TS_MIME_FIELD_HOST] = "white.com";
  txn->clientRequest.clientReqHeaders[CARP_ROUTED_HEADER] = "1";
  txn->clientRequest.url.host = "white.com";
  txn->clientRequest.url.port = 80;
  txn->clientRequest.url.scheme = "http";
  txn->clientRequest.url.params = "";
  txn->clientRequest.url.path = "/a";
  txn->clientRequest.url.query = "";

  // put hosts online so hash will select one
  // should not route since routed header present
  carpPluginHook(NULL, TS_EVENT_HTTP_READ_REQUEST_HDR, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_OS_DNS, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_SEND_RESPONSE_HDR, txnp);
  //CPPUNIT_ASSERT(txn->clientRequest.clientReqHeaders.find(CARP_FORWARD_HEADER) == txn->clientRequest.clientReqHeaders.end() );

  delete txn;
}

// ======================================================================
void
Testcarp::testcarpPluginHook3(void)
{
  TxnStruct* txn = new TxnStruct;
  TSHttpTxn txnp = (TSHttpTxn)txn;
  
  // setup the txn
  txn->clientRequest.method = TS_HTTP_METHOD_GET;
  txn->clientRequest.clientReqHeaders[TS_MIME_FIELD_HOST] = "notwhite.com";
  txn->clientRequest.url.host = "notwhite.com";
  txn->clientRequest.url.port = 80;
  txn->clientRequest.url.scheme = "http";
  txn->clientRequest.url.params = "";
  txn->clientRequest.url.path = "/a";
  txn->clientRequest.url.query = "";
 
  // non-white list so no forwarding
  carpPluginHook(NULL, TS_EVENT_HTTP_READ_REQUEST_HDR, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_OS_DNS, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_SEND_RESPONSE_HDR, txnp);
  CPPUNIT_ASSERT(txn->clientRequest.clientReqHeaders.find(CARP_FORWARD_HEADER) == txn->clientRequest.clientReqHeaders.end() );

  // good carpable header
  txn->clientRequest.clientReqHeaders[CARPABLE_HEADER] = "1";
  txn->clientRequest.clientReqHeaders.erase(CARP_ROUTED_HEADER);
  carpPluginHook(NULL, TS_EVENT_HTTP_READ_REQUEST_HDR, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_OS_DNS, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_SEND_RESPONSE_HDR, txnp); 
  CPPUNIT_ASSERT(txn->clientRequest.clientReqHeaders.find(CARP_FORWARD_HEADER) != txn->clientRequest.clientReqHeaders.end() );

  // bad carpable header, so ignore and route
  txn->clientRequest.clientReqHeaders[CARPABLE_HEADER] = "2";
  txn->clientRequest.clientReqHeaders.erase(CARP_ROUTED_HEADER);
  carpPluginHook(NULL, TS_EVENT_HTTP_READ_REQUEST_HDR, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_OS_DNS, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_SEND_RESPONSE_HDR, txnp); 
  CPPUNIT_ASSERT(txn->clientRequest.clientReqHeaders.find(CARP_FORWARD_HEADER) != txn->clientRequest.clientReqHeaders.end() );

  delete txn;
}

// ======================================================================
void
Testcarp::testcarpPluginHook4(void)
{
  TxnStruct* txn = new TxnStruct;
  TSHttpTxn txnp = (TSHttpTxn)txn;
  
  // setup the txn
  txn->clientRequest.method = TS_HTTP_METHOD_GET;
  txn->clientRequest.clientReqHeaders[TS_MIME_FIELD_HOST] = "white.com";
  txn->clientRequest.clientReqHeaders[CARP_ROUTED_HEADER] = "2";
  txn->clientRequest.url.host = "white.com";
  txn->clientRequest.url.port = 80;
  txn->clientRequest.url.scheme = "http";
  txn->clientRequest.url.params = "";
  txn->clientRequest.url.path = "/testcarpPluginHook4";
  txn->clientRequest.url.query = "";

  // should remove invalid carp routed header 
  carpPluginHook(NULL, TS_EVENT_HTTP_READ_REQUEST_HDR, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_OS_DNS, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_SEND_RESPONSE_HDR, txnp);
  CPPUNIT_ASSERT(txn->clientRequest.clientReqHeaders.find(CARP_ROUTED_HEADER) != txn->clientRequest.clientReqHeaders.end() );
  CPPUNIT_ASSERT(txn->clientRequest.clientReqHeaders[CARP_ROUTED_HEADER] != "2" );

  txn->clientRequest.clientReqHeaders[CARP_ROUTED_HEADER] = "dump";
  //the line below causes core dump upon exit.  appears to be related to a
  // destructor but running under gdb is of no help
  //  carpPluginHook(NULL, TS_EVENT_HTTP_READ_REQUEST_HDR, txnp);
  delete txn;
 
  g_CarpConfigPool->getGlobalConfig()->stop();
}

// ======================================================================
void
Testcarp::testcarpPluginHook4_DELETE(void)
{
  TxnStruct* txn = new TxnStruct;
  TSHttpTxn txnp = (TSHttpTxn)txn;

  // setup the txn
  txn->clientRequest.method = TS_HTTP_METHOD_DELETE;
  txn->clientRequest.clientReqHeaders[TS_MIME_FIELD_HOST] = "white.com";
  txn->clientRequest.url.host = "white.com";
  txn->clientRequest.url.port = 80;
  txn->clientRequest.url.scheme = "http";
  txn->clientRequest.url.params = "";
  txn->clientRequest.url.path = "/a";
  txn->clientRequest.url.query = "";

  // put hosts online so hash will select one
  // should not route since routed header present
  carpPluginHook(NULL, TS_EVENT_HTTP_READ_REQUEST_HDR, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_OS_DNS, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_SEND_RESPONSE_HDR, txnp);
  // since method it DELETE, should not contain either of these
  CPPUNIT_ASSERT(txn->clientRequest.clientReqHeaders.find(CARP_ROUTED_HEADER) == txn->clientRequest.clientReqHeaders.end() );
  CPPUNIT_ASSERT(txn->clientRequest.clientReqHeaders.find(CARP_FORWARD_HEADER) == txn->clientRequest.clientReqHeaders.end() );
  delete txn;
}

// ======================================================================
void
Testcarp::testcarpPluginHook4_PURGE(void)
{
  TxnStruct* txn = new TxnStruct;
  TSHttpTxn txnp = (TSHttpTxn)txn;

  // setup the txn
  txn->clientRequest.method = TS_HTTP_METHOD_PURGE;
  txn->clientRequest.clientReqHeaders[TS_MIME_FIELD_HOST] = "white.com";
  txn->clientRequest.url.host = "white.com";
  txn->clientRequest.url.port = 80;
  txn->clientRequest.url.scheme = "http";
  txn->clientRequest.url.params = "";
  txn->clientRequest.url.path = "/a";
  txn->clientRequest.url.query = "";

  // put hosts online so hash will select one
  // should not route since routed header present
  carpPluginHook(NULL, TS_EVENT_HTTP_READ_REQUEST_HDR, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_OS_DNS, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_SEND_RESPONSE_HDR, txnp);
  // since method it PURGE, should not contain either of these
  CPPUNIT_ASSERT(txn->clientRequest.clientReqHeaders.find(CARP_ROUTED_HEADER) == txn->clientRequest.clientReqHeaders.end() );
  CPPUNIT_ASSERT(txn->clientRequest.clientReqHeaders.find(CARP_FORWARD_HEADER) == txn->clientRequest.clientReqHeaders.end() );
  delete txn;
}

//=================================
void
Testcarp::testcarpPluginHook_Preremap_Schemes()
{
  //setup new config
  g_CarpConfigPool = NULL; // forget old config
  string filename("test.config");
  createTestConfig(filename, false, true, false, false,false);
  
  const char *argv[2];
  argv[0] = "carp.so";
  argv[1] = filename.c_str();
  
  TSPluginInit(2, argv);
  CPPUNIT_ASSERT(g_CarpConfigPool);
  CPPUNIT_ASSERT(g_CarpConfigPool->getGlobalConfig() != NULL);
  CPPUNIT_ASSERT(g_CarpConfigPool->getGlobalHashAlgo() != NULL);
  
  HashAlgorithm* h = g_CarpConfigPool->getGlobalHashAlgo();

  TxnStruct* txn = new TxnStruct;
  TSHttpTxn txnp = (TSHttpTxn)txn;

  // setup the txn
  txn->clientRequest.method = TS_HTTP_METHOD_GET;
  txn->clientRequest.clientReqHeaders[TS_MIME_FIELD_HOST] = "yahoo1.com";
  txn->clientRequest.url.host = "yahoo1.com";
  txn->clientRequest.url.port = 80;
  txn->clientRequest.url.scheme = "http";
  txn->clientRequest.url.params = "";
  txn->clientRequest.url.path = "/a";
  txn->clientRequest.url.query = "";

  h->setStatus("www.yahoo.com",80,true,time(NULL),500);
  h->setStatus("host2.cacheservice.com",80,true,time(NULL),500);
  // put hosts online so hash will select one
  // should not route since routed header present
  carpPluginHook(NULL, TS_EVENT_HTTP_READ_REQUEST_HDR, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_OS_DNS, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_SEND_RESPONSE_HDR, txnp);
  txn->clientRequest.dump();
  CPPUNIT_ASSERT(txn->clientRequest.clientReqHeaders.find(CARP_PREMAP_SCHEME) != txn->clientRequest.clientReqHeaders.end() );
  delete txn;


  //setup new config
  g_CarpConfigPool = NULL; // forget old config
  createTestConfig(filename, false, true, false, false,false);
  
  argv[0] = "carp.so";
  argv[1] = filename.c_str();
  
  TSPluginInit(2, argv);
  CPPUNIT_ASSERT(g_CarpConfigPool);
  CPPUNIT_ASSERT(g_CarpConfigPool->getGlobalConfig() != NULL);
  CPPUNIT_ASSERT(g_CarpConfigPool->getGlobalHashAlgo() != NULL);
  
  h = g_CarpConfigPool->getGlobalHashAlgo();

  txn = new TxnStruct;
  txnp = (TSHttpTxn)txn;

  // setup the txn
  txn->clientRequest.method = TS_HTTP_METHOD_GET;
  txn->clientRequest.clientReqHeaders[TS_MIME_FIELD_HOST] = "yahoo1.com:443";
  txn->clientRequest.clientReqHeaders[CARP_PREMAP_SCHEME] = "https";
  txn->clientRequest.clientReqHeaders[CARP_ROUTED_HEADER] = "1";
  txn->clientRequest.url.host = "yahoo1.com";
  txn->clientRequest.url.port = 80;
  txn->clientRequest.url.scheme = "http";
  txn->clientRequest.url.params = "";
  txn->clientRequest.url.path = "/a";
  txn->clientRequest.url.query = "";

  h->setStatus("www.yahoo.com",80,true,time(NULL),500);
  h->setStatus("host2.cacheservice.com",80,true,time(NULL),500);
  // put hosts online so hash will select one
  // should not route since routed header present
  carpPluginHook(NULL, TS_EVENT_HTTP_READ_REQUEST_HDR, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_OS_DNS, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_SEND_RESPONSE_HDR, txnp);
  txn->clientRequest.dump();
  //CPPUNIT_ASSERT(txn->clientRequest.url.scheme.find("https") != string::npos);
  //CPPUNIT_ASSERT(txn->clientRequest.url.port == 443);
  delete txn;
}
// ======================================================================
void
Testcarp::testcarpPluginHook5(void)
{
  // setup new config
  g_CarpConfigPool = NULL; // forget old config
  string filename("test.config");
  createTestConfig(filename, false, true, true, false,false);

  const char *argv[2];
  argv[0] = "carp.so";
  argv[1] = filename.c_str();

  TSPluginInit(2, argv);
  CPPUNIT_ASSERT(g_CarpConfigPool);
  CPPUNIT_ASSERT(g_CarpConfigPool->getGlobalConfig() != NULL);
  CPPUNIT_ASSERT(g_CarpConfigPool->getGlobalHashAlgo() != NULL);

  CarpConfig* c = g_CarpConfigPool->getGlobalConfig();
  HashAlgorithm* h = g_CarpConfigPool->getGlobalHashAlgo();

  CPPUNIT_ASSERT(c->isBlackListed(string("hc.cacheservice.com")));
  CPPUNIT_ASSERT(h);

  cleanup(filename);

  // test against new config
  TxnStruct* txn = new TxnStruct;
  TSHttpTxn txnp = (TSHttpTxn) txn;

  // setup the txn
  txn->clientRequest.method = TS_HTTP_METHOD_GET;
  txn->clientRequest.clientReqHeaders[TS_MIME_FIELD_HOST] = "hc.cacheservice.com";
  txn->clientRequest.url.host = "hc.cacheservice.com";
  txn->clientRequest.url.port = 80;
  txn->clientRequest.url.scheme = "http";
  txn->clientRequest.url.params = "";
  txn->clientRequest.url.path = "/a";
  txn->clientRequest.url.query = "";

  // put hosts online so hash will select one
  h->setStatus("www.yahoo.com",80,true,time(NULL),500);
  h->setStatus("host2.cacheservice.com",80,true,time(NULL),500);

  // should be black listed/not forward  

  carpPluginHook(NULL, TS_EVENT_HTTP_READ_REQUEST_HDR, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_OS_DNS, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_SEND_RESPONSE_HDR, txnp); 
  CPPUNIT_ASSERT(txn->clientRequest.clientReqHeaders.find(CARP_ROUTED_HEADER) == txn->clientRequest.clientReqHeaders.end() );
  CPPUNIT_ASSERT(txn->clientRequest.clientReqHeaders.find(CARP_FORWARD_HEADER) == txn->clientRequest.clientReqHeaders.end() );
  
  
  // make valid request that will get forward header added
  txn->clientRequest.clientReqHeaders[TS_MIME_FIELD_HOST] = "anynonblacklistedhost.com";
  txn->clientRequest.url.host = "anynonblacklistedhost.com";
  carpPluginHook(NULL, TS_EVENT_HTTP_READ_REQUEST_HDR, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_OS_DNS, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_SEND_RESPONSE_HDR, txnp); 
  //CPPUNIT_ASSERT(txn->clientRequest.clientReqHeaders.find(CARP_ROUTED_HEADER) != txn->clientRequest.clientReqHeaders.end() );
  //CPPUNIT_ASSERT(txn->clientRequest.clientReqHeaders.find(CARP_FORWARD_HEADER) != txn->clientRequest.clientReqHeaders.end() );

  // make carp forward the request and remove the fowrard header 
  sockaddr_in* sa = (sockaddr_in*)&(txn->incomingClientAddr);
  sa->sin_family = AF_INET;
  sa->sin_addr.s_addr = htonl(0x04030201);
  sa->sin_port = htons(81); // the allowfwdport

  carpPluginHook(NULL, TS_EVENT_HTTP_READ_REQUEST_HDR, txnp);
  //CPPUNIT_ASSERT(txn->clientRequest.clientReqHeaders.find(CARP_ROUTED_HEADER) != txn->clientRequest.clientReqHeaders.end() );
  //CPPUNIT_ASSERT(txn->clientRequest.clientReqHeaders.find(CARP_FORWARD_HEADER) == txn->clientRequest.clientReqHeaders.end() );

  delete txn;
}

// ======================================================================
void
Testcarp::testcarpPluginHook6(void)
{
  // setup new config
  g_CarpConfigPool = NULL; // forget old config
   string filename("test.config");
   createTestConfig(filename,  false, true, false, false,false);

   const char *argv[2];
  argv[0] = "carp.so";
  argv[1] = filename.c_str();

  TSPluginInit(2 , argv ) ;
  CPPUNIT_ASSERT(g_CarpConfigPool);
  CPPUNIT_ASSERT(g_CarpConfigPool->getGlobalConfig() != NULL);
  CPPUNIT_ASSERT(g_CarpConfigPool->getGlobalHashAlgo() != NULL);

  CarpConfig* c = g_CarpConfigPool->getGlobalConfig();
  HashAlgorithm* h = g_CarpConfigPool->getGlobalHashAlgo();
  
  CPPUNIT_ASSERT(c->isBlackListed(string("hc.cacheservice.com")));
  CPPUNIT_ASSERT(h);

  cleanup(filename);
  
  // test against new config
  TxnStruct* txn = new TxnStruct;
  TSHttpTxn txnp = (TSHttpTxn)txn;
  
  // setup the txn
// no host header get's us an extra line ;)
  txn->clientRequest.method = TS_HTTP_METHOD_GET;
  txn->clientRequest.url.port = 80;
  txn->clientRequest.url.scheme = "http";
  txn->clientRequest.url.params = "";
  txn->clientRequest.url.path = "/a";
  txn->clientRequest.url.query = "";

  // put hosts online so hash will select one
  h->setStatus("www.yahoo.com",80,true,time(NULL),500);
  h->setStatus("host2.cacheservice.com",80,true,time(NULL),500);

  // make valid request that will get processed pre-remap
  carpPluginHook(NULL, TS_EVENT_HTTP_READ_REQUEST_HDR, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_OS_DNS, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_SEND_RESPONSE_HDR, txnp); 
  //CPPUNIT_ASSERT(txn->clientRequest.clientReqHeaders.find(CARP_ROUTED_HEADER) != txn->clientRequest.clientReqHeaders.end() );
  delete txn;
}

// ======================================================================
void
Testcarp::testcarpPluginHook_Schemes_Preremap(void)
{
  // setup new config
  g_CarpConfigPool = NULL; // forget old config
  string filename("test.config");
  createTestConfig(filename, false, true, false, false, false); // pre-remap

  const char *argv[2];
  argv[0] = "carp.so";
  argv[1] = filename.c_str();

  TSPluginInit(2, argv);
  CPPUNIT_ASSERT(g_CarpConfigPool);
  CPPUNIT_ASSERT(g_CarpConfigPool->getGlobalConfig() != NULL);
  CPPUNIT_ASSERT(g_CarpConfigPool->getGlobalHashAlgo() != NULL);

  CarpConfig* c = g_CarpConfigPool->getGlobalConfig();
  HashAlgorithm* h = g_CarpConfigPool->getGlobalHashAlgo();

  CPPUNIT_ASSERT(c->isBlackListed(string("hc.cacheservice.com")));
  CPPUNIT_ASSERT(h);

  cleanup(filename);

  // test against new config
  TxnStruct* txn = new TxnStruct;
  TSHttpTxn txnp = (TSHttpTxn)txn;

  // setup the txn HTTP
// no host header get's us an extra line ;)
  txn->clientRequest.method = TS_HTTP_METHOD_GET;
  txn->clientRequest.url.port = 80;
  txn->clientRequest.url.scheme = "https";
  txn->clientRequest.url.params = "";
  txn->clientRequest.url.path = "/a";
  txn->clientRequest.url.query = "";

  // put hosts online so hash will select one
  h->setStatus("www.yahoo.com",80,true,time(NULL),500);
  h->setStatus("host2.cacheservice.com",80,true,time(NULL),500);

  // make valid request that will get processed pre-remap
  carpPluginHook(NULL, TS_EVENT_HTTP_READ_REQUEST_HDR, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_OS_DNS, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_SEND_RESPONSE_HDR, txnp);
  //CPPUNIT_ASSERT(txn->clientRequest.clientReqHeaders.find(CARP_ROUTED_HEADER) != txn->clientRequest.clientReqHeaders.end() );
  //CPPUNIT_ASSERT(txn->clientRequest.url.scheme.find("https") == string::npos); // should reflect scheme to peer
  txn->dump();
  delete txn;
  
  // Change carp peer to https
    // setup new config
  g_CarpConfigPool = NULL; // forget old config
  createTestConfig(filename, false, true, false, false, true); // pre-remap & https peer

  argv[0] = "carp.so";
  argv[1] = filename.c_str();

  TSPluginInit(2, argv);
  CPPUNIT_ASSERT(g_CarpConfigPool);
  CPPUNIT_ASSERT(g_CarpConfigPool->getGlobalConfig() != NULL);
  CPPUNIT_ASSERT(g_CarpConfigPool->getGlobalHashAlgo() != NULL);

  c = g_CarpConfigPool->getGlobalConfig();
  h = g_CarpConfigPool->getGlobalHashAlgo();

  CPPUNIT_ASSERT(c->isBlackListed(string("hc.cacheservice.com")));
  CPPUNIT_ASSERT(h);

  cleanup(filename);

  // test against new config
  txnp = (TSHttpTxn)(txn = new TxnStruct);

    // setup the txn HTTP
// no host header get's us an extra line ;)
  txn->clientRequest.clientReqHeaders.clear();
  txn->clientRequest.method = TS_HTTP_METHOD_GET;
  txn->clientRequest.url.port = 80;
  txn->clientRequest.url.scheme = "http";
  txn->clientRequest.url.params = "";
  txn->clientRequest.url.path = "/a";
  txn->clientRequest.url.query = "";

    // put (SSL) hosts online so hash will select one
  h->setStatus("www.yahoo.com",443,true,time(NULL),500);
  h->setStatus("host2.cacheservice.com",443,true,time(NULL),500);

  // make valid request that will get processed pre-remap
  carpPluginHook(NULL, TS_EVENT_HTTP_READ_REQUEST_HDR, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_OS_DNS, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_SEND_RESPONSE_HDR, txnp);

  //CPPUNIT_ASSERT(txn->clientRequest.clientReqHeaders.find(CARP_ROUTED_HEADER) != txn->clientRequest.clientReqHeaders.end() );
  //CPPUNIT_ASSERT(txn->clientRequest.url.scheme.find("https") != string::npos); // should reflect scheme to peer

  delete txn;

  // Change carp peer to https which with new format
    // setup new config
  g_CarpConfigPool = NULL; // forget old config
  createTestConfig(filename, false, true, false, false, true); // pre-remap & https peer

  argv[0] = "carp.so";
  argv[1] = filename.c_str();

  TSPluginInit(2, argv);
  CPPUNIT_ASSERT(g_CarpConfigPool);
  CPPUNIT_ASSERT(g_CarpConfigPool->getGlobalConfig() != NULL);
  CPPUNIT_ASSERT(g_CarpConfigPool->getGlobalHashAlgo() != NULL);

  c = g_CarpConfigPool->getGlobalConfig();
  h = g_CarpConfigPool->getGlobalHashAlgo();

  CPPUNIT_ASSERT(c->isBlackListed(string("hc.cacheservice.com")));
  CPPUNIT_ASSERT(h);

  cleanup(filename);

  // test against new config
  txnp = (TSHttpTxn)(txn = new TxnStruct);

    // setup the txn HTTP
// no host header get's us an extra line ;)
  txn->clientRequest.clientReqHeaders.clear();
  txn->clientRequest.method = TS_HTTP_METHOD_GET;
  txn->clientRequest.url.port = 80;
  txn->clientRequest.url.scheme = "http";
  txn->clientRequest.url.params = "";
  txn->clientRequest.url.path = "/a";
  txn->clientRequest.url.query = "";

    // put (SSL) hosts online so hash will select one
  h->setStatus("www.yahoo4.com",443,true,time(NULL),500);
  h->setStatus("www.yahoo7.com",4443,true,time(NULL),500);

  // make valid request that will get processed pre-remap
  carpPluginHook(NULL, TS_EVENT_HTTP_READ_REQUEST_HDR, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_OS_DNS, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_SEND_RESPONSE_HDR, txnp);

  //CPPUNIT_ASSERT(txn->clientRequest.clientReqHeaders.find(CARP_ROUTED_HEADER) != txn->clientRequest.clientReqHeaders.end() );
  CPPUNIT_ASSERT(txn->clientRequest.url.scheme.find("https") != string::npos); // should reflect scheme to peer

  delete txn;
}

// ======================================================================
void
Testcarp::testcarpPluginHook_Schemes_Postremap(void)
{
  // setup new config
  g_CarpConfigPool = NULL; // forget old config
  string filename("test.config");
  createTestConfig(filename, false, true, true, false, false); // post-remap http peers

  const char *argv[2];
  argv[0] = "carp.so";
  argv[1] = filename.c_str();

  TSPluginInit(2, argv);
  CPPUNIT_ASSERT(g_CarpConfigPool);
  CPPUNIT_ASSERT(g_CarpConfigPool->getGlobalConfig() != NULL);
  CPPUNIT_ASSERT(g_CarpConfigPool->getGlobalHashAlgo() != NULL);

  CarpConfig* c = g_CarpConfigPool->getGlobalConfig();
  HashAlgorithm* h = g_CarpConfigPool->getGlobalHashAlgo();

  CPPUNIT_ASSERT(c->isBlackListed(string("hc.cacheservice.com")));
  CPPUNIT_ASSERT(h);

  cleanup(filename);

  // test against new config
  TxnStruct* txn = new TxnStruct;
  TSHttpTxn txnp = (TSHttpTxn)txn;

  // setup the txn HTTP
// no host header get's us an extra line ;)
  txn->clientRequest.method = TS_HTTP_METHOD_GET;
  txn->clientRequest.url.port = 80;
  txn->clientRequest.url.scheme = "https";
  txn->clientRequest.url.params = "";
  txn->clientRequest.url.path = "/a";
  txn->clientRequest.url.query = "";

  // put hosts online so hash will select one
  h->setStatus("www.yahoo.com",80,true,time(NULL),500);
  h->setStatus("host2.cacheservice.com",80,true,time(NULL),500);

  // make valid request that will get processed pre-remap
  carpPluginHook(NULL, TS_EVENT_HTTP_READ_REQUEST_HDR, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_OS_DNS, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_SEND_RESPONSE_HDR, txnp);
  //CPPUNIT_ASSERT(txn->clientRequest.clientReqHeaders.find(CARP_ROUTED_HEADER) != txn->clientRequest.clientReqHeaders.end() );
  //CPPUNIT_ASSERT(txn->clientRequest.clientReqHeaders.find(CARP_FORWARD_HEADER) != txn->clientRequest.clientReqHeaders.end() );
  //CPPUNIT_ASSERT(txn->clientRequest.url.scheme.find("https") == string::npos); // should reflect scheme to peer
  txn->dump();
  delete txn;

  // Change carp peer to https
    // setup new config
  g_CarpConfigPool = NULL; // forget old config
  createTestConfig(filename, false, true, true, false, true); // post-remap & https peer

  argv[0] = "carp.so";
  argv[1] = filename.c_str();

  TSPluginInit(2, argv);
  CPPUNIT_ASSERT(g_CarpConfigPool);
  CPPUNIT_ASSERT(g_CarpConfigPool->getGlobalConfig() != NULL);
  CPPUNIT_ASSERT(g_CarpConfigPool->getGlobalHashAlgo() != NULL);

  c = g_CarpConfigPool->getGlobalConfig();
  h = g_CarpConfigPool->getGlobalHashAlgo();

  CPPUNIT_ASSERT(c->isBlackListed(string("hc.cacheservice.com")));
  CPPUNIT_ASSERT(h);

  cleanup(filename);

  // test against new config
  txnp = (TSHttpTxn)(txn = new TxnStruct);

    // setup the txn HTTP
// no host header get's us an extra line ;)
  txn->clientRequest.clientReqHeaders.clear();
  txn->clientRequest.method = TS_HTTP_METHOD_GET;
  txn->clientRequest.url.port = 80;
  txn->clientRequest.url.scheme = "http";
  txn->clientRequest.url.params = "";
  txn->clientRequest.url.path = "/a";
  txn->clientRequest.url.query = "";

    // put (SSL) hosts online so hash will select one
  h->setStatus("www.yahoo.com",443,true,time(NULL),500);
  h->setStatus("host2.cacheservice.com",443,true,time(NULL),500);

  // make valid request that will get processed pre-remap
  carpPluginHook(NULL, TS_EVENT_HTTP_READ_REQUEST_HDR, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_OS_DNS, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_SEND_RESPONSE_HDR, txnp);

  //CPPUNIT_ASSERT(txn->clientRequest.clientReqHeaders.find(CARP_ROUTED_HEADER) != txn->clientRequest.clientReqHeaders.end() );
  //CPPUNIT_ASSERT(txn->clientRequest.clientReqHeaders.find(CARP_FORWARD_HEADER) != txn->clientRequest.clientReqHeaders.end() );
  //CPPUNIT_ASSERT(txn->clientRequest.url.scheme.find("https") != string::npos); // should reflect scheme to peer

  delete txn;

  // Change carp peer to https with new format which support scheme
    // setup new config
  g_CarpConfigPool = NULL; // forget old config
  createTestConfig(filename, false, true, true, false, true); // post-remap & https peer

  argv[0] = "carp.so";
  argv[1] = filename.c_str();

  TSPluginInit(2, argv);
  CPPUNIT_ASSERT(g_CarpConfigPool);
  CPPUNIT_ASSERT(g_CarpConfigPool->getGlobalConfig() != NULL);
  CPPUNIT_ASSERT(g_CarpConfigPool->getGlobalHashAlgo() != NULL);

  c = g_CarpConfigPool->getGlobalConfig();
  h = g_CarpConfigPool->getGlobalHashAlgo();

  CPPUNIT_ASSERT(c->isBlackListed(string("hc.cacheservice.com")));
  CPPUNIT_ASSERT(h);

  cleanup(filename);

  // test against new config
  txnp = (TSHttpTxn)(txn = new TxnStruct);

    // setup the txn HTTP
// no host header get's us an extra line ;)
  txn->clientRequest.clientReqHeaders.clear();
  txn->clientRequest.method = TS_HTTP_METHOD_GET;
  txn->clientRequest.url.port = 80;
  txn->clientRequest.url.scheme = "http";
  txn->clientRequest.url.params = "";
  txn->clientRequest.url.path = "/a";
  txn->clientRequest.url.query = "";

    // put (SSL) hosts online so hash will select one
  h->setStatus("www.yahoo4.com",443,true,time(NULL),500);
  h->setStatus("www.yahoo7.com",4443,true,time(NULL),500);

  // make valid request that will get processed pre-remap
  carpPluginHook(NULL, TS_EVENT_HTTP_READ_REQUEST_HDR, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_OS_DNS, txnp);
  carpPluginHook(NULL, TS_EVENT_HTTP_SEND_RESPONSE_HDR, txnp);

  CPPUNIT_ASSERT(txn->clientRequest.clientReqHeaders.find(CARP_ROUTED_HEADER) != txn->clientRequest.clientReqHeaders.end() );
  CPPUNIT_ASSERT(txn->clientRequest.clientReqHeaders.find(CARP_FORWARD_HEADER) != txn->clientRequest.clientReqHeaders.end() );
  CPPUNIT_ASSERT(txn->clientRequest.url.scheme.find("https") != string::npos); // should reflect scheme to peer

  delete txn;
}

// Register this unit test class, so it will be run by a CppTestRunner
// executable.
CPPUNIT_TEST_SUITE_REGISTRATION(Testcarp);

