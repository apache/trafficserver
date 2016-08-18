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

/***********************************************************************
 * CfgContext.h
 *
 * Describes the CfgContext class. CfgContext has file name and set of Ele's
 ***********************************************************************/

#ifndef _CFG_CONTEXT_IMPL_H_
#define _CFG_CONTEXT_IMPL_H_

#include "mgmtapi.h"
#include "CfgContextDefs.h"
#include "GenericParser.h" /* use for TokenList */

#include "ts/List.h"

/**********************************************************************
 * CfgEleObj
 *
 * abstract base class; basic element in a CfgContext
 **********************************************************************/
class CfgEleObj
{
public:
  /* Each subclass must provide two constructors:
     using INK<file>Ele or a TokenList */

  virtual ~CfgEleObj() {} // virtual destructor
  virtual char *formatEleToRule() = 0;
  virtual bool isValid()          = 0;

  /* these are implemented as inline functons by subclasses */
  virtual TSCfgEle *getCfgEle()     = 0; /* returns actual ele */
  virtual TSCfgEle *getCfgEleCopy() = 0; /* returns copy of ele */
  virtual TSRuleTypeT getRuleType() = 0;

  LINK(CfgEleObj, link);

protected:
  bool m_valid; /* stores if Ele has valid fields; by default true */
};

/********************************************************************
 * Ele Subclasses
 ********************************************************************/
/* Each subclass can be constructed from a TokenList (creates the Ele)
 * or from passing in the Ele itself. Typically, objects with dynamically
 * allocated memory should also have a copy constructor and an
 * overloaded assignment operator. However, sine these subclasses function
 * more as wrappers for the C based Ele structs, we only require that
 * each subclass implements a function which returns a dynamic copy of
 * its m_ele data member
 */

/* CommentEle */
class CommentObj : public CfgEleObj
{
public:
  CommentObj(char *comment);
  ~CommentObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual TSCfgEle *getCfgEleCopy();
  virtual TSCfgEle *
  getCfgEle()
  {
    return (TSCfgEle *)m_ele;
  }
  virtual TSRuleTypeT
  getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  INKCommentEle *m_ele;
};

/* cache.config ***************************************************/
class CacheObj : public CfgEleObj
{
public:
  CacheObj(TSCacheEle *ele);
  CacheObj(TokenList *tokens); // creates the ele
  ~CacheObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual TSCfgEle *getCfgEleCopy();
  virtual TSCfgEle *
  getCfgEle()
  {
    return (TSCfgEle *)m_ele;
  }
  virtual TSRuleTypeT
  getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  TSCacheEle *m_ele;
};

/* congestion.config ***************************************************/
class CongestionObj : public CfgEleObj
{
public:
  CongestionObj(TSCongestionEle *ele);
  CongestionObj(TokenList *tokens); // creates the ele
  ~CongestionObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual TSCfgEle *getCfgEleCopy();
  virtual TSCfgEle *
  getCfgEle()
  {
    return (TSCfgEle *)m_ele;
  }
  virtual TSRuleTypeT
  getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  TSCongestionEle *m_ele;
};

/* hosting.config ************************************************/
class HostingObj : public CfgEleObj
{
public:
  HostingObj(TSHostingEle *ele);
  HostingObj(TokenList *tokens); // creates the ele
  ~HostingObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual TSCfgEle *getCfgEleCopy();
  virtual TSCfgEle *
  getCfgEle()
  {
    return (TSCfgEle *)m_ele;
  }
  virtual TSRuleTypeT
  getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  TSHostingEle *m_ele;
};

/* icp.config ************************************************/
class IcpObj : public CfgEleObj
{
public:
  IcpObj(TSIcpEle *ele);
  IcpObj(TokenList *tokens); // creates the ele
  ~IcpObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual TSCfgEle *getCfgEleCopy();
  virtual TSCfgEle *
  getCfgEle()
  {
    return (TSCfgEle *)m_ele;
  }
  virtual TSRuleTypeT
  getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  TSIcpEle *m_ele;
};

/* ip_allow.config    *********************************************/
class IpAllowObj : public CfgEleObj
{
public:
  IpAllowObj(TSIpAllowEle *ele);
  IpAllowObj(TokenList *tokens);
  ~IpAllowObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual TSCfgEle *getCfgEleCopy();
  virtual TSCfgEle *
  getCfgEle()
  {
    return (TSCfgEle *)m_ele;
  }
  virtual TSRuleTypeT
  getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  TSIpAllowEle *m_ele;
};

/* logs_xml.config    ********************************************/
class LogFilterObj : public CfgEleObj
{
public:
  LogFilterObj(TSLogFilterEle *ele);
  LogFilterObj(TokenList *tokens);
  ~LogFilterObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual TSCfgEle *getCfgEleCopy();
  virtual TSCfgEle *
  getCfgEle()
  {
    return (TSCfgEle *)m_ele;
  }
  virtual TSRuleTypeT
  getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  TSLogFilterEle *m_ele;
};

class LogFormatObj : public CfgEleObj
{
public:
  LogFormatObj(TSLogFilterEle *ele);
  LogFormatObj(TokenList *tokens);
  ~LogFormatObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual TSCfgEle *getCfgEleCopy();
  virtual TSCfgEle *
  getCfgEle()
  {
    return (TSCfgEle *)m_ele;
  }
  virtual TSRuleTypeT
  getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  TSLogFormatEle *m_ele;
};

/* logs.config ***************************************************/
class LogObjectObj : public CfgEleObj
{
public:
  LogObjectObj(TSLogObjectEle *ele);
  LogObjectObj(TokenList *tokens);
  ~LogObjectObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual TSCfgEle *getCfgEleCopy();
  virtual TSCfgEle *
  getCfgEle()
  {
    return (TSCfgEle *)m_ele;
  }
  virtual TSRuleTypeT
  getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  TSLogObjectEle *m_ele;
};

/* parent.config       *******************************************/
class ParentProxyObj : public CfgEleObj
{
public:
  ParentProxyObj(TSParentProxyEle *ele);
  ParentProxyObj(TokenList *tokens);
  ~ParentProxyObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual TSCfgEle *getCfgEleCopy();
  virtual TSCfgEle *
  getCfgEle()
  {
    return (TSCfgEle *)m_ele;
  }
  virtual TSRuleTypeT
  getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  TSParentProxyEle *m_ele;
};

/* volume.config    *******************************************/
class VolumeObj : public CfgEleObj
{
public:
  VolumeObj(TSVolumeEle *ele);
  VolumeObj(TokenList *tokens);
  ~VolumeObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual TSCfgEle *getCfgEleCopy();
  virtual TSCfgEle *
  getCfgEle()
  {
    return (TSCfgEle *)m_ele;
  }
  virtual TSRuleTypeT
  getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  TSVolumeEle *m_ele;
};

/* plugin.config    *******************************************/
class PluginObj : public CfgEleObj
{
public:
  PluginObj(TSPluginEle *ele);
  PluginObj(TokenList *tokens);
  ~PluginObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual TSCfgEle *getCfgEleCopy();
  virtual TSCfgEle *
  getCfgEle()
  {
    return (TSCfgEle *)m_ele;
  }
  virtual TSRuleTypeT
  getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  TSPluginEle *m_ele;
};

/* remap.config        *******************************************/
class RemapObj : public CfgEleObj
{
public:
  RemapObj(TSRemapEle *ele);
  RemapObj(TokenList *tokens);
  ~RemapObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual TSCfgEle *getCfgEleCopy();
  virtual TSCfgEle *
  getCfgEle()
  {
    return (TSCfgEle *)m_ele;
  }
  virtual TSRuleTypeT
  getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  TSRemapEle *m_ele;
};

/* socks.config        *******************************************/
class SocksObj : public CfgEleObj
{
public:
  SocksObj(TSSocksEle *ele);
  SocksObj(TokenList *tokens);
  ~SocksObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual TSCfgEle *getCfgEleCopy();
  virtual TSCfgEle *
  getCfgEle()
  {
    return (TSCfgEle *)m_ele;
  }
  virtual TSRuleTypeT
  getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  TSSocksEle *m_ele;
};

/* splitdns.config     *******************************************/
class SplitDnsObj : public CfgEleObj
{
public:
  SplitDnsObj(TSSplitDnsEle *ele);
  SplitDnsObj(TokenList *tokens);
  ~SplitDnsObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual TSCfgEle *getCfgEleCopy();
  virtual TSCfgEle *
  getCfgEle()
  {
    return (TSCfgEle *)m_ele;
  }
  virtual TSRuleTypeT
  getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  TSSplitDnsEle *m_ele;
};

/* storage.config      *******************************************/
class StorageObj : public CfgEleObj
{
public:
  StorageObj(TSStorageEle *ele);
  StorageObj(TokenList *tokens);
  ~StorageObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual TSCfgEle *getCfgEleCopy();
  virtual TSCfgEle *
  getCfgEle()
  {
    return (TSCfgEle *)m_ele;
  }
  virtual TSRuleTypeT
  getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  TSStorageEle *m_ele;
};

/* vaddrs.config       *******************************************/
class VirtIpAddrObj : public CfgEleObj
{
public:
  VirtIpAddrObj(TSVirtIpAddrEle *ele);
  VirtIpAddrObj(TokenList *tokens);
  ~VirtIpAddrObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual TSCfgEle *getCfgEleCopy();
  virtual TSCfgEle *
  getCfgEle()
  {
    return (TSCfgEle *)m_ele;
  }
  virtual TSRuleTypeT
  getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  TSVirtIpAddrEle *m_ele;
};

/*****************************************************************
 * CfgContext
 *****************************************************************/
class CfgContext
{
public:
  CfgContext(TSFileNameT filename);
  ~CfgContext();

  TSFileNameT
  getFilename()
  {
    return m_file;
  }
  int
  getVersion()
  {
    return m_ver;
  }
  void
  setVersion(int ver)
  {
    m_ver = ver;
  }

  CfgEleObj *
  first()
  {
    return m_eles.head;
  }
  CfgEleObj *
  next(CfgEleObj *here)
  {
    return (here->link).next;
  }

  TSMgmtError addEle(CfgEleObj *ele); /* enqueue EleObj at end of Queue */
  TSMgmtError removeEle(CfgEleObj *ele);
  TSMgmtError insertEle(CfgEleObj *ele, CfgEleObj *after_ele);
  TSMgmtError pushEle(CfgEleObj *ele);

private:
  TSFileNameT m_file;
  int m_ver; /* version of the file read */
  Queue<CfgEleObj> m_eles;
};

#endif
