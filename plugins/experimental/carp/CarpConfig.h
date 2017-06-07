/** @file

  Loads the CARP configuration

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
#ifndef __CARPCONFIG_H__
#define __CARPCONFIG_H__ 1

#include <string>
#include <vector>
#include <map>
#include <set>

#include "CarpHost.h"
#include "HttpFetch.h"
#include "CarpHashAlgorithm.h"

#define DEFAULT_GROUP 1  // default group

class HashAlgorithm;
class HttpFetch;

typedef std::vector<CarpHost *> CarpHostList;
typedef std::vector<CarpHost *>::iterator CarpHostListIter;
typedef std::vector<HttpFetch *> HttpClientList;
typedef std::vector<HttpFetch *>::iterator HttpClientListIter;
typedef std::map<int, int> GroupCountList;
typedef std::map<int, int>::iterator GroupCountListIter;

typedef std::set<std::string> BlackListContainer;

void *CarpConfigHealthCheckThreadStart(void* data);

class CarpConfig
{
public:
    enum  CarpMode { PRE, POST };
    CarpConfig();
    virtual ~CarpConfig();
    
    bool loadConfig(std::string filename);
    void addHost(CarpHost* h);
    void addHealthCheckClient(HttpFetch *client);
    void setPath(std::string path);
    std::string getPath();
    int getConfigCheckFreq() {
      return _configCheckFreq;
    }
    
    void dump(std::string &s);
    
    CarpHostList* getHostList() { return &_servers; };
    
    int getHealthCheckPort() { return _healthCheckPort; };
    const std::string& getHealthCheckUrl() { return _healthCheckUrl; };
    
    virtual void* run(HashAlgorithm *hash); 
    virtual void stop(void);  // tell thread to exit

    bool isBlackListed(const std::string& sHost);
    bool hasWhiteList() { return static_cast<bool>(_whiteList.size()); }
    bool isWhiteListed(const std::string& sHost);
    
    CarpMode getMode() { return _mode; }
    int   getAllowedForwardPort() { return _allowForwardPort; }
    int   getReplicationFactor()  { return _replicationFactor; }
    int   getNGroups() { return _nGroups; }
    GroupCountList getGroupCountList() { return _group_count_list; };
private:
    int         _healthCheckPort;
    std::string _healthCheckUrl;
    int         _healthCheckFreq;
    int         _healthCheckTimeout;

    int         _configCheckFreq;
    
    std::string _configPath;

    CarpHostList        _servers;
    HttpClientList      _httpClients;
    BlackListContainer  _blackList;
    BlackListContainer  _whiteList;
    // healthcheck thread stuff
    volatile int        _setExit;
      
    CarpMode            _mode;
    int                 _allowForwardPort;
    int                 _replicationFactor;
    int                 _nGroups;
    GroupCountList      _group_count_list;
};
   
#endif

