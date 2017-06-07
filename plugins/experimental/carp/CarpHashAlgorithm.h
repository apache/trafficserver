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
#ifndef __CARPHASHALGORITHM_H__
#define __CARPHASHALGORITHM_H__ 1
/*
 *  Implement algorithm interface for CARP
 *  It is possible to replace algorithm with others.
 */

#include <algorithm>
#include <string>
#include <vector>
#include <math.h>
#include "CarpConfig.h"

#define ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32-(n))))

class CarpConfig;
class HashNode
{
public:

    HashNode(std::string n, unsigned int l, std::string scheme, double w, bool s,struct sockaddr_storage f, int g = DEFAULT_GROUP) :
      name(n), listenPort(l), scheme(scheme), weight(w), isSelf(s), forwardAddr(f), group(g)
    {
        hash = 0;
        loadFactor = 0;
        loadMultiplier = 0;
        _status = false;
        _hits = 0;
        _carp_noforwarded = 0;
        _carp_forwarded = 0;
        _statusTime = 0;
        _statusLatencyMs = 0;
    }

    std::string name;
    unsigned int listenPort;
    std::string scheme;
    unsigned int hash;
    double weight;
    double loadFactor;
    double loadMultiplier;
    bool isSelf;    
    struct sockaddr_storage forwardAddr;
    int group;
    
    void hit() { ++_hits; }
    void carpNoForward() { ++_carp_noforwarded; }
    void carpForward() { ++_carp_forwarded; }
    void dump(std::string& s);
    time_t getCheckTime() {return _statusTime;}
    uint64_t getLatency() {return _statusLatencyMs;};
    bool getStatus() { return _status; }
    void setStatus(bool s, time_t t, uint64_t lat)
    {
       _status = s;
       if (t) {
        _statusTime = t;
      }
       if (lat) {
        _statusLatencyMs = lat;
      }
    }
    const char *getSchemeString()
    {
      return scheme.c_str();
    }
    int getCarpNoForwarded()
    {
      return _carp_noforwarded;
    }
    int getCarpForwarded()
    {
      return _carp_forwarded;
    }
  
private:
    HashNode();
    bool _status; // true means host is up
    uint64_t _hits; // this is the total # of times a host is hit by carp
    uint64_t _carp_noforwarded; // this is the # of times a host selects itself 
    uint64_t _carp_forwarded; // this is the # of times a host selects another host
    time_t   _statusTime; // time when status was last updated
    uint64_t _statusLatencyMs;
};

// define common interface for each algorithm

class HashAlgorithm
{
public:
    HashAlgorithm(CarpConfig *cfg) : _config(cfg) {} ;
    virtual ~HashAlgorithm();
    
    /**
    getRemapProxy selects a host by executing the hash algorithm.

    @param url url to hash.
    @param last Pass in previously returned HashNode* to get next highest scoring node.
    @return Pointer to chosen HashNode or NULL if end of list reached.
    */
    virtual HashNode* getRemapProxy(const std::string& url) = 0;
    virtual std::vector<HashNode*> getRemapProxyList(const std::string& url) = 0;

    virtual void addHost(std::string name, unsigned int port, std::string scheme, double weight, bool bSelf, struct sockaddr_storage fwdAddr);
    virtual void addHost(HashNode* h);
    virtual void setStatus(const std::string& name, unsigned int port, bool status, time_t time, uint64_t latencyMs);
    virtual void setStatus(HashNode *hashNode,  bool status, time_t time, uint64_t latencyMs);
    virtual void algoInit() {};
    
    virtual void dump(std::string& s);
    HashNode*   findStatusByNameAndPort(const std::string& name, unsigned int port, size_t* index=NULL);
    size_t findHashNodeIndex(HashNode *node);
protected:
    std::vector<HashNode*> _hostList;
    CarpConfig*   _config;
    

};

// implement CARP algorithm

class CarpHashAlgorithm : public HashAlgorithm
{
public:
    CarpHashAlgorithm(CarpConfig *cfg) : HashAlgorithm(cfg)
    {
    };

    ~CarpHashAlgorithm()
    {
    };

    virtual HashNode*  getRemapProxy(const std::string& url);
    virtual std::vector<HashNode*> getRemapProxyList(const std::string& url);

    virtual void algoInit();

private:
    
    virtual unsigned int _calculateHash(const std::string& hostname);
    void _calculateLoadMultiplier();
    double _getScore(HashNode* host, const std::string& url);
    HashNode* _selectNode(const std::string& url);
    std::vector<HashNode*> _selectReplicateNodes(const std::string& url);
};

#endif
