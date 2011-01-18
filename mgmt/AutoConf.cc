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

#include "ink_config.h"
#include "ink_platform.h"
#include "ink_port.h"
#include "ink_time.h"

#include "Main.h"
#include "AutoConf.h"
#include "TextBuffer.h"
#include "Tokenizer.h"
#include "WebMgmtUtils.h"
#include "ink_assert.h"
#include "ExpandingArray.h"
#include "WebOverview.h"

/****************************************************************************
 *
 *  AutoConf.cc - code to generate and delete client autoconf files
 *
 *
 ****************************************************************************/

#define FILE_MODE S_IRWXU

static const char fileHead[] = "function FindProxyForURL(url, host) {\n\n";


static const char checkProtocol[] =
  "\t// Make sure this a protcol we proxy\n\tif(!((url.substring(0,5) == \"http:\") || \n\t\t(url.substring(0,6) == \"https:\"))) {\n\t\t return \"DIRECT\";\n\t}\n\n";

/*
static const char checkProtocol[] = "\t// Make sure this a protcol we proxy\n\tif(!((url.substring(0,5) == \"http:\") || \n\t\t(url.substring(0,6) == \"https:\"))) {\n\t\t return \"DIRECT\";\n\t}\n\n";
*/

static const char checkNQ[] = "\tif(isPlainHostName(host)) {\n\t\t return \"DIRECT\";\n\t}\n\n";
static const char checkDomain[] = "dnsDomainIs(host, \"";
static const char checkHost[] = "localHostOrDomainIs(host, \"";
static const char directResponse[] = "\t\treturn \"DIRECT\";\n\t}\n\n";
static const char returnStr[] = "\n\treturn ";
static const char proxyStr[] = "\"PROXY ";
static const char directStr[] = "\"DIRECT\";";

AutoConf *autoConfObj;

const char *pacStrings[] = {
  "Request Succeeded\n",
  "No Client Auto Configuration Directory",
  "Create of Client Auto Configuration File Failed\n",
  "Invalid Submission\n",
  "File Already Exists\n",
  "Remove Failed\n",
  "Missing File Name\n"
};

AutoConf::AutoConf()
{
}

AutoConf::~AutoConf()
{
}

void
AutoConf::displayAutoConfPage(textBuffer * output)
{

  ExpandingArray acList(10, true);
  const char docStart[] =
    "<html>\n<head>\n<title> Browser Auto-Configuration </title>\n</head>\n<body bgcolor=\"#FFFFFF\">\n<h1> Configure: Browser Auto-Configuration </h1>\n";
  const char docEnd[] =
    "<a href=\"/main.ink?t=c_serv\" target=_top> <img src=\"/images/back.gif\" border=\"0\"> Configure: Server Basics </a>\n</body>\n</html>\n";
  const char active1[] = "<p> An Auto-Configuration file exists.\n  It was last modified at ";
  const char active2[] =
    "</p>\n <p><form method=GET action=\"/configure/autoconf_add.html\"><input type=Submit value=\"Replace the current file\" onClick=\"newWindow('proxy_pac_view')\"></form></p>\n<p><form method=POST action=autoconf_action.html>\n<input type=hidden name=action value=delete>\n<input type=submit value=\"Delete The current file\"></form></p>\n<p><form method=POST action=\"autoconf_action.html\"><input type=Submit value=\"View the current file\">\n<input type=hidden name=action value=view></form></p>\n <SCRIPT LANGUAGE=\"JavaScript\">function newWindow(winName) { \n window.open(\"/configure/autoconf_proxy_pac.html\", winName, \"width=680,height=420\"); }</SCRIPT> \n";
  const char noFile[] =
    "<p> There is no autoconfiguration file.  <a href=\"/configure/autoconf_add.html\"> Create One </a></p>";

  struct stat fileInfo;
  bool pacFile = false;
  Rollback *pacRoll;
  char dateBuf[64];

  // Check to see if we have a client autoconfig file already
  //
  //   If the file is zero length, that means that it is not active
  //
  if (configFiles->getRollbackObj("proxy.pac", &pacRoll)) {
    if (pacRoll->statVersion(ACTIVE_VERSION, &fileInfo) == 0) {
      if (fileInfo.st_size > 0) {
        pacFile = true;
      }
    }
  }

  output->copyFrom(docStart, strlen(docStart));

  if (pacFile == true) {
    output->copyFrom(active1, strlen(active1));
    char *result = ink_ctime_r(&fileInfo.st_mtime, dateBuf);
    if (result != NULL) {
      output->copyFrom(dateBuf, strlen(dateBuf));
    } else {
      output->copyFrom("???", 3);
    }

    output->copyFrom(active2, strlen(active2));
  } else {
    output->copyFrom(noFile, strlen(noFile));
  }


  output->copyFrom(docEnd, strlen(docEnd));
}

void
AutoConf::byPass(textBuffer & newFile, Tokenizer & tok, const char *funcStr)
{
  int num = tok.getNumber();

  newFile.copyFrom("\tif(", 4);
  for (int i = 0; i < num; i++) {
    newFile.copyFrom(funcStr, strlen(funcStr));
    newFile.copyFrom(tok[i], strlen(tok[i]));
    if (i + 1 != num) {
      newFile.copyFrom("\") ||\n\t   ", 10);
    } else {
      newFile.copyFrom("\")) {\n", 6);
    }
  }
  newFile.copyFrom(directResponse, strlen(directResponse));
}

void
AutoConf::addProxy(textBuffer & output, char *hostname, char *port, bool first, bool final)
{

  if (first == false) {
    // not first entry and more entries, JavaScript contcatenates
    // strings with "+" operator
    output.copyFrom(" + \n\t\t", 6);
  }

  output.copyFrom(proxyStr, strlen(proxyStr));
  output.copyFrom(hostname, strlen(hostname));
  output.copyFrom(":", 1);
  output.copyFrom(port, strlen(port));

  if (final == true) {          // need to put ';' outside quote
    output.copyFrom("\"; ", 3);
  } else {                      // not final entry, so put ';' inside quote
    output.copyFrom(";\" ", 3);
  }
}

void
AutoConf::processAction(char *submission, textBuffer * output)
{
  InkHashTable *vars = processFormSubmission(submission);
  const char *action = "Unknown";
  PACresult r = PAC_OK;
  bool genReply = true;

  // Look up the necessary information from the form submission
  if (!ink_hash_table_lookup(vars, "action", (void **) &action) || action == NULL) {
    mgmt_log(stderr, "[AutoConf::processAction] Invalid Submission\n");
    action = "Unknown";
    r = PAC_INVALID_SUBMISSION;
  }

  if (r == PAC_OK) {
    if (strcasecmp(action, "create") == 0) {
      r = handleCreate(vars);
    } else if (strcasecmp(action, "delete") == 0) {
      r = handleRemove();
    } else if (strcasecmp(action, "view") == 0) {
      handleView(output, 0);
      genReply = false;
    } else if (strcasecmp(action, "abort") == 0) {
      // r = PAC_OK; genReply = true;
      // display the autoconf.html page
    } else {
      r = PAC_INVALID_SUBMISSION;
      mgmt_log(stderr, "[AutoConf::processAction] Invalid Submission\n");
    }
  }

  if (genReply == true) {
    if (r == PAC_OK) {
      this->displayAutoConfPage(output);
    } else {
      pacErrorResponse(action, r, output);
    }
  }

  ink_hash_table_destroy_and_xfree_values(vars);
}

// void AutoConf::pacErrorResponse(char* action, PACresult error, textBuffer* output)
//
void
AutoConf::pacErrorResponse(const char *action, PACresult error, textBuffer * output)
{
  const char a[] =
    "<html>\n<head>\n<title> Client AutoConfig Error </title>\n</head>\n<body bgcolor=\"#FFFFFF\">\n<h1> Client AutoConfig Error </h1>\n<p>\nClient AutoCnfig File ";
  const char b[] = " failed: ";
  const char c[] = "\n</p>\n<a href=\"/configure/autoconf.html\"> Continue</a>\n</body>\n</html>";

  output->copyFrom(a, strlen(a));
  output->copyFrom(action, strlen(action));
  output->copyFrom(b, strlen(b));
  output->copyFrom(pacStrings[error], strlen(pacStrings[error]));
  output->copyFrom(c, strlen(c));
}

// Added extra argument 'flag' to distinguish between displaying
// the 'proxy.pac' info in a the frame or a seperate window.  - GV
void
AutoConf::handleView(textBuffer * output, int flag      // 0-> displaying in framset, 1-> displaying in seperate window
  )
{
  const char a[] =
    "<html>\n<title> Configure: Current Auto Configuration File </title>\n</head>\n<body bgcolor=\"#FFFFFF\">\n<h1> Current Auto Configuration File </h1>\n<pre>\n";
  const char a1[] =
    "<html>\n<title> Configure: Auto Configuration File </title>\n</head>\n<body bgcolor=\"#FFFFFF\">\n<h1> Auto Configuration File</h1>\n <p> <b><em><font size=-1> Last modified: ";
  const char b[] =
    "\n</pre>\n<a href=\"/configure/autoconf.html\"> <img src=\"/images/back.gif\" border=\"0\"> Configure: Client Auto-Configuration </a>\n</body>\n</html>\n";
  const char noBinding[] = "Internal Error Occured: No Binding to File";
  const char readFailed[] = "Unable to retrieve file: ";
  const char active1[] = "</font></em></b> <p>\n<pre>\n";
  textBuffer *pac = NULL;
  RollBackCodes r;
  Rollback *pacRoll = NULL;
  // following used to get modified times of 'proxy.pac'
  struct stat fileInfo;
  char dateBuf[64];

  // prepare header of HTML response depending upon whether the data is
  // being display in the frame or a seperate window
  if (1 == flag) {
    // seperate window
    output->copyFrom(a1, strlen(a1));

    if (configFiles->getRollbackObj("proxy.pac", &pacRoll) == false) {
      output->copyFrom(active1, strlen(active1));
      output->copyFrom(noBinding, strlen(noBinding));
    } else {
      r = pacRoll->getVersion(ACTIVE_VERSION, &pac);

      // get modified time of 'proxy.pac' file
      if (pacRoll->statVersion(ACTIVE_VERSION, &fileInfo) == 0) {
        if (fileInfo.st_size > 0) {
          // TODO: Huh, no-op?
        }
      }

      char *result = ink_ctime_r(&fileInfo.st_mtime, dateBuf);
      if (result != NULL) {
        output->copyFrom(dateBuf, strlen(dateBuf));
      } else {
        output->copyFrom("???", 3);
      }
      output->copyFrom(active1, strlen(active1));

      if (r == OK_ROLLBACK) {
        output->copyFrom(pac->bufPtr(), pac->spaceUsed());
      } else {
        output->copyFrom(readFailed, strlen(readFailed));
      }
    }
  } else {
    // in frame
    output->copyFrom(a, strlen(a));

    if (configFiles->getRollbackObj("proxy.pac", &pacRoll) == false) {
      output->copyFrom(noBinding, strlen(noBinding));
    } else {
      r = pacRoll->getVersion(ACTIVE_VERSION, &pac);

      if (r == OK_ROLLBACK) {
        output->copyFrom(pac->bufPtr(), pac->spaceUsed());
      } else {
        output->copyFrom(readFailed, strlen(readFailed));
      }
    }
  }

  if (0 == flag)                // only display back button when displaying in frame
    output->copyFrom(b, strlen(b));

  delete pac;
}


PACresult
AutoConf::handleCreate(InkHashTable * params)
{
  textBuffer newFile(2048);
  Rollback *pacRoll;

  if (BuildFile(params, newFile) == false) {
    return PAC_INVALID_SUBMISSION;
  }

  if (configFiles->getRollbackObj("proxy.pac", &pacRoll) == false) {
    return PAC_CREATE_FAILED;
  }

  if (pacRoll->forceUpdate(&newFile) != OK_ROLLBACK) {
    return PAC_CREATE_FAILED;
  }

  return PAC_OK;
}

PACresult
AutoConf::handleRemove()
{
  Rollback *pacRoll;
  textBuffer empty(16);

  if (configFiles->getRollbackObj("proxy.pac", &pacRoll) == false) {
    return PAC_REMOVE_FAILED;
  }

  if (pacRoll->forceUpdate(&empty) != OK_ROLLBACK) {
    return PAC_REMOVE_FAILED;
  }

  return PAC_OK;
}

// bool AutoConf::BuildFile(InkHashTable* parameters, textBuffer& newFile)
//
//  Constructs a client autoconfig file into newFile from information
//    contained in the parameters hashTable.
//
//  Returns true if the construction was success and
//     false otherwise
//
bool
AutoConf::BuildFile(InkHashTable * parameters, textBuffer & newFile)
{

  char *val;
  int num = 0;
  ink_assert(parameters != NULL);
  ExpandingArray clusterHosts(25, true);
  char portBuf[20];
  char rrNameBuf[64];
  bool rrEnabled;
  char *secondProxy = NULL;
  char *secondPort = NULL;
  bool clusterFO = false;
  bool secondFO = false;
  bool directFO = false;
  int remainingFO = 0;
  Tokenizer tok(" \t");
  char *nodeFQHN;
  bool FQHNfound;

  newFile.copyFrom(fileHead, strlen(fileHead));
  newFile.copyFrom(checkProtocol, strlen(checkProtocol));

  // Handle hosts without Fully Qualified Domanin Names
  if (ink_hash_table_lookup(parameters, "nq_hosts", (void **) &val)) {
    newFile.copyFrom(checkNQ, strlen(checkNQ));
  }
  // Handle hosts to bypass because of domain name
  if (ink_hash_table_lookup(parameters, "domain_bypass", (void **) &val)) {
    if (val != NULL && *val != '\0') {
      num = tok.Initialize(val, SHARE_TOKS);

      this->byPass(newFile, tok, checkDomain);
    }
  }
  // Handle hosts to bypass because of host name
  if (ink_hash_table_lookup(parameters, "host_bypass", (void **) &val)) {
    if (val != NULL && *val != '\0') {
      num = tok.Initialize(val, SHARE_TOKS);
      this->byPass(newFile, tok, checkHost);
    }
  }
  // Generate the default case proxy string
  //
  //  If virtual IP is enabled, use the round robin name, otherwise
  //    assume there is no round robin and just use this machine's
  //    hostname
  //
  if (lmgmt->virt_map->enabled == 0) {
    rrEnabled = false;
    if (varStrFromName("proxy.node.hostname_FQ", rrNameBuf, 64) == false) {
      return false;
    }
  } else {
    // Virtual IP
    rrEnabled = true;
    if (varStrFromName("proxy.config.proxy_name", rrNameBuf, 64) == false) {
      return false;
    }
  }

  if (varStrFromName("proxy.config.http.server_port", portBuf, 20) == false) {
    return false;
  }
  // check for 'Internal Cluster Failover' option
  if (ink_hash_table_lookup(parameters, "cluster_fo", (void **) &val)) {
    // Get number of cluster nodes and enable option only if more than one node
    num = overviewGenerator->getClusterHosts(&clusterHosts);
    if (num > 1) {
      clusterFO = true;
      remainingFO++;
    }
  }
  // check for 'Failover to Secondary Proxy' option
  if (ink_hash_table_lookup(parameters, "second_fo", (void **) &val)) {
    secondFO = true;
    remainingFO++;
  }
  // check for 'Go to Direct as Last Resort' option
  if (ink_hash_table_lookup(parameters, "direct_fo", (void **) &val)) {
    directFO = true;
    remainingFO++;
  }
  // Always add Round-Robin Name
  newFile.copyFrom(returnStr, strlen(returnStr));
  if (remainingFO)
    this->addProxy(newFile, rrNameBuf, portBuf, true, false);
  else
    this->addProxy(newFile, rrNameBuf, portBuf, true, true);

  if (clusterFO == true) {
    // 'Internal Cluster Failover' option
    remainingFO--;

    int i;

    // If the first response is a round robin name,
    //   include this machine in the cluster fail over
    //   list.  If the first response is the hostname of
    //   this machine, skip over this machine in cluster
    //   fail over generation as not to repeat the name
    //   this proxy
    if (rrEnabled == true) {
      i = 0;
    } else {
      i = 1;
    }

    for (; i < num; i++) {
      //coverity[alloc_fn]
      //coverity[var_assign]
      nodeFQHN = overviewGenerator->readString((char *) clusterHosts[i], "proxy.node.hostname_FQ", &FQHNfound);
      // We should always be able to find a FQHN but if
      //   we don't just muddle through with the unqualified
      //   and hope for the best
      if (FQHNfound == false) {
        //coverity[overwrite_var]
        nodeFQHN = xstrdup((char *) clusterHosts[i]);
      }

      if (remainingFO)
        this->addProxy(newFile, nodeFQHN, portBuf, false, false);
      else
        this->addProxy(newFile, nodeFQHN, portBuf, false, true);
      xfree(nodeFQHN);
    }
  }

  if (secondFO == true) {
    // 'Failover to Secondary Proxy' option
    remainingFO--;
    if (ink_hash_table_lookup(parameters, "second_proxy", (void **) &secondProxy) &&
        ink_hash_table_lookup(parameters, "second_port", (void **) &secondPort)) {
      if (secondProxy && secondPort && *secondProxy != '\0' && *secondPort != '\0') {
        if (remainingFO)
          this->addProxy(newFile, secondProxy, secondPort, false, false);
        else
          this->addProxy(newFile, secondProxy, secondPort, false, true);
      }
    }
  }

  if (directFO == true) {
    // 'Go to Direct as Last Resort' option
    remainingFO--;
    newFile.copyFrom(" + \n\t\t", 6);
    newFile.copyFrom(directStr, strlen(directStr));
  }

  newFile.copyFrom("\n}\n", 3);

  return true;
}

void
please_give_me_debug_info()
{
}
