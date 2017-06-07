//
// Test Cass
//

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

#include <vector>
#include <netdb.h>

#include "tsapi_stub.h"
#include "Common.h"
#include "CarpConfig.h"
#include "CarpHashAlgorithm.h"

using namespace std;

class TestCarpConfig : public CppUnit::TestFixture
{
    CPPUNIT_TEST_SUITE( TestCarpConfig );
    CPPUNIT_TEST( testloadConfig );
    CPPUNIT_TEST( testrun );

    CPPUNIT_TEST_SUITE_END();                                                     

public:

protected:
  void testloadConfig( void );   
  void testrun( void );   
private:
  void createTestConfig( string& filename );
  void cleanup(string& filename );
};

// ======================================================================
void
TestCarpConfig::createTestConfig(string& filename)
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
                "hotrr=1\r\n"
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
TestCarpConfig::cleanup(string& filename)
{
  remove(filename.c_str());
}


// ======================================================================
void
TestCarpConfig::testloadConfig(void)
{
  string filename("test.config");
  createTestConfig(filename);  
  
  CarpConfig c;
  CPPUNIT_ASSERT(!c.loadConfig(string("nonexistentfile")));
  CPPUNIT_ASSERT(!c.isBlackListed(string("hc.cacheservice.com")));
  CPPUNIT_ASSERT(!c.hasWhiteList());
  CPPUNIT_ASSERT(!c.isWhiteListed(string("white.com")));
  
  CPPUNIT_ASSERT(c.loadConfig(filename));
  CPPUNIT_ASSERT(c.getHealthCheckPort() == 8080);
  CPPUNIT_ASSERT(c.isBlackListed(string("hc.cacheservice.com")));
  CPPUNIT_ASSERT(c.isWhiteListed(string("white.com")));
  CPPUNIT_ASSERT(c.getMode() == CarpConfig::PRE);
  CPPUNIT_ASSERT(c.getAllowedForwardPort() == 81);
  CPPUNIT_ASSERT(c.getHealthCheckUrl().compare("http://healthcheck.cacheservice.com:8080/status.html") == 0);
  
  CarpHostList* hl = c.getHostList();
  CPPUNIT_ASSERT(hl->size() == 2);
  CPPUNIT_ASSERT(hl->at(0)->getName().compare("www.yahoo.com") == 0);
  CPPUNIT_ASSERT(hl->at(1)->getName().compare("host2.cacheservice.com") == 0);
  
  string hostDump;
  hl->at(0)->dump(hostDump);
  CPPUNIT_ASSERT(!hostDump.empty());
  cleanup(filename); 
}

void *pthread_start_func(void *data)
{
  CarpConfig *c = (CarpConfig *)data;
  CarpHashAlgorithm h(c);
  HashNode * hashNode;
  
  
  // Setup the hash algo
      CarpHostList* hostList = c->getHostList();
      CPPUNIT_ASSERT(hostList->size() == 2);
    // add hosts, etc to hash algo
    char szServerName[256];
    *szServerName = 0;

    gethostname(szServerName, 255);
    
    char buf[1024];
    struct hostent self, *selfhe = getHostIp(string(szServerName), &self, buf, sizeof (buf));

    for (CarpHostListIter i = hostList->begin(); i != hostList->end(); i++) {
      bool bSelf = false;
      if (NULL != selfhe) { // check for 'self'
        // include hostname and port
        bSelf = isSelf((*i)->getName(), (*i)->getPort(), selfhe);
      }
      (*i)->setHealthCheckPort(c->getHealthCheckPort()); // set HC port
      (*i)->setHealthCheckUrl(c->getHealthCheckUrl()); // set HC Url
      
      // Look up host and create addr struct for healthchecks
      char hBuf[1024];
      struct hostent host, *hosthe = getHostIp((*i)->getName(), &host, hBuf, sizeof (hBuf));
      if(hosthe) {
         // convert hostent to sockaddr_in structure 
        sockaddr_in hIn;
        memcpy(&hIn.sin_addr, hosthe->h_addr_list[0], hosthe->h_length);
        hIn.sin_port = htons((*i)->getHealthCheckPort()); // set port
        if(hosthe->h_length == 4) { // size match IPv4? assume such
          hIn.sin_family = AF_INET;
        } else { // assume IPv6
          hIn.sin_family = AF_INET6;
        }
        (*i)->setHealthCheckAddr(reinterpret_cast<struct sockaddr_storage &>(hIn));
        hIn.sin_port = htons((*i)->getPort());
        hashNode = new HashNode((*i)->getName(), (*i)->getPort(), (*i)->getScheme(), (*i)->getWeight(), bSelf,reinterpret_cast<struct sockaddr_storage &>(hIn)); 
        h.addHost(hashNode);
        HttpFetch *f = new HttpFetch(c->getHealthCheckUrl(), NULL, hashNode);
        c->addHealthCheckClient(f);
      }
    }
    h.algoInit();
  // done setting up hash algo
  c->run(&h);
  return NULL;
}
// ======================================================================
void
TestCarpConfig::testrun(void)
{
  string filename("test.config");
  createTestConfig(filename);  
  
  CarpConfig c;
  c.loadConfig(filename);
  pthread_t tid;

  cerr<<endl<<"Starting CarpConfig.run() and waiting for it to end"<<endl;
  CPPUNIT_ASSERT(pthread_create(&tid, NULL, pthread_start_func, (void *)&c) == 0);
  sleep(6);  // allow the thread to start up..
  c.stop(); // sait for it to end
  cleanup(filename); 
}

// Register this unit test class, so it will be run by a CppTestRunner
// executable.
CPPUNIT_TEST_SUITE_REGISTRATION(TestCarpConfig);

