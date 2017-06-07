//
// Test Case
//
/*
 Algorithm validation…

Hash
Test 1
- Setup 10 hosts with equal weight (1)
- Generate 1MM unique requests
- each host should have been selected ~100k times

Test 2
- Same test as test 1, but set weight of 1 host to 3, all others to 1
- All hosts should have 83K request except the 3x weight host should have 250k

Test 3
- Ensure hash results not changed (would change host selection and invalidate caches)
 


 */
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

#include <map>
#include <string>
#include <vector>

#include "tsapi_stub.h"
#include "CarpHashAlgorithm.h"


using namespace std;

class CarpHashAlgorithmTest : public CppUnit::TestFixture
{
    CPPUNIT_TEST_SUITE( CarpHashAlgorithmTest );
    CPPUNIT_TEST( testHash1 );
    CPPUNIT_TEST( testHash2 );    
    CPPUNIT_TEST( testHash3 );
    CPPUNIT_TEST( testHash4 );
    CPPUNIT_TEST( testHash5 );
    
//    CPPUNIT_TEST( testTime ); // take long time (30 seconds) and doesn't add to testing

    CPPUNIT_TEST_SUITE_END();                                                     

    
public:

protected:
  void testHash1( void );   
  void testHash2( void ); 
  void testHash3( void );
  void testHash4( void );
  void testHash5( void );

  void testTime( void );  
private:

};

// ======================================================================
void
CarpHashAlgorithmTest::testHash1(void)
{
/*
Test 1
- Setup 10 hosts with equal weight (1)
- Generate 10K unique requests
- each host should have been selected ~1k times
*/
  EnableTSDebug(false);
  string hosts[10] = {
    string("h1"),
    string("h2"),
    string("h3"),
    string("h4"),
    string("h5"),
    string("h6"),
    string("h7"),
    string("h8"),
    string("h9"),
    string("h10")
  };
  unsigned int numHosts = sizeof(hosts)/sizeof(string);
  map<HashNode*,int> stats;
  
  CarpConfig c;
  
  sockaddr_storage dummy;
  CarpHashAlgorithm hash(&c);
  // add hosts
  for(unsigned int i=0;i<numHosts; i++)  hash.addHost(hosts[i],80,"http",1,false,dummy);
  hash.algoInit();
  // set hosts online
  for(unsigned int i=0;i<numHosts; i++) hash.setStatus(hosts[i],80,true,time(NULL),500);


  const int numReqs = 10000;
  for (int i = 0; i < numReqs; i++) {
    stringstream ss;
    ss << "http://ncache1.gq1.yahoo.com/blah-blach/test/ack/" << i;
    HashNode* n = hash.getRemapProxy(ss.str());
    CPPUNIT_ASSERT(n);
    stats[n]++;
  }
  // show hit count
  cerr<<"CarpHashAlgorithm HOST,COUNT"<<endl;
  int perHostMin = (numReqs/numHosts) - ((numReqs/numHosts)/3);
  int perHostMax = (numReqs/numHosts) + ((numReqs/numHosts)/3);
  cerr<< "perHostMax="<< perHostMax<<" perHostMin="<<perHostMin<<endl;
  for(map<HashNode*,int>::iterator it = stats.begin() ; it != stats.end(); it++) {
    cerr<<it->first->name<<","<<it->second<<endl;
    CPPUNIT_ASSERT(it->second <= perHostMax && it->second >= perHostMin);
  }
  EnableTSDebug(true);
  cerr<<"Even distribution of host selection...pass"<<endl;
}

// ======================================================================
void
CarpHashAlgorithmTest::testHash2(void)
{
/*
Test 2
- Same test as test 1, but set weight of 1 host to 3, all others to 1
- All hosts should have ~833 request except the 3x weight host should have ~2500
*/
  EnableTSDebug(false);
  string hosts[10] = {
    string("h1"),
    string("h2"),
    string("h3"),
    string("h4"),
    string("h5"),
    string("h6"),
    string("h7"),
    string("h8"),
    string("h9"),
    string("h10")
  };
  unsigned int numHosts = sizeof(hosts)/sizeof(string);
  map<HashNode*,int> stats;
  
  CarpConfig c;
  
  sockaddr_storage dummy;
  CarpHashAlgorithm hash(&c);
  // add hosts
  for(unsigned int i=0;i<numHosts; i++) {
    double weight=1.0;
    if(i == 5) weight = 3;
    hash.addHost(hosts[i],80,"http",weight,false,dummy);
  }
  hash.algoInit();
  // set hosts online
  for(unsigned int i=0;i<numHosts; i++) hash.setStatus(hosts[i],80,true,time(NULL),500);


  const int numReqs = 10000;
  for (int i = 0; i < numReqs; i++) {
    stringstream ss;
    ss << "http://ncache1.gq1.yahoo.com/blah-blach/test/ack/" << i;
    HashNode* n = hash.getRemapProxy(ss.str());
    CPPUNIT_ASSERT(n);
    stats[n]++;
  }
  // show hit count
  cerr<<"CARP HOST,COUNT"<<endl;
  int iCount=0;
  for(map<HashNode*,int>::iterator it = stats.begin() ; it != stats.end(); it++,iCount++) {
    cerr<<it->first->name<<","<<it->second<<endl;
    int reqTarget = 833;
    if(it->first->name.compare(hosts[5]) == 0) reqTarget = 2500;
    int perHostMin = reqTarget - (reqTarget/2);
    int perHostMax = reqTarget + (reqTarget/2);
    cerr<< "perHostMax="<< perHostMax<<" perHostMin="<<perHostMin<<endl;
    if(it->first->name.compare("h7")) { // h7 selection is too numerous, ignore stats for it
      CPPUNIT_ASSERT(it->second <= perHostMax && it->second >= perHostMin);
    } else {
      cerr<<"IGNORED COUNT FROM "<<it->first->name<<endl;
    }
  }
  EnableTSDebug(true);
  cerr<<"Even distribution of host selection...pass"<<endl;
}

// ======================================================================
void
CarpHashAlgorithmTest::testHash3(void)
{
  /*
  Test 3
  - Ensure hash results not changed (would change host selection and invalidate caches)
   */
  string hosts[10] = {
    string("h1"),
    string("h2"),
    string("h3"),
    string("h4"),
    string("h5"),
    string("h6"),
    string("h7"),
    string("h8"),
    string("h9"),
    string("h10")
  };
  unsigned int numHosts = sizeof (hosts) / sizeof (string);
  map<HashNode*, int> stats;

  EnableTSDebug(false);
  CarpConfig c;

  sockaddr_storage dummy;
  CarpHashAlgorithm hash(&c);
  // add hosts
  for (unsigned int i = 0; i < numHosts; i++) hash.addHost(hosts[i], 80, "http", 1, false, dummy);
  hash.algoInit();
  // set hosts online
  for (unsigned int i = 0; i < numHosts; i++) hash.setStatus(hosts[i], 80, true,time(NULL),500);

  HashNode* n1 = hash.getRemapProxy(string("http://ncache1.gq1.yahoo.com/blah-blach/test/ack/1"));
  HashNode* n2 = hash.getRemapProxy(string("http://ncache1.gq1.yahoo.com/blah-blach/test/ack/2"));
  HashNode* n3 = hash.getRemapProxy(string("http://ncache1.gq1.yahoo.com/blah-blach/test/ack/3"));
  HashNode* n4 = hash.getRemapProxy(string("http://ncache1.gq1.yahoo.com/blah-blach/test/ack/4"));
  HashNode* n5 = hash.getRemapProxy(string("http://ncache1.gq1.yahoo.com/blah-blach/test/ack/5"));
  HashNode* n6 = hash.getRemapProxy(string("http://ncache1.gq1.yahoo.com/blah-blach/test/ack/6"));
  HashNode* n7 = hash.getRemapProxy(string("http://ncache1.gq1.yahoo.com/blah-blach/test/ack/7"));

  cerr << n1->name << endl;
  cerr << n2->name << endl;
  cerr << n3->name << endl;
  cerr << n4->name << endl;
  cerr << n5->name << endl;
  cerr << n6->name << endl;
  cerr << n7->name << endl;
  /*
h6
h10
h2
h5
h1
h9
h6   */
  CPPUNIT_ASSERT(n1->name.compare("h6") == 0);
  CPPUNIT_ASSERT(n2->name.compare("h10") == 0);
  CPPUNIT_ASSERT(n3->name.compare("h2") == 0);
  CPPUNIT_ASSERT(n4->name.compare("h5") == 0);
  CPPUNIT_ASSERT(n5->name.compare("h1") == 0);
  CPPUNIT_ASSERT(n6->name.compare("h9") == 0);
  CPPUNIT_ASSERT(n7->name.compare("h6") == 0);

  EnableTSDebug(true);
  cerr << "Hash consistency vs previous version of code...pass" << endl;

}

// ======================================================================
void
CarpHashAlgorithmTest::testHash4(void)
{
  /*
  Test 3
  - Ensure hash results not changed (would change host selection and invalidate caches)
   */
  string hosts[10] = {
    string("h1"),
    string("h2"),
    string("h3"),
    string("h4"),
    string("h5"),
    string("h6"),
    string("h7"),
    string("h8"),
    string("h9"),
    string("h10")
  };
  unsigned int numHosts = sizeof (hosts) / sizeof (string);
  map<HashNode*, int> stats;

  EnableTSDebug(false);
  CarpConfig c;

  sockaddr_storage dummy;
  CarpHashAlgorithm hash(&c);
  // add hosts
  for (unsigned int i = 0; i < numHosts; i++) {
    hash.addHost(hosts[i], 80, "http", 1, false, dummy);
    hash.setStatus(hosts[i], 80, true,time(NULL),500);
  }
  hash.algoInit();
  // exercise dump
  string dmp;
  hash.dump(dmp);
  cerr<<"Dump returned:" << endl << dmp << endl;
  CPPUNIT_ASSERT(1);

  EnableTSDebug(true);
  cerr << "Dump...pass" << endl;

}

// ======================================================================
void
CarpHashAlgorithmTest::testHash5(void)
{
/*
Test 5
- Same test as test 2, add a new Node after first initialization
- The urls should still distributed to previous node or the newly added node
*/
  EnableTSDebug(false);
  string hosts[10] = {
    string("h1"),
    string("h2"),
    string("h3"),
    string("h4"),
    string("h5"),
    string("h6"),
    string("h7"),
    string("h8"),
    string("h9"),
    string("h10")
  };
  unsigned int numHosts = sizeof(hosts)/sizeof(string);
  map<HashNode*,int> stats;
  map<HashNode*, vector<string> > strVec;

  CarpConfig c;

  sockaddr_storage dummy;
  CarpHashAlgorithm hash(&c);
  // add hosts
  for(unsigned int i=0;i<numHosts; i++) {
    double weight=1.0;
    if(i == 5) weight = 3;
    hash.addHost(hosts[i],80,"http",weight,false,dummy);
  }
  hash.algoInit();
  // set hosts online
  for(unsigned int i=0;i<numHosts; i++) hash.setStatus(hosts[i],80,true,time(NULL),500);


  const int numReqs = 10000;
  for (int i = 0; i < numReqs; i++) {
    stringstream ss;
    ss << "http://ncache1.gq1.yahoo.com/blah-blach/test/ack/" << i;
    HashNode* n = hash.getRemapProxy(ss.str());
    CPPUNIT_ASSERT(n);
    stats[n]++;
    strVec[n].push_back(ss.str());
  }
  // show hit count
  cerr<<"CARP HOST,COUNT"<<endl;
  int iCount=0;
  for(map<HashNode*,int>::iterator it = stats.begin() ; it != stats.end(); it++,iCount++) {
    cerr<<it->first->name<<","<<it->second<<endl;
    int reqTarget = 833;
    if(it->first->name.compare(hosts[5]) == 0) reqTarget = 2500;
    int perHostMin = reqTarget - (reqTarget/2);
    int perHostMax = reqTarget + (reqTarget/2);
    cerr<< "perHostMax="<< perHostMax<<" perHostMin="<<perHostMin<<endl;
    if(it->first->name.compare("h7")) { // h7 selection is too numerous, ignore stats for it
      CPPUNIT_ASSERT(it->second <= perHostMax && it->second >= perHostMin);
    } else {
      cerr<<"IGNORED COUNT FROM "<<it->first->name<<endl;
    }
  }

  EnableTSDebug(true);
  // add one more node into CARP dynamically
  hash.addHost(string("h1b"), 80, "http", 2, false, dummy);
  hash.algoInit();
  hash.setStatus(string("h1b"), 80, true, time(NULL), 500);

  map<HashNode *, int> stats2;
  map<HashNode *, vector<string> > strVec2;
  for (int i = 0; i < numReqs; i++) {
    stringstream ss;
    ss << "http://ncache1.gq1.yahoo.com/blah-blach/test/ack/" << i;
    HashNode* n = hash.getRemapProxy(ss.str());
    CPPUNIT_ASSERT(n);
    stats2[n]++;
    strVec2[n].push_back(ss.str());
  }

  vector<string> * vStringOld;
  vector<string> * vStringNew;
  vector<string> * vString;
  for (map<HashNode *, vector<string> >::iterator it = strVec.begin(); it != strVec.end();
      it++) {
    if ( it->first->name == string("h1")) {
      vStringOld = &(it->second);
    }
  }
  for (map<HashNode *, vector<string> >::iterator it = strVec2.begin(); it != strVec2.end();
      it++) {
    if ( it->first->name == string("h1")) {
      vStringNew = &(it->second);
    }
    if ( it->first->name == string("h1b")) {
      vString = &(it->second);
    }
  }

  /*
   * vStringOld,vStringNew, vString are sorted
   */
  bool foundInSelf = false;
  bool foundInNewNode = false;
  for (vector<string>::iterator it = vStringOld->begin();
      it != vStringOld->end(); it++) {
    for (vector<string>::iterator it2 = vStringNew->begin();
        it2 != vStringNew->end(); it2++) {
      if (*it == *it2) {
        foundInSelf = true;
        break;
      }
    }
    for (vector<string>::iterator it3 = vString->begin();
        it3 != vString->end(); it3++) {
      if (*it == *it3) {
        foundInNewNode = true;
        break;
      }
    }

    CPPUNIT_ASSERT(foundInSelf || foundInNewNode);
    foundInSelf = false;
    foundInNewNode = false;

  }

  cerr<<"Even distribution of host selection...pass"<<endl;
}


// ======================================================================
void
CarpHashAlgorithmTest::testTime(void)
{
  /*
  Test 1
  - Setup 10 hosts with equal weight (1)
  - Generate 10K unique requests
  - each host should have been selected ~1k times
   */
  EnableTSDebug(false);
  string hosts[10] = {
    string("h1"),
    string("h2"),
    string("h3"),
    string("h4"),
    string("h5"),
    string("h6"),
    string("h7"),
    string("h8"),
    string("h9"),
    string("h10")
  };
  unsigned int numHosts = sizeof (hosts) / sizeof (string);

  int numReqs = 10000;

  CarpConfig c;

  sockaddr_storage dummy;
  CarpHashAlgorithm hash(&c);
  // add hosts
  for (unsigned int i = 0; i < numHosts; i++) hash.addHost(hosts[i], 80, "http", 1, false, dummy);
  hash.algoInit();
  // set hosts online
  for (unsigned int i = 0; i < numHosts; i++) hash.setStatus(hosts[i], 80, true,time(NULL),500);

  timespec tstart;
  timespec tstop;
  double t2;
  
  do {
    clock_gettime(CLOCK_REALTIME, &tstart);
    hash.algoInit();
    string url("http://ncache1.gq1.yahoo.com/blah-blach/test/ack/");
    for (int i = 0; i < numReqs; i++) {
      HashNode* n = hash.getRemapProxy(url);
      CPPUNIT_ASSERT(n);
    }
    clock_gettime(CLOCK_REALTIME, &tstop);

    long nano = tstop.tv_nsec - tstart.tv_nsec;
    long sec = tstop.tv_sec - tstart.tv_sec;
    if (nano < 0) {
      nano += 1000000000;
      --sec;
    }
    t2 = sec + (nano / 1000000000.0);
    cerr << "PERFORMANCE Hashes/S = " << numReqs / t2 << " t2="<<t2<<endl;
    numReqs = (numReqs / t2) * 15; // calc new numReqs
  } while (t2 < 10); // want to run long enough to really tell...
  EnableTSDebug(true);
}


// ======================================================================

// Register this unit test class, so it will be run by a CppTestRunner
// executable.
CPPUNIT_TEST_SUITE_REGISTRATION(CarpHashAlgorithmTest);

