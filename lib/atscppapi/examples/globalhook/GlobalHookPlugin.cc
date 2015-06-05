/**
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

#include <iostream>
#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/PluginInit.h>
#include <../ts/Diags.h>
using namespace atscppapi;
using namespace std;
class GlobalHookPlugin : public GlobalPlugin
{
public:
  GlobalHookPlugin() { registerHook(HOOK_READ_REQUEST_HEADERS_PRE_REMAP); }

  virtual void
  handleReadRequestHeadersPreRemap(Transaction &transaction)
  {
   //////////////////////////////////////////////
      int value;
      float fvalue;
      char* str;
      std::string svalue;
      int slength;
      std::string keyval("proxy.config.http.slow.log.threshold");

    TSOverridableConfigKey okey;
    TSRecordDataType type;
// initialise string
    transaction.configStringGet(TS_CONFIG_HTTP_RESPONSE_SERVER_STR,&svalue,&slength);
    Note("value for http response str  %s , %d\n",svalue.c_str(),slength);
// setting values
    transaction.configIntSet(TS_CONFIG_HTTP_CACHE_HEURISTIC_MIN_LIFETIME,4000);
    if (false==transaction.configFloatSet(TS_CONFIG_HTTP_CACHE_HEURISTIC_LM_FACTOR, 0.15))
        Note("Could not set the lm factor\n");

    str= new char[svalue.length()+1];
    strcpy(str,svalue.c_str());
    str[0]='X';
    svalue.assign(str);
    transaction.configStringSet(TS_CONFIG_HTTP_RESPONSE_SERVER_STR,svalue,slength);
//getting values
    transaction.configIntGet(TS_CONFIG_HTTP_CACHE_HEURISTIC_MIN_LIFETIME,&value);
    Note("value for heuristic min lifetime %d\n",value);
    transaction.configFloatGet(TS_CONFIG_HTTP_CACHE_HEURISTIC_LM_FACTOR, &fvalue);
    Note("value for heuristic lm factor %f\n",fvalue);
    transaction.configStringGet(TS_CONFIG_HTTP_RESPONSE_SERVER_STR,&svalue,&slength);
    Note("value for http response str  %s , %d\n",svalue.c_str(),slength);

    if(true==transaction.configFind(keyval.c_str(),-1,&okey,&type))
       Note("found %d\n",okey);
///////////////////////////////////////////////////////////////////////
    std::cout << "Hello from handleReadRequesHeadersPreRemap!" << std::endl;
    transaction.resume();
  }
};

void
TSPluginInit(int argc ATSCPPAPI_UNUSED, const char *argv[] ATSCPPAPI_UNUSED)
{
  atscppapi::TSRegisterPlugin( std::string("ghp"),  std::string("ghp"),  std::string("ghp"));
  std::cout << "Hello from " << argv[0] << std::endl;  
  new GlobalHookPlugin();
}
