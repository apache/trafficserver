
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
#include "Common.h"

#include <arpa/inet.h>
#include <stdlib.h> 
#include <stdio.h> 
#include <errno.h> 
#include <netdb.h>
#include <string.h>
#include <memory.h>

using namespace std;

/************************************************************************/
void stringExplode(string str, string separator, vector<string>* results) 
{
    size_t found;
    found = str.find_first_of(separator);
    while (found != std::string::npos) 
    {
        if (found > 0) 
        {
            results->push_back(str.substr(0, found));
        }
        str = str.substr(found + 1);
        found = str.find_first_of(separator);
    }
    if (str.length() > 0) 
    {
        results->push_back(str);
    }
}

/************************************************************************/
/*
 Parse /proc/{pid}/net/tcp
   sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode                                                     
   0: 00000000:036B 00000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 22272 1 ffff8801b7ecb700 299 0 0 2 -1                     
   1: 00000000:08AE 00000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 10621 1 ffff8801b7b9a080 299 0 0 2 -1                     
   2: 00000000:006F 00000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 22046 1 ffff8801b7b9b400 299 0 0 2 -1                     
 Parse /proc/{pid}/net/tcp6
 *   sl  local_address                         remote_address                        st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode
   0: 00000000000000000000000000000000:08AE 00000000000000000000000000000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 10619 1 ffff8801b266d780 299 0 0 2 -1
   1: 00000000000000000000000000000000:006F 00000000000000000000000000000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 22051 1 ffff8801b3a51880 299 0 0 2 -1
   2: 00000000000000000000000000000000:0016 00000000000000000000000000000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 10656 1 ffff8801b266d040 299 0 0 2 -1
   3: 00000000000000000000000000000000:8E77 00000000000000000000000000000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 22300 1 ffff8801b2f5c780 299 0 0 2 -1
 
 looking for local_address:PORT wher st = LISTEN (0x0A)
 *  ./include/net/tcp_states.h
 * enum {
    TCP_ESTABLISHED = 1,
    TCP_SYN_SENT,
    TCP_SYN_RECV,
    TCP_FIN_WAIT1,
    TCP_FIN_WAIT2,
    TCP_TIME_WAIT,
    TCP_CLOSE,
    TCP_CLOSE_WAIT,
    TCP_LAST_ACK,
    TCP_LISTEN,
    TCP_CLOSING,    

    TCP_MAX_STATES  
 */

/************************************************************************/

/* used internally                                                      */
bool
scanProcFileForPort(string sFilename, string sPid, unsigned int iPort)
{
  bool bMatch = false;
  TSFile file;
  int iLine = 0;
  bool bParseDone = false;

  // attempt to open file
  TSDebug(DEBUG_TAG_INIT, "Trying to open proc file @ %s to determine listening ports", sFilename.c_str());
  file = TSfopen(sFilename.c_str(), "r");
  if (file == NULL) {
    TSError("Failed to open proc tcp file of %s.  Error=%s", sFilename.c_str(), strerror(errno));
    return false;
  }

  TSDebug(DEBUG_TAG_INIT, "Successfully opened %s file", sFilename.c_str());

  char buffer[1024];
  memset(buffer, 0, sizeof (buffer));

  while (TSfgets(file, buffer, sizeof (buffer) - 1) != NULL && !bParseDone) {
    char *eol = strstr(buffer, "\r\n");
    if (!eol)
      eol = strchr(buffer, '\n');
    if (eol)
      *eol = 0; // remove ending LF or CRLF

    //    TSDebug(DEBUG_TAG_INIT, "Parsing line: %s", buffer);
    ++iLine;
    if (iLine == 1) { // ignore header line
      continue;
    }
    vector<string> vParts;
    stringExplode(string(buffer), string(" "), &vParts);
    // part 3 is status
    unsigned int iStatus = 0;
    sscanf(vParts[3].c_str(), "%x", &iStatus);
    if (iStatus == TCP_LISTEN) {
      vector<string> vLocalParts;
      stringExplode(vParts[1], string(":"), &vLocalParts); // part 1 is localaddr
      unsigned int iLPort = 0;
      sscanf(vLocalParts[1].c_str(), "%x", &iLPort);
      TSDebug(DEBUG_TAG_INIT, "Found listening port %d", iLPort);
      if (iLPort == iPort) {
        string sLabel = string("socket:[" + vParts[9] + "]");
        // find inode in   /proc/{pid}/fd/
        string sDir = "/proc/" + string(sPid) + "/fd";

        DIR *dp;
        struct dirent *dirp;
        if ((dp = opendir(sDir.c_str())) == NULL) {
          TSDebug(DEBUG_TAG_INIT, "Failed to open directory %s, %s", sDir.c_str(), strerror(errno));
          continue;
        }

        while ((dirp = readdir(dp)) != NULL && !bParseDone) {
          //TSDebug(DEBUG_TAG_INIT, "File in  %s = %s inode=%d", sDir.c_str(),dirp->d_name,dirp->d_ino);
          string sPath = sDir + "/" + dirp->d_name;
          char sBuf[256];
          ssize_t iCount = readlink(sPath.c_str(), sBuf, sizeof (sBuf));
          if (iCount > 0) {
            string sLink(sBuf, iCount);
            if (sLink.compare(sLabel) == 0) {
              TSDebug(DEBUG_TAG_INIT, "Found that port %d is opened for listening by pid %s", iLPort, sPid.c_str());
              bParseDone = true;
              bMatch = true;
            }
          }
        }
        closedir(dp);
      }
    }
    /*    
        for(int i=0;i<vParts.size();i++)
        {
          TSDebug(DEBUG_TAG_INIT, "part[%d]='%s'", i, vParts[i].c_str());
        }
     */
  }

  TSfclose(file);

  return bMatch;
}

/************************************************************************/
bool 
isPortSelf(unsigned int iPort)
{
  pid_t pid = getpid();
  char sPid[10];
  bool bMatch = false;

  string sFileName; 
  // open file @ /proc/{pid}/net/tcp

  
  sprintf(sPid,"%d",pid);
  
  // look for IPv4 listener first
  sFileName = "/proc/" + string(sPid) + "/net/tcp";
  bMatch = scanProcFileForPort(sFileName,sPid, iPort);
  if(!bMatch) { // did not find IPv4 listener, check for IPv6
    sFileName = "/proc/" + string(sPid) + "/net/tcp6";
    bMatch = scanProcFileForPort(sFileName,sPid, iPort);
  }
  return bMatch;
}

/************************************************************************/
struct hostent * 
getHostIp(string hName, struct hostent *h, char *buf, int buflen)
{
    int res, err;
    struct hostent *hp = NULL;

    res = gethostbyname_r(hName.c_str(), h, buf, buflen, &hp, &err);
    if ((res == 0) && (hp != NULL)) {
      return hp;
    } else {
      TSDebug(DEBUG_TAG_INIT, "gethostbyname_r failed for %s.  Error=%s", hName.c_str(), strerror(err));
    }
    return NULL;
}

          
/************************************************************************/
/*
 * *** WARNING *** You will need to run the carp plugin with traffic_manager for it to detect
 * itself and to forward directly to the origin.  It will not work by running traffic_server directly!
 */
bool
isSelf(string sName, int iPort, struct hostent *pSelf)
{
  bool bMatch = false;
  struct hostent other, *pOther;
  char buf[1024];
  pOther = getHostIp(sName,&other,buf, sizeof(buf));
  //uint32_t self_addr = htonl(*(uint32_t *)pSelf->h_addr_list);
  //TSDebug(DEBUG_TAG_INIT, "isSelf SELF h_addrtype=%d h_length=%d ip=%s",
  //    pSelf->h_addrtype, pSelf->h_length, inet_ntoa(*(struct in_addr*)&self_addr));
  if(pOther) {
    if( (pOther->h_addrtype != pSelf->h_addrtype) ||  // check basics are same
       (pOther->h_length != pSelf->h_length)) {
      return false;
    }
    
    for (int i = 0; pOther->h_addr_list[i] != NULL; i++) {  // loop through each in other
      for (int j = 0; pSelf->h_addr_list[j] != NULL; j++) { // loop through self
  //      uint32_t other_addr = htonl(*(uint32_t *)pOther->h_addr_list);
  //      TSDebug(DEBUG_TAG_INIT, "isSelf OTHER h_addrtype=%d h_length=%d ip=%s",
  //          pOther->h_addrtype, pOther->h_length, inet_ntoa(*(struct in_addr*)&other_addr));

        if(memcmp(pSelf->h_addr_list[j], pOther->h_addr_list[i], pOther->h_length) == 0) {
          // check for matching ports
          // for self, look at the ports we are listening on.
          bMatch = isPortSelf(iPort);
          TSDebug(DEBUG_TAG_INIT, "port matched %s", bMatch ? "true":"false");
        }
      }
    }
  }
  return bMatch;
}

/************************************************************************/
bool
addHeader(TSMBuffer& reqp, TSMLoc& hdr_loc, string header, string value)
{
  bool bReturn = false;
  if (value.size() <= 0) {
    TSDebug(DEBUG_TAG_HOOK, "\tWould set header %s to an empty value, skipping", header.c_str());
  } else {
    TSMLoc new_field;

    if (TS_SUCCESS == TSMimeHdrFieldCreateNamed(reqp, hdr_loc, header.data(), header.size(), &new_field)) {
      if (TS_SUCCESS == TSMimeHdrFieldValueStringInsert(reqp, hdr_loc, new_field, -1,  value.data(), value.size())) {
        if (TS_SUCCESS == TSMimeHdrFieldAppend(reqp, hdr_loc, new_field)) {
          TSDebug(DEBUG_TAG_HOOK, "\tAdded header %s: %s", header.c_str(), value.c_str());
          bReturn = true;
        }
      }
      TSHandleMLocRelease(reqp, hdr_loc, new_field);
    }
  }
  return bReturn;
}

/************************************************************************/
bool
getHeader(TSMBuffer& reqp, TSMLoc& hdr_loc, const std::string header, std::string& value)
{
  bool bReturn = false;
  TSMLoc fieldLoc = TSMimeHdrFieldFind(reqp, hdr_loc, header.data(), header.size());

  if (fieldLoc && (fieldLoc != NULL)) {
    const char *str;
    int strLen = 0;
    str = TSMimeHdrFieldValueStringGet(reqp, hdr_loc, fieldLoc, 0, &strLen);
    if (str && strLen > 0) {
      value.assign(str, strLen);
      bReturn = true;
    } else {
      bReturn = false;
    }
    TSHandleMLocRelease(reqp, hdr_loc, fieldLoc);
  }
  return bReturn;
}

/************************************************************************/
bool
removeHeader(TSMBuffer& reqp, TSMLoc& hdr_loc, string header)
{
  bool bReturn = false;
  TSMLoc fieldLoc = TSMimeHdrFieldFind(reqp, hdr_loc, header.data(), header.size());

  if (fieldLoc && (fieldLoc != NULL)) {
    if(TSMimeHdrFieldRemove(reqp, hdr_loc, fieldLoc) == TS_SUCCESS) {
      if(TSMimeHdrFieldDestroy(reqp, hdr_loc, fieldLoc) == TS_SUCCESS) {
        TSDebug(DEBUG_TAG_HOOK, "\tRemoved header %s", header.c_str());
        bReturn = true;
      }
    }
    TSHandleMLocRelease(reqp, hdr_loc, fieldLoc);
  }
  return bReturn;
}


bool
setHeader(TSMBuffer& reqp, TSMLoc& hdr_loc, const std::string header, const std::string value)
{
  bool bReturn = false;
  TSMLoc fieldLoc = TSMimeHdrFieldFind(reqp, hdr_loc, header.data(), header.size());

  if (fieldLoc && (fieldLoc != NULL)) {
    if (TSMimeHdrFieldValueStringSet(reqp, hdr_loc, fieldLoc, 0, value.data(), value.length()) == TS_SUCCESS) {
      TSDebug(DEBUG_TAG_HOOK, "\tSet header %s to %s", header.c_str(), value.c_str());
      bReturn = true;
    } else {
      bReturn = false;
    }
    TSHandleMLocRelease(reqp, hdr_loc, fieldLoc);
  }
  return bReturn;
}

/************************************************************************/
//Convert a struct sockaddr address to a string, IPv4 and IPv6:
bool
getStringFromSockaddr(const struct sockaddr *sa, string& s)
{
  char str[INET6_ADDRSTRLEN];
  memset(str,sizeof(str),0);
  switch (sa->sa_family) {
  case AF_INET:
    inet_ntop(AF_INET, &(((struct sockaddr_in *) sa)->sin_addr), str, sizeof(str));
    break;

  case AF_INET6:
    inet_ntop(AF_INET6, &(((struct sockaddr_in6 *) sa)->sin6_addr), str, sizeof(str));
    break;

  default:
    s.assign("Unknown");
    return false;
  }
  s.assign(str);
  sprintf(str,":%d",ntohs(((struct sockaddr_in *) sa)->sin_port));
  s += str;
  return true;
}
