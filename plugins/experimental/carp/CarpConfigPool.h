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
#ifndef __CARPCONFIGPOOL_H__
#define __CARPCONFIGPOOL_H__ 1

#include <string>

#include "CarpConfig.h"
#include "CarpHashAlgorithm.h"
#include <ts/ts.h>

struct CarpConfigAndHash
{
  CarpConfigAndHash() : _config(NULL), _hashAlgo(NULL), _lastLoad(0) {};

  CarpConfig*     _config;
  HashAlgorithm*  _hashAlgo;
  time_t          _lastLoad;
  std::string     _configPath;
  TSThread        _thread;

  ~CarpConfigAndHash() {
    delete _config;
    delete _hashAlgo;
  };

};
typedef std::map<std::string,CarpConfigAndHash *> CarpConfigList;
typedef std::map<std::string,CarpConfigAndHash *>::iterator CarpConfigListIter;

class CarpConfigPool
{
public:
  CarpConfigPool();
  
  ~CarpConfigPool();
  
  CarpConfigAndHash*    processConfigFile(std::string sFilename,bool isGlobal=false);
  HashAlgorithm*        getGlobalHashAlgo() { return _globalHash; }
  CarpConfig*           getGlobalConfig() { return _globalConfig; }
private:
  HashAlgorithm*  _globalHash;
  CarpConfig*     _globalConfig;
  CarpConfigList  _configList;
};

#endif


