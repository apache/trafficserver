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

//////////////////////////////////////////////////////////////
// Read CARP configuration file
// [Servers]
// host1.yahoo.com:4080 weight=2      # port 4080 on host1.yahoo.com with weight factor of 2
// host2.yahoo.com                    # port 80 on host2.yahoo.com with (default) weight factor of 1
// 
// [Values]
// healthcheck={host}:8001/status.html
// healthfreq=30
// global=on
//

#include <stdlib.h> 
#include <stdio.h> 
#include <memory.h> 
#include <ts/ts.h>
#include <sys/time.h>
#include <errno.h>

#include <sstream>

#include "CarpConfig.h"
#include "Common.h"
#include "CarpConfigPool.h"

using namespace std;

#define DEFAULT_HEALTH_CHECK_FREQ 30  // 30 second period for health checks
#define DEFAULT_HEALTH_CHECK_PORT 80  // default to makeing healthcheck requests against port 80
#define DEFAULT_CONFIG_RELOAD_FREQ 30 // 30 seconds used in TSContSchedule
#define DEFAULT_PORT 80  // default to makeing requests against port 80
#define DEFAULT_WEIGHT 1  // default weight
#define DEFAULT_SCHEME "http"
#define DEFAULT_REPLICATION_FACTOR 1

#define HTTPS_PORT 443
#define SPECIAL_PORT "{port}"
#define HTTP_SCHEME "http://"
#define HTTPS_SCHEME "https://"

// config section headers
static const char *const SECTION_SERVERS_STR = "[Servers]";
static const char *const SECTION_VALUES_STR = "[Values]";

// key strings
static const char *const KEY_HEALTHCHECK_STR = "healthcheck";
static const char *const KEY_HEALTHFREQ_STR = "healthfreq";
static const char *const KEY_RELOADFREQ_STR = "reloadfreq";
static const char *const KEY_HCTIMEOUT_STR =  "hctimeout";
static const char *const KEY_BLACKLIST_STR = "blacklist";
static const char *const KEY_WHITELIST_STR = "whitelist";
static const char *const KEY_MODE_STR = "mode";
static const char *const KEY_ALLOWFWDPORT_STR = "allowfwdport";
static const char *const KEY_REPLICATIONFACTOR_STR = "replicationfactor";

// parameter strings
static const char *const WEIGHT_EQUALS_STRING = "weight=";
static const char *const GROUP_EQUALS_STRING = "group=";
static const char *const KEY_MODE_PREREMAP_STR = "pre-remap";
static const char *const KEY_MODE_POSTREMAP_STR = "post-remap";


/**********************************************************/
bool
getInt(char** pptr, int *val)
{
  bool bReturn = false;
  errno = 0;

  char *ptr = *pptr;
  char *endptr;

  int v = strtol(ptr, &endptr, 0);	

  if (errno == 0 && endptr != ptr) {
    bReturn = true;
    *pptr = endptr;
    *val = v;
  }

  return bReturn;
}

/**********************************************************/
// [http[s]://]host[:port]/path
bool
getHostAndPort(char** pptr,string* sHost,int* iPort, string* sScheme)
{
  bool bReturn = false;
  char* ptr = *pptr;
  char *endptr;

  //skip leading white space
  while (*ptr && isspace(*ptr)) ++ptr;

  if (*ptr) { // validate not end of string
    if(strncmp(ptr, HTTP_SCHEME, strlen(HTTP_SCHEME)) == 0) {
      ptr += strlen(HTTP_SCHEME);
    } else if(strncmp(ptr, HTTPS_SCHEME, strlen(HTTPS_SCHEME)) == 0) {
      *iPort = HTTPS_PORT;
      *sScheme = TS_URL_SCHEME_HTTPS;
      ptr += strlen(HTTPS_SCHEME);
    }
    
    char* ptemp = ptr; // start of host
    //find white space or ':' or '/'
    while (*ptr && !isspace(*ptr) && *ptr != ':' && *ptr != '/') ++ptr;

    *sHost = string(ptemp, (ptr - ptemp));
    // skip white space (if any) after host
    while (*ptr && isspace(*ptr)) ++ptr;
    bReturn = true;
    
    if (*ptr) {// have more to parse
      // need to get port number?
      if (*ptr == ':') {
        ++ptr;
        if (!getInt(&ptr, iPort)) {
          // could be our special 'PORT' value, check for that
          if(!strncmp(ptr, SPECIAL_PORT, strlen(SPECIAL_PORT))) { // yes, is '{port}'
            *iPort = -1;
            bReturn = true;
          } else { // really was an error
            TSError("carp: error parsing port number from '%s'", *pptr);
            bReturn = false;
          }
        } else {
          // if port number is 443, treat the scheme as https
          if (*iPort == HTTPS_PORT) {
            *sScheme = TS_URL_SCHEME_HTTPS;
          }
        }
      }
    }
  }
  if(bReturn) {
    *pptr = ptr;
  }
  
  return bReturn;
}
/**********************************************************/
CarpConfig::CarpConfig()
{
  _healthCheckPort = DEFAULT_HEALTH_CHECK_PORT;
  _healthCheckFreq = DEFAULT_HEALTH_CHECK_FREQ;
  _configCheckFreq = DEFAULT_CONFIG_RELOAD_FREQ;
  _healthCheckTimeout = DEFAULT_HEALTH_CHECK_TIMEOUT;
  _setExit = 0;
  _mode = PRE;
  _allowForwardPort = 0;
  _replicationFactor = DEFAULT_REPLICATION_FACTOR;
  _nGroups = 0;
}

/**********************************************************/
CarpConfig::~CarpConfig()
{
  for(size_t ptr = 0; ptr < _servers.size(); ptr++) {
    delete _servers[ptr];
  }

  for(size_t ptr = 0; ptr < _httpClients.size(); ptr++) {
    delete _httpClients[ptr];
  }
}

/**********************************************************/
bool
CarpConfig::loadConfig(string filename)
{
  TSFile file;
  GroupCountList group_counts;
  GroupCountListIter group_counts_it;

  // attempt to open config file assuming path provided
  TSDebug(DEBUG_TAG_INIT, "Trying to open config file in this path: %s", filename.c_str());
  file = TSfopen(filename.c_str(), "r");
  if (file == NULL) {
    TSError("Failed to open carp config file %s. Trying relative path.", filename.c_str());
    std::string path = TSConfigDirGet();
    path += '/';
    path += filename;
    if (NULL == (file = TSfopen(path.c_str(), "r"))) {
	TSError("Failed to open carp config file %s with relative path.", path.c_str());
    	return false;
    }
  }

  TSDebug(DEBUG_TAG_INIT, "Successfully opened config file");

  char buffer[1024];
  memset(buffer, 0, sizeof(buffer));

  enum CONFIG_SECTION {
    CFG_NONE_SECTION = 0,
    CFG_SERVER_SECTION,
    CFG_VALUES_SECTION
  };
  int cfg_section = CFG_NONE_SECTION;
  bool done_parsing = false;
  while (TSfgets(file, buffer, sizeof(buffer) - 1) != NULL && !done_parsing) {

    char *eol = 0;
    // make sure line was not bigger than buffer
    if ((eol = strchr(buffer, '\n')) == NULL) {
      TSError("carp config line was too long, did not get a good line in cfg, skipping, line: %s", buffer);
      memset(buffer, 0, sizeof(buffer));
      continue;
    }

    // make sure line has something useful on it
    if (eol - buffer < 2 || buffer[0] == '#' || isspace(buffer[0])) {
      memset(buffer, 0, sizeof(buffer));
      continue;
    }
    
    // remove ending CR/LF
    *eol = 0;

    // check if we are changing sections
    if (strncasecmp(buffer, SECTION_SERVERS_STR, strlen(SECTION_SERVERS_STR)) == 0) {
      cfg_section = CFG_SERVER_SECTION;
      TSDebug(DEBUG_TAG_INIT, "Parsing [Servers] section");
      continue;
    } else if (strncasecmp(buffer, SECTION_VALUES_STR, strlen(SECTION_VALUES_STR)) == 0) {
      cfg_section = CFG_VALUES_SECTION;
      TSDebug(DEBUG_TAG_INIT, "Parsing [Values] section");
      continue;
    }

    //TSDebug(DEBUG_TAG_INIT, "config line input:'%s'", buffer);

    switch (cfg_section) {
    case CFG_SERVER_SECTION:
    {
      string sHost;
      int iPort = DEFAULT_PORT;
      int iWeight = DEFAULT_WEIGHT;
      int iGroup = DEFAULT_GROUP;
      string sScheme = DEFAULT_SCHEME;
      bool bSuccess = true;

      char* ptr = buffer;

      if(!getHostAndPort(&ptr,&sHost,&iPort, &sScheme)) {
         TSError("carp: error parsing port number from '%s'", ptr);
      }
      
      while (*ptr) {
         // skip white space (if any)
         while (*ptr && isspace(*ptr)) ++ptr;

         if (*ptr) { // next we could find weight= or group=
            if (strncmp(ptr, WEIGHT_EQUALS_STRING, strlen(WEIGHT_EQUALS_STRING)) == 0) {
               ptr += strlen(WEIGHT_EQUALS_STRING);
               if (!getInt(&ptr, &iWeight)) {
                  TSError("carp: error parsing weight value from '%s'", buffer);
                  bSuccess = false;
                  continue;
               }
            } else if (strncmp(ptr, GROUP_EQUALS_STRING, strlen(GROUP_EQUALS_STRING)) == 0) {
               ptr += strlen(GROUP_EQUALS_STRING);
               if (!getInt(&ptr, &iGroup)) {
                  TSError("carp: error parsing group value from '%s'", buffer);
                  bSuccess = false;
                  continue;
               }
            } else {
               TSError("carp: error parsing from line '%s'", buffer);
               // malformed entry, skip to next space
               while (*ptr && !isspace(*ptr)) ++ptr;
               bSuccess = false;
               continue;
            }
         }
      }
      
      if (!bSuccess) continue;

      group_counts_it = group_counts.find(iGroup);
      if (group_counts_it != group_counts.end()) {
         group_counts[iGroup]++;
      } else {
         _nGroups++;
         group_counts[iGroup] = 1;
      }

      TSDebug(DEBUG_TAG_INIT, "Host = %s, port=%d, weight=%d, group=%d", sHost.c_str(), iPort, iWeight, iGroup);
      CarpHost* host=new CarpHost(sHost, iPort, sScheme, iWeight, iGroup);
      TSAssert(host != NULL);
      // store the parsed data
      addHost(host);
      break;
    }
    case CFG_VALUES_SECTION:
    {
      char* ptr = buffer;

      //skip leading white space
      while (*ptr && isspace(*ptr)) ++ptr;

      //find end of key
      while (*ptr && !isspace(*ptr) && *ptr != '=') ++ptr;

      string sKey = string(buffer, (ptr - buffer));

      // skip white space (if any) after key
      while (*ptr && isspace(*ptr)) ++ptr;
      if (*ptr != '=') {
        TSError("carp: expecting '=' after key in line '%s'", buffer);
        continue;
      }

      // skip '=' and white space (if any) after '='
      ++ptr;
      while (*ptr && isspace(*ptr)) ++ptr;

      char* ptemp = ptr; // get start of value

      // find end of value
      while (*ptr && !isspace(*ptr)) ++ptr;
      string sValue = string(ptemp, ptr - ptemp);

      TSDebug(DEBUG_TAG_INIT, "Key=%s Value=%s", sKey.c_str(), sValue.c_str());

      char* pTemp = (char *)sValue.c_str();
      if (sKey.compare(KEY_HEALTHCHECK_STR) == 0) {
        string sH;
        int iP;
        string scheme;
        _healthCheckUrl = string(pTemp);
        if (!getHostAndPort(&pTemp, &sH, &iP, &scheme)) {
          TSError("carp: error parsing host and/or port number from '%s'", buffer);
        }
        
        // store the parsed data
        _healthCheckPort = iP;
        TSDebug(DEBUG_TAG_INIT, "healthcheck Url=%s port=%d", _healthCheckUrl.c_str(), _healthCheckPort);

      } else if (sKey.compare(KEY_HEALTHFREQ_STR) == 0) {
        int iFreq;
        if (!getInt(&pTemp, &iFreq)) {
           TSError("carp: error parsing number from '%s'", buffer);
        } else {
          TSDebug(DEBUG_TAG_INIT, "healthcheck freq=%d", iFreq);
           // store the parsed data
          _healthCheckFreq = iFreq;
        }
      } else if (sKey.compare(KEY_HCTIMEOUT_STR) == 0) {
        int iFreq;
        if (!getInt(&pTemp, &iFreq)) {
           TSError("carp: error parsing number from '%s'", buffer);
        } else {
          TSDebug(DEBUG_TAG_INIT, "healthcheck timeout value=%d", iFreq);
           // store the parsed data
          _healthCheckTimeout = iFreq;
        }
      } else if (sKey.compare(KEY_RELOADFREQ_STR) == 0) {
        int lFreq;
        if (!getInt(&pTemp, &lFreq)) {
          TSError("carp: error parsing number from '%s'", buffer);
        } else {
          TSDebug(DEBUG_TAG_INIT, "config reload freq=%d", lFreq);
          _configCheckFreq = lFreq;
        }
      } else if (sKey.compare(KEY_BLACKLIST_STR) == 0) {
        vector<string> results;
        stringExplode(sValue, string(","), &results);
        for (vector<string>::iterator it = results.begin(); it != results.end();
            it++) {
          TSDebug(DEBUG_TAG_INIT, "Adding blacklist hostname %s",
              (*it).c_str());
          _blackList.insert(*it);
        }
      } else if (sKey.compare(KEY_WHITELIST_STR) == 0) {
        vector<string> results;
        stringExplode(sValue, string(","), &results);
        for(vector<string>::iterator it=results.begin(); it != results.end(); it++) {
          TSDebug(DEBUG_TAG_INIT, "Adding whitelist hostname %s",(*it).c_str() );
          _whiteList.insert(*it);
        }
      } else if (sKey.compare(KEY_MODE_STR) == 0) {
        if(sValue.compare(KEY_MODE_PREREMAP_STR) == 0) {
          _mode = PRE;
        } else if(sValue.compare(KEY_MODE_POSTREMAP_STR) == 0) {
          _mode = POST;
        } else {
          TSError("carp: invalid mode in '%s'", buffer);
        }
      } else if (sKey.compare(KEY_ALLOWFWDPORT_STR) == 0) {
        int iPort;
        if (!getInt(&pTemp, &iPort)) {
           TSError("carp: error parsing number from '%s'", buffer);
        }
        else {
          TSDebug(DEBUG_TAG_INIT, "Allow forwarding port=%d", iPort);
           // store the parsed data
          _allowForwardPort = iPort;
        }
      } else if (sKey.compare(KEY_REPLICATIONFACTOR_STR) == 0) {
        int iFactor;
        if (!getInt(&pTemp, &iFactor)) {
           TSError("carp: error parsing number from '%s'", buffer);
        }
        else {
          TSDebug(DEBUG_TAG_INIT, "Replication factor=%d", iFactor);
           // store the parsed data
          _replicationFactor = iFactor;
        }
 
      } else
        TSError("carp found bad setting under Values section '%s'", buffer);
      break;
    }
    default:
      // ignore bad cfg_section
      TSDebug(DEBUG_TAG_INIT, "hit default in switch, ignoring extra input '%s'",buffer);
      break;
    };
  } //while

  if (_healthCheckTimeout > _healthCheckFreq - 1 ) {
    TSDebug(DEBUG_TAG_INIT, "Healthcheck timeout too large, setting to %d.", _healthCheckFreq - 1);
    _healthCheckTimeout = _healthCheckFreq - 1;
  }

  TSfclose(file);

  if (_blackList.size() && _whiteList.size() ) {
    TSError("Carp configured with both blacklist and whitelist, blacklist will be ignored");
  }

  if (_nGroups > _replicationFactor) {
    TSError("Too many groups configured! Failing config.");
    return false;
  } else {
    TSDebug(DEBUG_TAG_INIT, "Group Config is as follows:");
    for (map<int, int>::const_iterator it = group_counts.begin(); it != group_counts.end(); it++) {
       TSDebug(DEBUG_TAG_INIT, "Group %d has %d members.", it->first, it->second);
       _group_count_list[it->first] = it->second;
    }
  }

  return true;
}

/**********************************************************/
void
CarpConfig::addHost(CarpHost* host) {
  _servers.push_back(host);
}

void
CarpConfig::addHealthCheckClient(HttpFetch *client) {
  client->setHealthcheckTimeout(_healthCheckTimeout);
  _httpClients.push_back(client);

}

void
CarpConfig::setPath(string path) {
  _configPath = path;
}

string
CarpConfig::getPath() {
  return _configPath;
}

/**********************************************************/
void *
CarpConfigHealthCheckThreadStart(void* data)
{
  CarpConfigAndHash* cch = static_cast<CarpConfigAndHash *> (data);
  TSAssert(cch);
  return cch->_config->run(cch->_hashAlgo);
}

/**********************************************************/
bool 
CarpConfig::isBlackListed(const string& sHost)
{
  return (_blackList.find(sHost) !=  _blackList.end());
}

/**********************************************************/
bool 
CarpConfig::isWhiteListed(const string& sHost)
{
  return (_whiteList.find(sHost) !=  _whiteList.end());
}

/**********************************************************/
/*
   perform healthchecks on the hosts and mark them up/down
*/

void*
CarpConfig::run(HashAlgorithm *hash) {

  // every httpClient would send a health request to each peer
  // would be nice to not wait and just 'go' when the server is 100% up
  sleep(5);
  // send healthcheck request to each peer
  while (!_setExit) {
    TSDebug(DEBUG_TAG_HEALTH, "BEGIN HEALTH CHECKING");
    if (!_setExit) {
      for (size_t i = 0; i < _servers.size(); i++) {
        TSDebug(DEBUG_TAG_HEALTH, "entring loop, list size is %lu",
            _servers.size());
        TSDebug(DEBUG_TAG_HEALTH, "Fetching '%s' from %s",
            _servers[i]->getHealthCheckUrl().c_str(),
            _servers[i]->getName().c_str());
        if (_httpClients[i] -> isReady()) {
          _httpClients[i]->makeAsyncRequest(
                  reinterpret_cast<const sockaddr *>(_servers[i]->getHealthCheckAddr()));
        }
      }

      int iSleepCounter = _healthCheckFreq;
      while (!_setExit && iSleepCounter--) {
        sleep(1);
      }
    }
  }
  _setExit = 2;

  return NULL;
}

/**********************************************************/
void CarpConfig::stop() {
  _setExit = 1;
  while (_setExit == 1) {
    sleep(1);
  }
}

/**********************************************************/
void CarpConfig::dump(string &s) {
  stringstream ss;

  ss << "Health check port = " << _healthCheckPort << endl;
  ss << "Health check path = " << _healthCheckUrl << endl;
  ss << "Health check frequency = " << _healthCheckFreq << endl;
  ss << "Health check timeout = "  << _healthCheckTimeout << endl;
  ss << "Config check frequency = " << _configCheckFreq << endl;
  ss << endl;

  s += ss.str();

  for (unsigned int i = 0; i < _servers.size(); i++)
    _servers[i]->dump(s);

}
