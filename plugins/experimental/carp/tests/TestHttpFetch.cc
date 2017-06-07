//
// Test Cass
//

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

#include <vector>
#include "tsapi_stub.h"
#include "HttpFetch.h"

using namespace std;

class TestHttpFetch : public CppUnit::TestFixture
{
    CPPUNIT_TEST_SUITE( TestHttpFetch );
    CPPUNIT_TEST( testmakeSyncRequest );

    CPPUNIT_TEST_SUITE_END();                                                     

public:

protected:
  void testmakeSyncRequest( void );   
private:

};

// ======================================================================
// there is a lot of stuff required in tsapi_stuff to make the fetcher actually work
// this is just the beginning...
void
TestHttpFetch::testmakeSyncRequest(void)
{
  HttpFetch fetcher(string("www.yahoo.com"), NULL, NULL, TS_HTTP_METHOD_GET);
  
  sockaddr_in sa;
  
  sa.sin_family=AF_INET;
  sa.sin_port=htons(12435);
  sa.sin_addr.s_addr=htonl(0x01020304);

  fetcher.makeAsyncRequest(reinterpret_cast<const sockaddr *>(&sa));

  HttpFetch::HttpFetcherEvent r=fetcher.getResponseResult();
  TSHttpStatus s= fetcher.getResponseStatusCode();
  string body = fetcher.getResponseBody();
  string headers = fetcher.getResponseHeaders();
  
  CPPUNIT_ASSERT(r == HttpFetch::SUCCESS);
  CPPUNIT_ASSERT(s == 0);
  CPPUNIT_ASSERT(body.empty());
  CPPUNIT_ASSERT(headers.empty());

}



// Register this unit test class, so it will be run by a CppTestRunner
// executable.
CPPUNIT_TEST_SUITE_REGISTRATION(TestHttpFetch);

