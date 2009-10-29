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

#include "INKMgmtAPI.h"
#include "CfgContextDefs.h"
#include "GenericParser.h"      /* use for TokenList */

#include "List.h"


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

  virtual ~ CfgEleObj()
  {
  }                             // virtual destructor

  virtual char *formatEleToRule() = 0;
  virtual bool isValid() = 0;

  /* these are implemented as inline functons by subclasses */
  virtual INKCfgEle *getCfgEle() = 0;   /* returns actual ele */
  virtual INKCfgEle *getCfgEleCopy() = 0;       /* returns copy of ele */
  virtual INKRuleTypeT getRuleType() = 0;

  Link<CfgEleObj> link;

protected:
  bool m_valid;                 /* stores if Ele has valid fields; by default true */

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
class CommentObj:public CfgEleObj
{
public:
  CommentObj(char *comment);
   ~CommentObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual INKCfgEle *getCfgEleCopy();
  virtual INKCfgEle *getCfgEle()
  {
    return (INKCfgEle *) m_ele;
  }
  virtual INKRuleTypeT getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  INKCommentEle * m_ele;

};

/* admin_access.config    ****************************************/
class AdminAccessObj:public CfgEleObj
{
public:
  AdminAccessObj(INKAdminAccessEle * ele);
  AdminAccessObj(TokenList * tokens);   //creates the ele
  ~AdminAccessObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual INKCfgEle *getCfgEleCopy();
  virtual INKCfgEle *getCfgEle()
  {
    return (INKCfgEle *) m_ele;
  }
  virtual INKRuleTypeT getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  INKAdminAccessEle * m_ele;
};

/* cache.config ***************************************************/
class CacheObj:public CfgEleObj
{
public:
  CacheObj(INKCacheEle * ele);
  CacheObj(TokenList * tokens); //creates the ele
  ~CacheObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual INKCfgEle *getCfgEleCopy();
  virtual INKCfgEle *getCfgEle()
  {
    return (INKCfgEle *) m_ele;
  }
  virtual INKRuleTypeT getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  INKCacheEle * m_ele;
};

/* congestion.config ***************************************************/
class CongestionObj:public CfgEleObj
{
public:
  CongestionObj(INKCongestionEle * ele);
  CongestionObj(TokenList * tokens);    //creates the ele
  ~CongestionObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual INKCfgEle *getCfgEleCopy();
  virtual INKCfgEle *getCfgEle()
  {
    return (INKCfgEle *) m_ele;
  }
  virtual INKRuleTypeT getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  INKCongestionEle * m_ele;
};

/* filter.config    *********************************************/
class FilterObj:public CfgEleObj
{
public:
  FilterObj(INKFilterEle * ele);
  FilterObj(TokenList * tokens);        //creates the ele
  ~FilterObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual INKCfgEle *getCfgEleCopy();
  virtual INKCfgEle *getCfgEle()
  {
    return (INKCfgEle *) m_ele;
  }
  virtual INKRuleTypeT getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  INKFilterEle * m_ele;
};

/* ftp_remap.config *********************************************/
class FtpRemapObj:public CfgEleObj
{
public:
  FtpRemapObj(INKFtpRemapEle * ele);
  FtpRemapObj(TokenList * tokens);      //creates the ele
  ~FtpRemapObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual INKCfgEle *getCfgEleCopy();
  virtual INKCfgEle *getCfgEle()
  {
    return (INKCfgEle *) m_ele;
  }
  virtual INKRuleTypeT getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  INKFtpRemapEle * m_ele;
};

/* hosting.config ************************************************/
class HostingObj:public CfgEleObj
{
public:
  HostingObj(INKHostingEle * ele);
  HostingObj(TokenList * tokens);       //creates the ele
  ~HostingObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual INKCfgEle *getCfgEleCopy();
  virtual INKCfgEle *getCfgEle()
  {
    return (INKCfgEle *) m_ele;
  }
  virtual INKRuleTypeT getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  INKHostingEle * m_ele;
};

/* icp.config ************************************************/
class IcpObj:public CfgEleObj
{
public:
  IcpObj(INKIcpEle * ele);
  IcpObj(TokenList * tokens);   //creates the ele
  ~IcpObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual INKCfgEle *getCfgEleCopy();
  virtual INKCfgEle *getCfgEle()
  {
    return (INKCfgEle *) m_ele;
  }
  virtual INKRuleTypeT getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  INKIcpEle * m_ele;
};

/* ip_allow.config    *********************************************/
class IpAllowObj:public CfgEleObj
{
public:
  IpAllowObj(INKIpAllowEle * ele);
  IpAllowObj(TokenList * tokens);
  ~IpAllowObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual INKCfgEle *getCfgEleCopy();
  virtual INKCfgEle *getCfgEle()
  {
    return (INKCfgEle *) m_ele;
  }
  virtual INKRuleTypeT getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  INKIpAllowEle * m_ele;
};

/* logs_xml.config    ********************************************/
class LogFilterObj:public CfgEleObj
{
public:
  LogFilterObj(INKLogFilterEle * ele);
  LogFilterObj(TokenList * tokens);
  ~LogFilterObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual INKCfgEle *getCfgEleCopy();
  virtual INKCfgEle *getCfgEle()
  {
    return (INKCfgEle *) m_ele;
  }
  virtual INKRuleTypeT getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  INKLogFilterEle * m_ele;
};

class LogFormatObj:public CfgEleObj
{
public:
  LogFormatObj(INKLogFilterEle * ele);
  LogFormatObj(TokenList * tokens);
  ~LogFormatObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual INKCfgEle *getCfgEleCopy();
  virtual INKCfgEle *getCfgEle()
  {
    return (INKCfgEle *) m_ele;
  }
  virtual INKRuleTypeT getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  INKLogFormatEle * m_ele;
};

/* logs.config ***************************************************/
class LogObjectObj:public CfgEleObj
{
public:
  LogObjectObj(INKLogObjectEle * ele);
  LogObjectObj(TokenList * tokens);
  ~LogObjectObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual INKCfgEle *getCfgEleCopy();
  virtual INKCfgEle *getCfgEle()
  {
    return (INKCfgEle *) m_ele;
  }
  virtual INKRuleTypeT getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  INKLogObjectEle * m_ele;
};

#if 0
/* log_hosts.config    *******************************************/
class LogHostsObj:public CfgEleObj
{
public:
  LogHostsObj(INKLogHostsEle * ele);
  LogHostsObj(TokenList * tokens);
  ~LogHostsObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual INKCfgEle *getCfgEle()
  {
    return (INKCfgEle *) m_ele;
  }
  virtual INKRuleTypeT getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  INKLogHostsEle * m_ele;
};
#endif

/* mgmt_allow.config   *******************************************/
class MgmtAllowObj:public CfgEleObj
{
public:
  MgmtAllowObj(INKMgmtAllowEle * ele);
  MgmtAllowObj(TokenList * tokens);
  ~MgmtAllowObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual INKCfgEle *getCfgEleCopy();
  virtual INKCfgEle *getCfgEle()
  {
    return (INKCfgEle *) m_ele;
  }
  virtual INKRuleTypeT getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  INKMgmtAllowEle * m_ele;
};

/* nntp_access.config  *******************************************/
class NntpAccessObj:public CfgEleObj
{
public:
  NntpAccessObj(INKNntpAccessEle * ele);
  NntpAccessObj(TokenList * tokens);
  ~NntpAccessObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual INKCfgEle *getCfgEleCopy();
  virtual INKCfgEle *getCfgEle()
  {
    return (INKCfgEle *) m_ele;
  }
  virtual INKRuleTypeT getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  INKNntpAccessEle * m_ele;
};

/* nntp_servers.config *******************************************/
class NntpSrvrObj:public CfgEleObj
{
public:
  NntpSrvrObj(INKNntpSrvrEle * ele);
  NntpSrvrObj(TokenList * tokens);
  ~NntpSrvrObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual INKCfgEle *getCfgEleCopy();
  virtual INKCfgEle *getCfgEle()
  {
    return (INKCfgEle *) m_ele;
  }
  virtual INKRuleTypeT getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  INKNntpSrvrEle * m_ele;
};

/* parent.config       *******************************************/
class ParentProxyObj:public CfgEleObj
{
public:
  ParentProxyObj(INKParentProxyEle * ele);
  ParentProxyObj(TokenList * tokens);
  ~ParentProxyObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual INKCfgEle *getCfgEleCopy();
  virtual INKCfgEle *getCfgEle()
  {
    return (INKCfgEle *) m_ele;
  }
  virtual INKRuleTypeT getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  INKParentProxyEle * m_ele;
};

/* partition.config    *******************************************/
class PartitionObj:public CfgEleObj
{
public:
  PartitionObj(INKPartitionEle * ele);
  PartitionObj(TokenList * tokens);
  ~PartitionObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual INKCfgEle *getCfgEleCopy();
  virtual INKCfgEle *getCfgEle()
  {
    return (INKCfgEle *) m_ele;
  }
  virtual INKRuleTypeT getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  INKPartitionEle * m_ele;
};

/* plugin.config    *******************************************/
class PluginObj:public CfgEleObj
{
public:
  PluginObj(INKPluginEle * ele);
  PluginObj(TokenList * tokens);
  ~PluginObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual INKCfgEle *getCfgEleCopy();
  virtual INKCfgEle *getCfgEle()
  {
    return (INKCfgEle *) m_ele;
  }
  virtual INKRuleTypeT getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  INKPluginEle * m_ele;
};

/* remap.config        *******************************************/
class RemapObj:public CfgEleObj
{
public:
  RemapObj(INKRemapEle * ele);
  RemapObj(TokenList * tokens);
  ~RemapObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual INKCfgEle *getCfgEleCopy();
  virtual INKCfgEle *getCfgEle()
  {
    return (INKCfgEle *) m_ele;
  }
  virtual INKRuleTypeT getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  INKRemapEle * m_ele;
};

/* socks.config        *******************************************/
class SocksObj:public CfgEleObj
{
public:
  SocksObj(INKSocksEle * ele);
  SocksObj(TokenList * tokens);
  ~SocksObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual INKCfgEle *getCfgEleCopy();
  virtual INKCfgEle *getCfgEle()
  {
    return (INKCfgEle *) m_ele;
  }
  virtual INKRuleTypeT getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  INKSocksEle * m_ele;
};


/* splitdns.config     *******************************************/
class SplitDnsObj:public CfgEleObj
{
public:
  SplitDnsObj(INKSplitDnsEle * ele);
  SplitDnsObj(TokenList * tokens);
  ~SplitDnsObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual INKCfgEle *getCfgEleCopy();
  virtual INKCfgEle *getCfgEle()
  {
    return (INKCfgEle *) m_ele;
  }
  virtual INKRuleTypeT getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  INKSplitDnsEle * m_ele;
};

/* storage.config      *******************************************/
class StorageObj:public CfgEleObj
{
public:
  StorageObj(INKStorageEle * ele);
  StorageObj(TokenList * tokens);
  ~StorageObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual INKCfgEle *getCfgEleCopy();
  virtual INKCfgEle *getCfgEle()
  {
    return (INKCfgEle *) m_ele;
  }
  virtual INKRuleTypeT getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  INKStorageEle * m_ele;
};


/* update.config       *******************************************/
class UpdateObj:public CfgEleObj
{
public:
  UpdateObj(INKUpdateEle * ele);
  UpdateObj(TokenList * tokens);
  ~UpdateObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual INKCfgEle *getCfgEleCopy();
  virtual INKCfgEle *getCfgEle()
  {
    return (INKCfgEle *) m_ele;
  }
  virtual INKRuleTypeT getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  INKUpdateEle * m_ele;
};

/* vaddrs.config       *******************************************/
class VirtIpAddrObj:public CfgEleObj
{
public:
  VirtIpAddrObj(INKVirtIpAddrEle * ele);
  VirtIpAddrObj(TokenList * tokens);
  ~VirtIpAddrObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual INKCfgEle *getCfgEleCopy();
  virtual INKCfgEle *getCfgEle()
  {
    return (INKCfgEle *) m_ele;
  }
  virtual INKRuleTypeT getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  INKVirtIpAddrEle * m_ele;
};

#ifdef OEM
/* rmserver.cfg      *******************************************/
class RmServerObj:public CfgEleObj
{
public:
  RmServerObj(INKRmServerEle * ele);
  RmServerObj(TokenList * tokens);
  ~RmServerObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual INKCfgEle *getCfgEleCopy();
  virtual INKCfgEle *getCfgEle()
  {
    return (INKCfgEle *) m_ele;
  }
  virtual INKRuleTypeT getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  INKRmServerEle * m_ele;
};

/* vscan.config      *******************************************/
class VscanObj:public CfgEleObj
{
public:
  VscanObj(INKVscanEle * ele);
  VscanObj(TokenList * tokens);
  ~VscanObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual INKCfgEle *getCfgEleCopy();
  virtual INKCfgEle *getCfgEle()
  {
    return (INKCfgEle *) m_ele;
  }
  virtual INKRuleTypeT getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  INKVscanEle * m_ele;
};

/* trusted-host.config      *******************************************/
class VsTrustedHostObj:public CfgEleObj
{
public:
  VsTrustedHostObj(INKVsTrustedHostEle * ele);
  VsTrustedHostObj(TokenList * tokens);
  ~VsTrustedHostObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual INKCfgEle *getCfgEleCopy();
  virtual INKCfgEle *getCfgEle()
  {
    return (INKCfgEle *) m_ele;
  }
  virtual INKRuleTypeT getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  INKVsTrustedHostEle * m_ele;
};

/* extensions.config      *******************************************/
class VsExtensionObj:public CfgEleObj
{
public:
  VsExtensionObj(INKVsExtensionEle * ele);
  VsExtensionObj(TokenList * tokens);
  ~VsExtensionObj();

  virtual char *formatEleToRule();
  virtual bool isValid();
  virtual INKCfgEle *getCfgEleCopy();
  virtual INKCfgEle *getCfgEle()
  {
    return (INKCfgEle *) m_ele;
  }
  virtual INKRuleTypeT getRuleType()
  {
    return m_ele->cfg_ele.type;
  }

private:
  INKVsExtensionEle * m_ele;
};

#endif

/*****************************************************************
 * CfgContext
 *****************************************************************/
class CfgContext
{
public:
  CfgContext(INKFileNameT filename);
  ~CfgContext();

  INKFileNameT getFilename()
  {
    return m_file;
  }
  int getVersion()
  {
    return m_ver;
  }
  void setVersion(int ver)
  {
    m_ver = ver;
  }

  CfgEleObj *first()
  {
    return m_eles.head;
  }
  CfgEleObj *next(CfgEleObj * here)
  {
    return (here->link).next;
  }

  INKError addEle(CfgEleObj * ele);     /* enqueue EleObj at end of Queue */
  INKError removeEle(CfgEleObj * ele);
  INKError insertEle(CfgEleObj * ele, CfgEleObj * after_ele);
  INKError pushEle(CfgEleObj * ele);

private:
  INKFileNameT m_file;
  int m_ver;                    /* version of the file read */
  Queue<CfgEleObj> m_eles;
};



#endif
