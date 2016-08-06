/** @file

  Manage a list of CARP configurations

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
#include <sstream>

#include "Common.h"
#include "CarpConfigPool.h"
#include "UrlComponents.h"
#include "CarpHost.h"

#include <netdb.h>
#include <ts/ts.h>
 
using namespace std;

/*******************************************************************/
CarpConfigPool::CarpConfigPool()
{
  _globalHash = NULL;
  _globalConfig = NULL;
}

/*******************************************************************/
CarpConfigPool::~CarpConfigPool()
{
  for(CarpConfigListIter it=_configList.begin(); it != _configList.end(); it++) {
    it->second->_config->stop();
    delete it->second;
  }
}
/*******************************************************************/
static int initCarpConfigAndHash(CarpConfigAndHash * cch, string sFilename) {
  cch->_configPath = sFilename;
  cch->_config = new CarpConfig();
  TSAssert(cch->_config);

  if (!cch->_config->loadConfig(sFilename)) {
    return -1;
  }

  cch->_hashAlgo = new CarpHashAlgorithm(cch->_config);

  TSAssert(cch->_hashAlgo);

  CarpHostList* hostList = cch->_config->getHostList();
  // add hosts, etc to hash algo
  char szServerName[256];
  *szServerName = 0;

  if (!gethostname(szServerName, 255)) { // success!
    TSDebug(DEBUG_TAG_INIT, "using %s as server name to detect 'self'",
        szServerName);
  }
  char buf[1024];
  struct hostent self, *selfhe = getHostIp(string(szServerName), &self, buf,
      sizeof(buf));

  for (CarpHostListIter i = hostList->begin(); i != hostList->end(); i++) {
    HashNode *hashNode;
    bool bSelf = false;
    if (NULL != selfhe) { // check for 'self'
      // include hostname and port
      bSelf = isSelf((*i)->getName(), (*i)->getPort(), selfhe);
    }
    if (cch->_config->getHealthCheckPort() == -1) {	// did they specify 'PORT'?
      (*i)->setHealthCheckPort((*i)->getPort()); // set HC port from server spec'd port
    } else {
      (*i)->setHealthCheckPort(cch->_config->getHealthCheckPort()); // set HC port
    }
    string sHCUrl = cch->_config->getHealthCheckUrl();
    size_t pos = sHCUrl.find("{port}");
    if (pos != string::npos) { // need to replace '{port}' with servers port
      stringstream ss;
      ss << (*i)->getPort();
      sHCUrl.replace(pos, 6, ss.str()); // 6 = strlen of '{port}'
    }
    pos = sHCUrl.find("{host}");
    if (pos != string::npos) {
      sHCUrl.replace(pos, 6, (*i)->getName());
    }
    (*i)->setHealthCheckUrl(sHCUrl); // set HC Url

    // Look up host and create addr struct for healthchecks
    char hBuf[1024];
    struct hostent host, *hosthe = getHostIp((*i)->getName(), &host, hBuf,
        sizeof(hBuf));
    if (hosthe) {
      // convert hostent to sockaddr_in structure
      sockaddr_in hIn;
      memcpy(&hIn.sin_addr, hosthe->h_addr_list[0], hosthe->h_length);
      hIn.sin_port = htons((*i)->getHealthCheckPort()); // set port
      if (hosthe->h_length == 4) { // size match IPv4? assume such
        hIn.sin_family = AF_INET;
      } else { // assume IPv6
        hIn.sin_family = AF_INET6;
      }
      (*i)->setHealthCheckAddr(
          reinterpret_cast<struct sockaddr_storage &>(hIn));
      hIn.sin_port = htons((*i)->getPort());
      hashNode = new HashNode((*i)->getName(), (*i)->getPort(),
          (*i)->getScheme(), (*i)->getWeight(), bSelf,
          reinterpret_cast<struct sockaddr_storage &>(hIn),
          (*i)->getGroup());
      cch->_hashAlgo->addHost(hashNode);
    } else {
      //Config error or dns error. Should not continue
      TSError("carp: error get peer address of host '%s'", (*i)->getName().c_str());
      return -1;
    }

    HttpFetch *f = new HttpFetch(sHCUrl, cch->_hashAlgo, hashNode);
    cch->_config->addHealthCheckClient(f);
  }
  string diag;
  cch->_config->dump(diag);
  TSDebug(DEBUG_TAG_INIT, "Carp Configuration\n%s", diag.c_str());

  // tell algo we are done configuring it
  cch->_hashAlgo->algoInit();

  return 1;
}

/*******************************************************************/
int
cleanHandler(TSCont cont, TSEvent event, void *edata) {
  CarpConfigAndHash * cch = (CarpConfigAndHash *)TSContDataGet(cont);
  delete cch;
  TSContDestroy(cont);

  return 1;
}

/*******************************************************************/
CarpConfigAndHash*
CarpConfigPool::processConfigFile(string sFilename,bool isGlobal)
{

  CarpConfigAndHash* cch = _configList[sFilename];

  if ( NULL == cch ) { // new config file
    cch = new CarpConfigAndHash();
    _configList[sFilename] = cch;
    TSDebug(DEBUG_TAG_INIT, "processing new config file '%s'", sFilename.c_str());
    if (initCarpConfigAndHash(cch, sFilename) < 0 ) {
    	return NULL;
    }

    if(isGlobal) { // extract global setting(s) from this config file and save locally
      _globalHash = cch->_hashAlgo;
      _globalConfig = cch->_config;
    }
    // create and start health watcher thread
    cch->_thread = TSThreadCreate( CarpConfigHealthCheckThreadStart,static_cast<void *> (cch) );
  } else { // config reload

     TSDebug(DEBUG_TAG_HEALTH, "Reload the config file '%s'", sFilename.c_str());
     CarpConfigAndHash *newCCH = new CarpConfigAndHash();
     CarpConfigAndHash *oldCCH = cch;
     if (initCarpConfigAndHash(newCCH, sFilename) < 0 ) {
        return NULL;
     }
     /*
      * Find the status of current Host in previous HashAlgo, and
      * then assign the value to the new HashAlgo.
      */
     size_t index;
     vector<CarpHost*> *list = newCCH->_config->getHostList();
     for (unsigned int i = 0; i < list->size(); i++) {
        string name = (*list)[i]->getName();
        unsigned int port = (unsigned int)(*list)[i]->getPort();
         HashNode * node = oldCCH->_hashAlgo->findStatusByNameAndPort(name,port, &index);
         if (node != NULL) {
            newCCH->_hashAlgo->setStatus(name, port, node->getStatus(), node->getCheckTime(), node->getLatency());
         }
     }
     newCCH->_lastLoad = oldCCH->_lastLoad;
     oldCCH->_config->stop();
     if(isGlobal) {
       _globalHash = newCCH->_hashAlgo;
       _globalConfig = newCCH->_config;
     }

     newCCH->_thread = TSThreadCreate( CarpConfigHealthCheckThreadStart,static_cast<void *> (newCCH) );
     // this operation do not need lock
     // previous http requests would use the oldCCH for carp operation
     // later http requests would use this newCCH for carp operation
     _configList[sFilename] = newCCH;
     cch = newCCH;
     
     // delay the free of the old _hashAlgo and _config
     TSCont cleanCont = TSContCreate(cleanHandler, NULL);
     TSContDataSet(cleanCont, (void*)oldCCH);
     TSContSchedule(cleanCont, oldCCH->_config->getConfigCheckFreq()*1000*2, TS_THREAD_POOL_TASK);

  }

  return cch;
}
