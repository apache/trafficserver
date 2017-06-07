//
// Test Cass
//

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

#include <vector>
#include "tsapi_stub.h"
#include "UrlComponents.h"

using namespace std;

class TestUrlComponents : public CppUnit::TestFixture
{
    CPPUNIT_TEST_SUITE( TestUrlComponents );
    CPPUNIT_TEST( testpopulate );
    CPPUNIT_TEST( testgetCompletePathString );
    CPPUNIT_TEST( testgetCompleteHostString );
    CPPUNIT_TEST( testGettersAndSetters );

    CPPUNIT_TEST_SUITE_END();                                                     

public:

protected:
  void testpopulate( void );   
  void testgetCompletePathString( void );   
  void testgetCompleteHostString( void );   
  void testGettersAndSetters( void );   

private:

};

// ======================================================================
void
TestUrlComponents::testpopulate(void)
{
  
  string sUrl[] = { 
    string("http://www.yahoo.com/"), 
    string("http://www.yahoo.com/test.com?query=1"), 
    string("http://www.yahoo.com/test.com"),
    string("http://www.yahoo.com:81/test.com"),
    string("http://www.yahoo.com:81/test.com?query=1"), 
    string("http://www.yahoo.com/test.com;matrix=1"),
    string("http://www.yahoo.com:81/test.com;matrix=1"),
    string("http://www.yahoo.com:81/test.com;matrix=1?query=1")
  };
  const char *start;
  const char *end;
  UrlComponents url;

  TSMBuffer bufp = NULL;
  TSMLoc url_loc;


  TSUrlCreate(bufp, &url_loc);
  for(size_t i=0;i<sizeof(sUrl)/sizeof(sUrl[0]);i++)
  {
    start = sUrl[i].data();
    end = sUrl[i].data() + sUrl[i].length();
    TSUrlParse(bufp, url_loc, &start, end);

    url.populate(bufp, url_loc);

    string result;
    url.construct(result);
    CPPUNIT_ASSERT(result.compare(sUrl[i]) == 0);
    cerr << "pass testpopulate for sUrl[i]="<<sUrl[i]<<endl;    
  }
}

// ======================================================================
void
TestUrlComponents::testgetCompletePathString(void)
{

  string sUrl("http://www.yahoo.com:81/test.com?query=q");

  const char *start;
  const char *end;
  UrlComponents url;

  TSMBuffer bufp = NULL;
  TSMLoc url_loc;

  TSUrlCreate(bufp, &url_loc);

  start = sUrl.data();
  end = sUrl.data() + sUrl.length();
  TSUrlParse(bufp, url_loc, &start, end);

  url.populate(bufp, url_loc);
  string temp;
  url.getCompletePathString(temp);
  CPPUNIT_ASSERT(temp.compare("/test.com?query=q") == 0);
  
  
  string sUrl2("http://www.yahoo.com:81/test.com;matrix=q");
  
  start = sUrl2.data();
  end = sUrl2.data() + sUrl2.length();
  TSUrlParse(bufp, url_loc, &start, end);

  url.populate(bufp, url_loc);
  url.getCompletePathString(temp);
  CPPUNIT_ASSERT(temp.compare("/test.com;matrix=q") == 0);
 
  string sUrl3("http://www.yahoo.com/");
  
  start = sUrl3.data();
  end = sUrl3.data() + sUrl3.length();
  TSUrlParse(bufp, url_loc, &start, end);

  url.populate(bufp, url_loc);
  url.getCompletePathString(temp);
  CPPUNIT_ASSERT(temp.compare("/") == 0);
 
}

// ======================================================================
void
TestUrlComponents::testgetCompleteHostString(void)
{
  string sUrl("http://www.yahoo.com:81/test.com?query=q");

  const char *start;
  const char *end;
  UrlComponents url;

  TSMBuffer bufp = NULL;
  TSMLoc url_loc;

  TSUrlCreate(bufp, &url_loc);

  start = sUrl.data();
  end = sUrl.data() + sUrl.length();
  TSUrlParse(bufp, url_loc, &start, end);

  url.populate(bufp, url_loc);
  string temp;
  url.getCompleteHostString(temp);
  CPPUNIT_ASSERT(temp.compare("www.yahoo.com:81") == 0);
  
  string sUrl2("http://www.yahoo.com/test.com?query=q");
  start = sUrl2.data();
  end = sUrl2.data() + sUrl2.length();
  TSUrlParse(bufp, url_loc, &start, end);

  url.populate(bufp, url_loc);
  url.getCompleteHostString(temp);
  CPPUNIT_ASSERT(temp.compare("www.yahoo.com") == 0);
  
}

// ======================================================================
void
TestUrlComponents::testGettersAndSetters(void)
{
  UrlComponents url;

  string sHost("www.yahoo.com");
  string sMatrix(";m=1");
  string sPath("/path");
  string sQuery("?query=1");
  string sScheme("https");
  
  url.setHost(sHost);
  url.setMatrix(sMatrix);
  url.setPath(sPath);
  url.setPort(1234);
  url.setQuery(sQuery);
  url.setScheme(sScheme);
  
  CPPUNIT_ASSERT(url.getHost().compare(sHost) == 0);
  CPPUNIT_ASSERT(url.getMatrix().compare(sMatrix) == 0);
  CPPUNIT_ASSERT(url.getPath().compare(sPath) == 0);
  CPPUNIT_ASSERT(url.getPort() == 1234);
  CPPUNIT_ASSERT(url.getQuery().compare(sQuery) == 0);
  CPPUNIT_ASSERT(url.getScheme().compare(sScheme) == 0);
  
}

// Register this unit test class, so it will be run by a CppTestRunner
// executable.
CPPUNIT_TEST_SUITE_REGISTRATION(TestUrlComponents);

