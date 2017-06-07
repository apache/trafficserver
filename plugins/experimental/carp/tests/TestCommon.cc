//
// Test Cass
//

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

#include <vector>
#include "tsapi_stub.h"
#include "Common.h"

using namespace std;

class TestCommon : public CppUnit::TestFixture
{
    CPPUNIT_TEST_SUITE( TestCommon );
    CPPUNIT_TEST( teststringExplode );
    CPPUNIT_TEST( testgetHostIp );
    CPPUNIT_TEST( testisPortSelf );
    CPPUNIT_TEST( testisSelf );
    CPPUNIT_TEST( testaddHeader );
    CPPUNIT_TEST( testgetStringFromSockaddr );
    CPPUNIT_TEST_SUITE_END();                                                     

public:

protected:
  void teststringExplode( void );   
  void testgetHostIp( void );
  void testisPortSelf( void );
  void testisSelf( void );
  void testaddHeader( void );
  void testgetStringFromSockaddr( void );
private:

};

// ======================================================================

void
TestCommon::teststringExplode(void)
{
  string sSource("This is a test string");
  vector<string> result;

  stringExplode(sSource, string(" "), &result);

  CPPUNIT_ASSERT(result.size() == 5);
  CPPUNIT_ASSERT(result[0].compare("This") == 0);
  CPPUNIT_ASSERT(result[1].compare("is") == 0);
  CPPUNIT_ASSERT(result[2].compare("a") == 0);
  CPPUNIT_ASSERT(result[3].compare("test") == 0);
  CPPUNIT_ASSERT(result[4].compare("string") == 0);
}

// ======================================================================

void TestCommon::testgetHostIp( void ) {
  hostent he;
  char buf[1000];
  
  hostent *h=getHostIp(string("localhost"),&he,buf,sizeof(buf));
  CPPUNIT_ASSERT(h);
  //struct hostent {
  //  char  *h_name;            /* official name of host */
  //  char **h_aliases;         /* alias list */
  //  int    h_addrtype;        /* host address type */
  //  int    h_length;          /* length of address */
  //  char **h_addr_list;       /* list of addresses */
  //}
  //#define h_addr h_addr_list[0] /* for backward compatibility */
   
  CPPUNIT_ASSERT(strcmp(h->h_name,"localhost") == 0);
  CPPUNIT_ASSERT((int)h->h_addr_list[0][0] == 127);
  CPPUNIT_ASSERT((int)h->h_addr_list[0][1] == 0);
  CPPUNIT_ASSERT((int)h->h_addr_list[0][2] == 0);
  CPPUNIT_ASSERT((int)h->h_addr_list[0][3] == 1);
/*  cerr<<"h_name="<<h->h_name<<endl;
  cerr<<"h_addrtype="<<h->h_addrtype<<endl;
  cerr<<"h_length="<<h->h_length<<endl;
  cerr<<"h_addr_list[0]="
    <<(int)h->h_addr_list[0][0]<<"."
    <<(int)h->h_addr_list[0][1]<<"."
    <<(int)h->h_addr_list[0][2]<<"."
    <<(int)h->h_addr_list[0][3]<<" "
    <<endl;
*/
  
  // FAILURE CASE
  h=getHostIp(string("1badhostname2"),&he,buf,sizeof(buf));
  CPPUNIT_ASSERT(h == NULL);
}

// ======================================================================
void TestCommon::testisPortSelf( void )
{
  EnableTSDebug(true);
  CPPUNIT_ASSERT(!isPortSelf(1234));
  
  struct sockaddr_in servaddr;
  
  int listenfd=socket(AF_INET,SOCK_STREAM,0);

   bzero(&servaddr,sizeof(servaddr));
   servaddr.sin_family = AF_INET;
   servaddr.sin_addr.s_addr=htonl(INADDR_ANY);
   servaddr.sin_port=htons(1234);
   bind(listenfd,(struct sockaddr *)&servaddr,sizeof(servaddr));

   listen(listenfd,1024);
   CPPUNIT_ASSERT(isPortSelf(1234));
   close(listenfd);
}

// ======================================================================
void
TestCommon::testisSelf(void)
{
  char szServerName[256];
  *szServerName = 0;

  if (!gethostname(szServerName, 255)) {// success!
    cerr << "using " << szServerName << " as server name 'self'" << endl;
  }
  char buf[1024];
  struct hostent self, *selfhe = getHostIp(string(szServerName), &self, buf, sizeof (buf));

  CPPUNIT_ASSERT(!isSelf(szServerName, 1234, selfhe));
}

// ======================================================================
void
TestCommon::testaddHeader(void)
{
  string header("Test");
  string value("value");
  string valueOut;
  
  TSMBuffer bufp = NULL;
  TSMLoc hdr_loc;
  
  TSMimeHdrCreate(bufp, &hdr_loc);
    
  CPPUNIT_ASSERT(addHeader(bufp,hdr_loc, header,value));
  CPPUNIT_ASSERT(getHeader(bufp, hdr_loc, header, valueOut));
  CPPUNIT_ASSERT(valueOut.compare(value) == 0);  
  CPPUNIT_ASSERT(removeHeader(bufp, hdr_loc, header));
  CPPUNIT_ASSERT(!getHeader(bufp, hdr_loc, header, valueOut));
}
// ======================================================================
void
TestCommon::testgetStringFromSockaddr(void)
{
  sockaddr_in sa;
  
  sa.sin_family = AF_INET;
  sa.sin_port = 0xd204;
  sa.sin_addr.s_addr = 0x0101A8C0;

  string result;
  CPPUNIT_ASSERT(getStringFromSockaddr((const struct sockaddr *) &sa, result));
  CPPUNIT_ASSERT(result.compare("192.168.1.1:1234") == 0);
  
  sockaddr_in6 sa6;
  sa6.sin6_family = AF_INET6;
  sa6.sin6_port = 0xd204;
  sa6.sin6_addr.__in6_u.__u6_addr32[0] = 0x01020304;
  sa6.sin6_addr.__in6_u.__u6_addr32[1] = 0x05060708;
  sa6.sin6_addr.__in6_u.__u6_addr32[2] = 0x090A0B0C;
  sa6.sin6_addr.__in6_u.__u6_addr32[3] = 0x0D0E0F10;

  CPPUNIT_ASSERT(getStringFromSockaddr((const struct sockaddr *) &sa6, result));
  CPPUNIT_ASSERT(result.compare("403:201:807:605:c0b:a09:100f:e0d:1234") == 0);
}


// Register this unit test class, so it will be run by a CppTestRunner
// executable.
CPPUNIT_TEST_SUITE_REGISTRATION(TestCommon);

