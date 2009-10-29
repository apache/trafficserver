/** @file

  A brief file description

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


#ifndef __INK_API_AAA_H_
#define __INK_API_AAA_H_


#include "InkAPI.h"


#ifdef __cplusplus
extern "C"
{
#endif                          /* __cplusplus */

/**
 * AAA API
 */


  inkapi INKReturnCode INKUserPolicyLookup(INKHttpTxn txnp, void **user_info);
  inkapi INKReturnCode INKHttpTxnBillable(INKHttpTxn txnp, int bill, const char *eventName);

/**
 * END AAA API
 */

/**
  * AAA policy contiunation set API
  */
  inkapi void INKPolicyContSet(INKCont p);
  inkapi INKReturnCode INKUserPolicyFetch(INKU32 ip, char *name);
/**
  * End AAA policy contiunation set API
  */


/**
 * AAA USER CACHE API
 */

  typedef unsigned int UINT4;

  typedef enum
  {
    POLICY_FETCHING = 1,
    POLICY_FETCHED,
    LOGGED_OFF,
    REASSIGNED
  } status_t;


  struct USER_INFO
  {
    INKU32 ipaddr;
    char *name;
    status_t status;
    int len;                    /* the length of policy string with "\0" in the middle */
    void *policy;
    struct USER_INFO *next;
  };

  inkapi void UserCacheInit();  /* it must be called by PluginInit */
  inkapi void UserCacheDelete(INKU32 ip);
  inkapi int UserCacheInsert(INKU32 ip, char *name, status_t status, void *policy, int len);
  inkapi struct USER_INFO *UserCacheLookup(INKU32 ip, INKCont caller_cont);
  inkapi int UserCacheModify(INKU32 ip, char *name, status_t status, void *policy, int len);
  inkapi void UserCacheCloneFree(struct USER_INFO *a);


/**
 * END AAA USER CACHE API
 */


#ifdef __cplusplus
}
#endif                          /* __cplusplus */

#endif                          /* __INK_API_AAA_H_ */
