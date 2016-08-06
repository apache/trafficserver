/** @file

  Implements the CARP hash algorithm

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */
#include <ts/ts.h>
#include <stdio.h>
#include <memory.h>
#include <list>

#include <iostream>
#include <sstream>

#include "Common.h"
#include "CarpHashAlgorithm.h"

using namespace std;

/*****************************************************************/
void
HashNode::dump(string& s)
{
  stringstream ss;
  string sSockaddr;
  getStringFromSockaddr(reinterpret_cast<const struct sockaddr *>( &forwardAddr), sSockaddr);
  
  ss << scheme << "://" << name << ":"<<listenPort<<" ("<<sSockaddr<<") weight:"<<weight<< (_status ?  string(" UP ") : string(" DOWN ")); 
  if(_statusTime) {
    ss << "(" << time(NULL)-_statusTime<< "s ago in "<< _statusLatencyMs << "mS)";
  }
  ss << " hits:" << _hits;
  ss << " carp_noforwarded:" << _carp_noforwarded;
  ss << " carp_forwarded:" << _carp_forwarded;
  ss << endl;
  s += ss.str();
}

/*****************************************************************/
void
HashAlgorithm::addHost(std::string name, unsigned int port, std::string scheme, double weight, bool self, struct sockaddr_storage fwdAddr)
{
  HashNode* node = new HashNode(name, port, scheme, weight, self, fwdAddr);
  TSAssert(NULL != node);
  addHost(node);
}

/*****************************************************************/
void
HashAlgorithm::addHost(HashNode* node)
{
  if (!node) return;
  _hostList.push_back(node);
}

/*****************************************************************/
HashNode*
HashAlgorithm::findStatusByNameAndPort(const string& name, unsigned int port,
    size_t* index) {
  /*
   * Todo: This use loop to find the corresponding HashNode
   * 		But the HttpClient and the Hash was related, so we
   * 		could use more easier method to write the status
   */
  for (size_t ptr = 0; ptr < _hostList.size(); ptr++) {
    if (_hostList[ptr]->listenPort == port
        && _hostList[ptr]->name.compare(name) == 0) { // found it
      if (index) {
        *index = ptr;
      }
      return _hostList[ptr];
    }
  }
  return NULL;
}

size_t
HashAlgorithm::findHashNodeIndex(HashNode *node) {
  for (size_t ptr = 0; ptr < _hostList.size(); ptr++) {
    if ( _hostList[ptr] == node) {
      return ptr;
    }
  }
  return -1;
}

/*****************************************************************/
void
HashAlgorithm::setStatus(const string& name, unsigned int port, bool status, time_t time, uint64_t latencyMs)
{
  TSDebug(DEBUG_TAG_INIT, "HashAlgorithm::setStatus name=%s status=%d", name.c_str(), status);
  
  HashNode* node = findStatusByNameAndPort(name,port);
  if(node) {
    node->setStatus(status,time,latencyMs);
  } else {
    TSError("Carp internal error setStatus host %s not found",name.c_str());
  }
}

void
HashAlgorithm::setStatus(HashNode * node, bool status, time_t time, uint64_t latencyMs)
{
  TSDebug(DEBUG_TAG_INIT, "HashAlgorithm::setStatus name=%s status=%d", node->name.c_str(), status);

//  HashNode* node = findStatusByNameAndPort(name,port);
  if(node) {
    node->setStatus(status,time,latencyMs);
  } else {
    TSError("Carp internal error setStatus host %s not found",node->name.c_str());
  }
}

/*****************************************************************/
HashAlgorithm::~HashAlgorithm()
{
  for (size_t ptr = 0; ptr < _hostList.size(); ptr++) {
    delete _hostList[ptr];
  }
}

/*****************************************************************/
void
HashAlgorithm::dump(string& s)
{
  _config->dump(s);
  
  stringstream ss;
  ss << "Hash Algo stats:" << endl;
  s += ss.str();
  
  for (size_t ptr = 0; ptr < _hostList.size(); ptr++) {
    ss.str("");
    ss.clear();
    ss << ptr << "-";
    s += ss.str();
    _hostList[ptr]->dump(s);
  }
}

/*****************************************************************/
void
CarpHashAlgorithm::algoInit()
{
  // calculate hash for each host
  for (size_t ptr = 0; ptr < _hostList.size(); ptr++) {
    std::string cname;
    char buf[64];
    memset(buf, 0, 64);
    snprintf(buf, 64, "%s:%d", _hostList[ptr]->name.c_str(), _hostList[ptr]->listenPort);
    cname = buf;

    _hostList[ptr]->hash = _calculateHash(cname);
  }

  // calculate loadmultiplier
  _calculateLoadMultiplier();
  
   for (size_t ptr = 0; ptr < _hostList.size(); ptr++) {
     TSDebug(DEBUG_TAG_INIT, "algoInit host=%s port=%d hash=0x%x weight=%g loadFac=%g loadMult=%g isSelf=%d status=%d", 
             _hostList[ptr]->name.c_str(),
             _hostList[ptr]->listenPort,
             _hostList[ptr]->hash,
             _hostList[ptr]->weight,
             _hostList[ptr]->loadFactor,
             _hostList[ptr]->loadMultiplier,
             _hostList[ptr]->isSelf,
             _hostList[ptr]->getStatus());
   }
}

/*****************************************************************/
std::vector<HashNode*>
CarpHashAlgorithm::getRemapProxyList(const std::string& url)
{
  std::vector<HashNode *> hn=_selectReplicateNodes(url);
  return hn;
}

/*****************************************************************/
HashNode*
CarpHashAlgorithm::getRemapProxy(const std::string& url)
{
  HashNode *hn=_selectNode(url);
  if(hn) {
    hn->hit();
  }
  return hn;
}


/*****************************************************************/
unsigned int
CarpHashAlgorithm::_calculateHash(const std::string& hostname)
{
  // check input data
  if (hostname.empty())
    return 0;

  unsigned int hash = 0;

  // calculate MemberProxy_Hash
  for (size_t ptr = 0; ptr < hostname.length(); ptr++)
    hash += ROTATE_LEFT(hash, 19) + hostname.c_str()[ptr];

  hash += hash * 0x62531965;
  hash = ROTATE_LEFT(hash, 21);
  // return
  return hash;
}

/*****************************************************************/
void
CarpHashAlgorithm::_calculateLoadMultiplier()
{
  /* 
   * 
   * 3.3. Load Factor

  Support for array members with differing HTTP processing & caching
  capacity is achieved by multiplying each of the combined hash values
  by a Load Factor Multiplier.

  The Load Factor Multiplier for an individual member is calculated by
  taking each member's relative Load Factor and applying the
  following formula:

  The Load Factor Multiplier must be calculated from the smallest 
  P_k to the largest P_k.  The sum of all P_k's must be 1.

  For each proxy server 1,...,K, the Load Factor Multiplier, X_k, is
  calculated iteratively as follows:

  All X_n values are 32 bit floating point numbers.

  X_1 = pow ((K*p_1), (1/K))

  X_k = ([K-k+1] * [P_k - P_{k-1}])/(X_1 * X_2 * ... * X_{k-1})
  X_k += pow ((X_{k-1}, {K-k+1})
  X_k = pow (X_k, {1/(K-k+1)})

  where:

  X_k = Load Factor Multiplier for proxy k
  K = number of proxies in an array
  P_k = relative percent of the load that proxy k should handle 

  This is then combined with the previously computed hashes as

  Resultant_value = Combined_Hash * X_k


   * recalculate loadfactor for each proxy [borrow it from nginx-upstream-carp module]
   * X_1 = pow ((K*p_1), (1/K))
   * X_k = ([K-k+1] * [P_k - P_{k-1}])/(X_1 * X_2 * ... * X_{k-1})
   * X_k += pow ((X_{k-1}, {K-k+1})
   * X_k = pow (X_k, {1/(K-k+1)})
   */

  int K = _hostList.size();
  double P_last = 0.0; /* Empty P_0 */
  double Xn = 1.0; /* Empty starting point of X_1 * X_2 * ... * X_{x-1} */
  double X_last = 0.0; /* Empty X_0, nullifies the first pow statement */
  double K_sum = 0.0;

  // get weight total
  for (int k = 1; k <= K; k++)
  {
    K_sum += _hostList[k - 1]->weight;
  }
  
  // calculate LoadMulitplier for each Carp host
  for (int k = 1; k <= K; k++) {
    double Kk1 = (double) (K - k + 1);
    HashNode* p = _hostList[k - 1];
    p->loadFactor = p->weight / K_sum;
    p->loadMultiplier = (Kk1 * (p->loadFactor - P_last)) / Xn;
    p->loadMultiplier = p->loadMultiplier + pow(X_last, Kk1);
    p->loadMultiplier = pow(p->loadMultiplier, 1.0 / Kk1);
    Xn *= p->loadMultiplier;
    X_last = p->loadMultiplier;
    P_last = p->loadFactor;

  }
  return;
}

/*****************************************************************/
double
CarpHashAlgorithm::_getScore(HashNode* host, const string& url)
{
  // check input data
  if (url.empty())
    return 0;

  unsigned int combined = 0;

  // calculate Combined_Hash
  for (size_t ptr = 0; ptr < url.length(); ptr++)
    combined += ROTATE_LEFT(combined, 19) + url.c_str()[ptr];

  combined = combined ^ host->hash;
  combined += combined * 0x62531965;
  combined = ROTATE_LEFT(combined, 21);

  // return score
  return combined * host->loadMultiplier;
}

/*****************************************************************/
// define something we can sort
struct sortableContainer {

  sortableContainer()
  {
  };

  sortableContainer(double s, HashNode * h) : score(s), p(h)
  {
  };

  bool operator<(const sortableContainer & rhs) const
  {
    return score > rhs.score; // sport high to low
  }

  double score;
  HashNode *p;
};

std::vector<HashNode*>
CarpHashAlgorithm::_selectReplicateNodes(const string& url)
{
  vector<sortableContainer> scoreList;
  vector<HashNode*> replicateList;
  int remaining_replicates = _config->getReplicationFactor();
  GroupCountList group_counts = _config->getGroupCountList();

  // insert elements in the container
  for (size_t k = 0; k < _hostList.size(); k++) {
    scoreList.push_back(sortableContainer(_getScore(_hostList[k], url), _hostList[k]));
  }

  // sort
  std::sort(scoreList.begin(), scoreList.end());

  // select the first k nodes that are up and meet conditions
  for (size_t k = 0; remaining_replicates && k < scoreList.size(); k++) {
    int group_number = scoreList[k].p->group;
    int remaining_in_group = group_counts[group_number];
    if (remaining_in_group && scoreList[k].p->getStatus())
      replicateList.push_back(scoreList[k].p);
      remaining_replicates--;
      group_counts[group_number]--;
  }

  return replicateList;
}

HashNode*
CarpHashAlgorithm::_selectNode(const string& url)
{
  vector<sortableContainer> scoreList;

  // insert elements in the container
  for (size_t k = 0; k < _hostList.size(); k++) {
    scoreList.push_back(sortableContainer(_getScore(_hostList[k], url), _hostList[k]));
  }

  // sort
  std::sort(scoreList.begin(), scoreList.end());

  // select the first node that is up one
  for (size_t k = 0; k < scoreList.size(); k++) {
    if (scoreList[k].p->getStatus())
      return scoreList[k].p;
  }

  return NULL;
}
