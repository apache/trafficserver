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
#include "CarpHashAlgorithm.h"

using namespace std;

class TestCarpConfigPool : public CppUnit::TestFixture
{
    CPPUNIT_TEST_SUITE( TestCarpConfigPool );
    CPPUNIT_TEST( testprocessConfigFile );

    CPPUNIT_TEST_SUITE_END();                                                     

public:

protected:
  void testprocessConfigFile( void );   
  
private:
  void createTestConfig( string& filename );
  void cleanup(string& filename );
};

// ======================================================================
void
TestCarpConfigPool::createTestConfig(string& filename)
{
  string config(
                "[Servers]\r\n"
                "www.yahoo.com:4080 weight=2\r\n"
                "host2.cacheservice.com  \r\n"
                "www.yahoo2.com 4080 weight=2\r\n"
                "www.yahoo3.com:4080 weight=\r\n"
                "[Values]\r\n"
                "healthcheck=http://healthcheck.cacheservice.com:8080/status.html\r\n"
                "healthfreq=30\r\n"
                "blacklist=healthcheck.cacheservice.com,hc.cacheservice.com\r\n"
                "whitelist=white.com\r\n"
                "mode=unknown\r\n"
                "mode=post-remap\r\n"
                "mode=pre-remap\r\n"
                "hotslots=20\r\n"
                "hotthreshold=5\r\n"
                "allowfwdport=81\r\n"
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

  ofstream f;
  f.open(filename.c_str());
  f << config;
  f.close();
}

// ======================================================================
void
TestCarpConfigPool::cleanup(string& filename)
{
  remove(filename.c_str());
}
// ======================================================================
void
TestCarpConfigPool::testprocessConfigFile(void)
{
  string filename("test.config");
  createTestConfig(filename);  
  
  CarpConfigPool p;
  p.processConfigFile(filename,true);
  sleep(1);
  CarpConfig* c = p.getGlobalConfig();
  HashAlgorithm* h = p.getGlobalHashAlgo();
  
 
  CPPUNIT_ASSERT(c->getHealthCheckPort() == 8080);
  CPPUNIT_ASSERT(c->isBlackListed(string("hc.cacheservice.com")));
  CPPUNIT_ASSERT(c->isWhiteListed(string("white.com")));
  CPPUNIT_ASSERT(c->getMode() == CarpConfig::PRE);
  CPPUNIT_ASSERT(c->getAllowedForwardPort() == 81);
  CPPUNIT_ASSERT(c->getHealthCheckUrl().compare("http://healthcheck.cacheservice.com:8080/status.html") == 0);
 
  CPPUNIT_ASSERT(h);
  
  cleanup(filename);
}


// Register this unit test class, so it will be run by a CppTestRunner
// executable.
CPPUNIT_TEST_SUITE_REGISTRATION(TestCarpConfigPool);

