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

 /***************************************/
/****************************************************************************
 *
 *  WebHttp.cc - code to process requests, and create responses
 *
 *
 ****************************************************************************/

#include "inktomi++.h"
#include "ink_platform.h"
#include "ink_unused.h" /* MAGIC_EDITING_TAG */

#include "SimpleTokenizer.h"

#include "WebCompatibility.h"
#include "WebHttp.h"
#include "WebHttpAuth.h"
#include "WebHttpContext.h"
#include "WebHttpLog.h"
#include "WebHttpMessage.h"
#include "WebHttpRender.h"
#include "WebHttpSession.h"
#include "WebHttpTree.h"
#include "WebOverview.h"
#include "WebConfig.h"

#include "INKMgmtAPI.h"
//#include "I_AccCrypto.h"
#include "LocalManager.h"
#include "RecordsConfig.h"
#include "WebMgmtUtils.h"
#include "MgmtUtils.h"
#include "EnvBlock.h"
#include "CfgContextUtils.h"

#include "ConfigAPI.h"
#include "SysAPI.h"

#ifdef HAVE_LIBSSL
#include "openssl/ssl.h"
#include "openssl/err.h"
#include "openssl/crypto.h"
#if !defined(_WIN32)
// Ugly hack - define HEAP_H and STACK_H to prevent stuff from the
// template library from being included which SUNPRO CC does not not
// like.
#define HEAP_H
#define STACK_H
#endif // !defined(_WIN32)
#endif // HAVE_LIBSSL


//-------------------------------------------------------------------------
// defines
//-------------------------------------------------------------------------

#ifndef _WIN32
#define DIR_MODE S_IRWXU
#define FILE_MODE S_IRWXU
#else
#define FILE_MODE S_IWRITE
#endif

#define MAX_ARGS         10
#define MAX_TMP_BUF_LEN  1024

#define MAX_ADD_RULES    50     // update c_config_display.ink

//-------------------------------------------------------------------------
// types
//-------------------------------------------------------------------------

typedef int (*WebHttpHandler) (WebHttpContext * whc, const char *file);

//-------------------------------------------------------------------------
// globals
//-------------------------------------------------------------------------

// only allow access to specific files on the autoconf port
static InkHashTable *g_autoconf_allow_ht = 0;

static InkHashTable *g_submit_bindings_ht = 0;
static InkHashTable *g_file_bindings_ht = 0;
static InkHashTable *g_extn_bindings_ht = 0;

InkHashTable *g_display_config_ht = 0;

//-------------------------------------------------------------------------
// prototypes
//-------------------------------------------------------------------------

void spawn_script(WebHttpContext * whc, char *script, char **args);

//-------------------------------------------------------------------------
// record_version_valid
//-------------------------------------------------------------------------

static bool
record_version_valid(char *record_version)
{
  int old_version, old_pid, cur_version;
  // coverity[secure_coding]
  if (sscanf(record_version, "%d:%d", &old_pid, &old_version) == 2 && old_version >= 0) {
    cur_version = RecGetRecordUpdateCount(RECT_CONFIG);
    //fix me --> lmgmt->record_data->pid
    if (cur_version != old_version || lmgmt->record_data->pid != old_pid) {
      // we are out of date since the version number has been incremented
      return false;
    } else {
      return true;
    }
  }
  // bad format, return false to be safe
  return false;
}

//-------------------------------------------------------------------------
// set_record_value
//-------------------------------------------------------------------------

static bool
set_record_value(WebHttpContext * whc, const char *rec, const char *value)
{
  MgmtData varValue;
  char *record;
  char *script = NULL;
  char *script_path;

  if (rec == NULL) {
    return false;
  }
  if (value == NULL) {
    value = "";
  }
  // INKqa11771: exec script that associates with a record
  record = xstrdup(rec);
  if ((script = strchr(record, ':'))) {
    *script = '\0';
    script++;
  }
  // FIXME: If someone else has already added a NOTE or WARN, then we
  // won't be able to add anymore.  This is desired for
  // handle_submit_update, but going forward, we'll need a more
  // general mechanism.

  varValue.setFromName(record);
  if (varValue.compareFromString(value) == false) {
    if (recordValidityCheck(record, value)) {
      if (recordRestartCheck(record)) {
        ink_hash_table_insert(whc->submit_note_ht, record, NULL);
        if (!(whc->request_state & WEB_HTTP_STATE_SUBMIT_NOTE)) {
          HtmlRndrText(whc->submit_note, whc->lang_dict_ht, HTML_ID_RESTART_REQUIRED);
          HtmlRndrBr(whc->submit_note);
        }
        whc->request_state |= WEB_HTTP_STATE_SUBMIT_NOTE;
      }
      varSetFromStr(record, value);

#ifndef _WIN32
      if (script) {
        const char *args[MAX_ARGS + 1];
        for (int i = 0; i < MAX_ARGS; i++)
          args[i] = NULL;
        script_path = WebHttpAddDocRoot_Xmalloc(whc, script);
        args[0] = script_path;
        args[1] = value;
        processSpawn(&args[0], NULL, NULL, NULL, false, false);
        xfree(script_path);
      }
#endif
    } else {
      ink_hash_table_insert(whc->submit_warn_ht, record, NULL);
      if (!(whc->request_state & WEB_HTTP_STATE_SUBMIT_WARN)) {
        HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_INVALID_ENTRY);
        HtmlRndrBr(whc->submit_warn);
      }
      whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
    }
  }
  xfree(record);
  return true;
}

//-------------------------------------------------------------------------
// set_config_file
//-------------------------------------------------------------------------

static bool
set_config_file(WebHttpContext * whc, char *file_version, char *file_contents, char *file_checksum)
{

  bool err = true;

  Rollback *rb;
  textBuffer *tb;
  version_t fversion;
  char frecord[MAX_VAR_LENGTH + 1];
  char fname[MAX_VAL_LENGTH + 1];
  char checksum[MAX_CHECKSUM_LENGTH + 1];
  int file_size;

  // coverity[secure_coding]
  if (sscanf(file_version, "%d:%s", &fversion, frecord) == 2) {
    if (varStrFromName(frecord, fname, MAX_VAL_LENGTH)) {
      if (configFiles->getRollbackObj(fname, &rb)) {
        // INKqa12198: remove ^M (CR) from each line in file_contents
        convertHtmlToUnix(file_contents);
        file_size = strlen(file_contents);
        tb = NEW(new textBuffer(file_size + 1));
        tb->copyFrom(file_contents, file_size); //orig

        // calculate checksum - skip file update if match checksum
        fileCheckSum(tb->bufPtr(), tb->spaceUsed(), checksum, sizeof(checksum));
        if (strcmp(file_checksum, checksum) != 0) {
          if (rb->updateVersion(tb, fversion) != OK_ROLLBACK) {
            err = false;
          }
          // put note if file update required restart
          if (recordRestartCheck(frecord)) {
            ink_hash_table_insert(whc->submit_note_ht, frecord, NULL);
            if (!(whc->request_state & WEB_HTTP_STATE_SUBMIT_NOTE)) {
              HtmlRndrText(whc->submit_note, whc->lang_dict_ht, HTML_ID_RESTART_REQUIRED);
              HtmlRndrBr(whc->submit_note);
            }
            whc->request_state |= WEB_HTTP_STATE_SUBMIT_NOTE;
          }
        }
        delete tb;
      }
    }
  }
  return err;
}

//-------------------------------------------------------------------------
// spawn_cgi
//-------------------------------------------------------------------------

#if defined(_WIN32)

// adjustCmdLine
//
// This function is used for constructing a command line from a CGI
// scripting program because Windows doesn't know how to execute a
// script.  For example, instead of executing "blacklist.cgi", we need
// to tell Windows to execute "perl.exe blacklist.cgi".

static void
adjustCmdLine(char *cmdLine, int cmdline_len, const char *cgiFullPath)
{
  char line[1024 + 1];
  char *interpreter = NULL;

  FILE *f = fopen(cgiFullPath, "r");
  if (f != NULL) {
    if (fgets(line, 1024, f) != NULL) {
      int n = strlen(line);
      if (n > 2 && strncmp(line, "#!", 2) == 0 && line[n - 1] == '\n') {
        line[n - 1] = '\0';
        interpreter = line + 2;
      }
    }
    fclose(f);
  }

  if (interpreter) {
    snprintf(cmdLine, cmdline_len, "\"%s\" \"%s\"", interpreter, cgiFullPath);
  } else {
    ink_strncpy(cmdLine, cgiFullPath, cmdline_len);
  }
  return;
}

#endif

int
spawn_cgi(WebHttpContext * whc, const char *cgi_path, char **args, bool nowait, bool run_as_root)
{

  struct stat info;
  const char *query_string;
  textBuffer query_string_tb(MAX_TMP_BUF_LEN);
  int qlen = 0;
  char content_length_buffer[10];
  EnvBlock env;
  bool success = false;
  const char *a[MAX_ARGS + 2];
  int i;

  httpMessage *request = whc->request;
  textBuffer *replyMsg = whc->response_bdy;
  httpResponse *answerHdr = whc->response_hdr;

  // check if file exists
  if (stat(cgi_path, &info) < 0) {
    WebHttpSetErrorResponse(whc, STATUS_NOT_FOUND);
    answerHdr->setStatus(STATUS_NOT_FOUND);
    return WEB_HTTP_ERR_REQUEST_ERROR;
  }
  // initialize arguments
  for (i = 0; i < MAX_ARGS + 2; i++)
    a[i] = NULL;
  a[0] = cgi_path;
  for (i = 1; i < MAX_ARGS + 1 && args && args[i - 1]; i++)
    a[i] = args[i - 1];

  // initialize environment
  if (request->getContentType() != NULL) {
    env.setVar("CONTENT_TYPE", request->getContentType());
  }
  if (request->getMethod() == METHOD_POST) {
    env.setVar("REQUEST_METHOD", "POST");
    query_string = request->getBody();

  } else if (request->getMethod() == METHOD_GET) {
    env.setVar("REQUEST_METHOD", "GET");
    query_string = request->getQuery();

  } else {
    answerHdr->setStatus(STATUS_NOT_IMPLEMENTED);
    WebHttpSetErrorResponse(whc, STATUS_NOT_IMPLEMENTED);
    return WEB_HTTP_ERR_REQUEST_ERROR;;
  }
  if (query_string != NULL) {

    // use getConLen() to handle binary
    qlen = request->getConLen();
    if (qlen <= 0)
      qlen = strlen(query_string);
    snprintf(content_length_buffer, sizeof(content_length_buffer), "%d", qlen);
    env.setVar("CONTENT_LENGTH", content_length_buffer);
    env.setVar("QUERY_STRING", query_string);

    query_string_tb.copyFrom(query_string, qlen);
  }
#ifndef _WIN32
  if (processSpawn(&a[0], &env, &query_string_tb, replyMsg, nowait, run_as_root) != 0) {
    mgmt_elog(stderr, "[spawn_cgi] Unable to fork child process\n");
    WebHttpSetErrorResponse(whc, STATUS_INTERNAL_SERVER_ERROR);
    answerHdr->setStatus(STATUS_INTERNAL_SERVER_ERROR);

  } else {
    success = true;
  }
#else

  char buffer[1024];
  char cmdLine[PATH_MAX * 2 + 6];
  DWORD nbytes = 0;

  SECURITY_ATTRIBUTES saAttr;
  saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
  saAttr.bInheritHandle = TRUE;
  saAttr.lpSecurityDescriptor = NULL;

  // STDIN
  HANDLE hChildStdinR = NULL;
  HANDLE hChildStdinW = NULL;

  CreatePipe(&hChildStdinR, &hChildStdinW, &saAttr, 0);

  // Dup to NULL and set inheritable to FALSE so that
  // it won't be inherited by the child process
  DuplicateHandle(GetCurrentProcess(), hChildStdinW, GetCurrentProcess(), NULL, 0, FALSE, DUPLICATE_SAME_ACCESS);

  // STDOUT
  HANDLE hChildStdoutR = NULL;
  HANDLE hChildStdoutW = NULL;

  CreatePipe(&hChildStdoutR, &hChildStdoutW, &saAttr, 0);

  // Dup to NULL and set inheritable to FALSE so that
  // it won't be inherited by the child process
  DuplicateHandle(GetCurrentProcess(), hChildStdoutR, GetCurrentProcess(), NULL, 0, FALSE, DUPLICATE_SAME_ACCESS);

  STARTUPINFO suInfo;
  PROCESS_INFORMATION procInfo;
  ZeroMemory((PVOID) & suInfo, sizeof(suInfo));

  // hide the new console window from the user
  suInfo.cb = sizeof(STARTUPINFO);
  suInfo.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
  suInfo.wShowWindow = SW_HIDE;
  suInfo.hStdInput = hChildStdinR;
  suInfo.hStdOutput = hChildStdoutW;
  suInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);

  adjustCmdLine(cmdLine, sizeof(cmdLine), cgi_path);

  if (CreateProcess(NULL, cmdLine, NULL,        // FIX THIS: process security attributes
                    NULL,       // FIX THIS: thread security attributes
                    TRUE,       // make handles inheritable
                    0,          // FIX THIS: specify a priority
                    env.toString(), ts_base_dir,        // make script run from TSBase
                    &suInfo, &procInfo) == FALSE) {

    mgmt_elog(stderr, "[spawn_cgi] CreateProcess error: %s\n", ink_last_err());
    WebHttpSetErrorResponse(whc, STATUS_INTERNAL_SERVER_ERROR);
    answerHdr->setStatus(STATUS_INTERNAL_SERVER_ERROR);

  } else {

    CloseHandle(hChildStdinR);
    if (request->getMethod() == METHOD_POST && query_string != NULL) {
      WriteFile(hChildStdinW, query_string, qlen, &nbytes, NULL);
    }
    CloseHandle(hChildStdinW);

    CloseHandle(hChildStdoutW);
    while (ReadFile(hChildStdoutR, buffer, 1024, &nbytes, NULL) == TRUE) {
      if (nbytes == 0) {
        break;
      }
      replyMsg->copyFrom(buffer, nbytes);
    }
    CloseHandle(hChildStdoutR);
    success = true;
  }

#endif

  // was this a plugin callout?
  if (whc->request_state & WEB_HTTP_STATE_PLUGIN) {
    // notify server plugin to update its config
    if (success == true && query_string != NULL) {
      int i;
      char *plugin_name = new char[qlen];
      const char *tmp = strstr(query_string, "INK_PLUGIN_NAME=");
      if (tmp != NULL) {
        tmp += strlen("INK_PLUGIN_NAME=");
        for (i = 0; *tmp != '&' && *tmp != '\0'; tmp++) {
          plugin_name[i++] = *tmp;
        }
        plugin_name[i] = '\0';
        substituteUnsafeChars(plugin_name);
        lmgmt->signalEvent(MGMT_EVENT_PLUGIN_CONFIG_UPDATE, plugin_name);
      }
      delete[]plugin_name;
    }
  }

  return WEB_HTTP_ERR_OKAY;

}

//-------------------------------------------------------------------------
// encryptToFileAuth_malloc
//
// Given the clear-case password, this function will encrypt the password
// and print the key to a unique file (name assembled from timestamp and
// stored in the path specified by an auth record)
// Returns the filename of this file or NULL if the encryption failed.
// Used for bind_pwd_file in filter.config and for radius shared keys.
//-------------------------------------------------------------------------
char *
encryptToFileAuth_malloc(const char *password)
{
  RecString dir_path;
  RecGetRecordString_Xmalloc("proxy.config.auth.password_file_path", &dir_path);

  if (!dir_path)
    return NULL;

  char file_path[MAX_TMP_BUF_LEN + 1];
  time_t my_time_t;
  time(&my_time_t);
  memset(file_path, 0, MAX_TMP_BUF_LEN);
  snprintf(file_path, MAX_TMP_BUF_LEN, "%s%spwd_%ld.enc", dir_path, DIR_SEP, my_time_t);
  if (dir_path)
    xfree(dir_path);

//  AuthString fileAuthStr(file_path);
//  AuthString passwdAuthStr(password);
/*  if (!AccCrypto::encryptToFile(fileAuthStr, passwdAuthStr)) {
    Debug("config", "[encryptToFileAuth_malloc] Failed to encrypt password");

  }*/

  return xstrdup(file_path);
}


//-------------------------------------------------------------------------
// handle_cgi_extn
//-------------------------------------------------------------------------

static int
handle_cgi_extn(WebHttpContext * whc, const char *file)
{
  int err;
  char *cgi_path;
  whc->response_hdr->setCachable(0);
  whc->response_hdr->setStatus(STATUS_OK);
  whc->response_hdr->setContentType(TEXT_HTML);
  cgi_path = WebHttpAddDocRoot_Xmalloc(whc, whc->request->getFile());
  err = spawn_cgi(whc, cgi_path, NULL, false, false);
  xfree(cgi_path);
  return err;
}

//-------------------------------------------------------------------------
// handle_ink_extn
//-------------------------------------------------------------------------

static int
handle_ink_extn(WebHttpContext * whc, const char *file)
{
  int err;
  if ((err = WebHttpRender(whc, file)) == WEB_HTTP_ERR_OKAY) {
    whc->response_hdr->setStatus(STATUS_OK);
    whc->response_hdr->setLength(whc->response_bdy->spaceUsed());
    whc->response_hdr->setContentType(TEXT_HTML);
  }
  return err;
}

//-------------------------------------------------------------------------
// handle_chart
//-------------------------------------------------------------------------

static int
handle_chart(WebHttpContext * whc, const char *file)
{

  //-----------------------------------------------------------------------
  // FIXME: HARD-CODED HTML HELL!!!
  //-----------------------------------------------------------------------

  // Note that chart.cgi is a special case so it can not be handled
  // like our other submit_bindings; the browswer can access the cgi
  // either by a GET/query or by a POST/body combo.

  int err = WEB_HTTP_ERR_OKAY;

  httpMessage *request = whc->request;
  textBuffer *replyMsg = whc->response_bdy;
  httpResponse *answerHdr = whc->response_hdr;
  InkHashTable *post_data_ht, *params;

  char *varName = NULL;
  int varNameLen;
  char tmpVal[MAX_VAL_LENGTH];
  bool postForm = true;
  bool clusterGraph = false;

  //800, 410
  static const char dimensions[] = "width=\"1600\" height=\"1200\"";
  static const char multiGraph[] = "Inktomi Real-time Graphing";
  int numGraphs = 0;
  const int totalNumGraphs = 10;
  char numGraphStr[8];
  char *theGraphs[totalNumGraphs];
  const char *theGraphNames[totalNumGraphs];
  const char *graphNames[] = {
    "Document Hit Rate", "Bandwidth Savings", "Cache Percent Free",
    "Open Server Connections", "Open Client Connections",
    "Cache Transfers In Progress", "Client Throughput",
    "Transactions Per Second", "Host Database Hit Rate",
    "DNS Lookups Per Second"
  };

  static const char str1[] = "<html>\n" "<title>";
  static const char str1_5[] =
    "</title>\n" "<body><b> No variable(s) were selected for graphing. </b></body>\n" "</html>\n";
  static const char str2[] =
    "</title>\n"
    "<body bgcolor=\"#C0C0C0\" onResize=\"resize()\" onLoad=\"resize()\" "
    " topmargin=\"0\" leftmargin=\"0\" marginwidth=\"0\" marginheight=\"0\">\n"
    "<SCRIPT LANGUAGE=\"JavaScript\">\n"
    "   function myFunc(page, winName) {\n"
    "          window.open(page, winName, \"width=850,height=435,status,resizable=yes\");\n"
    "   }\n"
    "   function resize() {\n"
    "	var w_newWidth,w_newHeight;\n"
    "	var w_maxWidth=1600,w_maxHeight=1200;\n"
    "	if (navigator.appName.indexOf(\"Microsoft\") != -1)\n"
    "	{\n"
    "		w_newWidth=document.body.clientWidth;\n"
    "		w_newHeight=document.body.clientHeight;\n"
    "	} else {\n"
    "		var netscapeScrollWidth=15;\n"
    "		w_newWidth=window.innerWidth-netscapeScrollWidth;\n"
    "		w_newHeight=window.innerHeight-netscapeScrollWidth;\n"
    "	}\n"
    "	if (w_newWidth>w_maxWidth)\n"
    "		w_newWidth=w_maxWidth;\n"
    "	if (w_newHeight>w_maxHeight)\n"
    "		w_newHeight=w_maxHeight;\n"
    "	document.ink_chart.resizeFrame(w_newWidth,w_newHeight);\n"
    "        window.scroll(0,0);\n" "   }\n" "   window.onResize = resize;\n" "   window.onLoad = resize;\n"
    //kwt
    "   function closeTheBrowser() {\n"
    "   window.close();\n"
    "   }\n"
    "   function SnapshotAlert() {\n"
    "   window.alert(\"Snapshot is currently not supported on SSL connection.\");\n" "   }\n"
    //kwt
    "</SCRIPT>\n"
    "<applet NAME=\"ink_chart\" CODE=\"InktomiCharter.class\" " " ARCHIVE=\"/charting/InkChart.jar\" MAYSCRIPT ";
  static const char str3[] = ">\n<param name=ServerName value=\"";
  static const char str3_2[] = "\">\n<param name=ServerPort value=\"";
  static const char str3_3[] = "\">\n<param name=ServerWebPort value=\"";
  static const char str3_4[] = "\">\n<param name=Graphs value=\"";
  static const char str3_5[] = "\">\n<param name=StatNames   value=\"";
  static const char str3_6[] = "\">\n<param name=SSL value=\"";
  static const char str4[] = "\">\n</applet>\n</body>\n</html>\n";

  static const int str1Len = strlen(str1);
  static const int str1_5Len = strlen(str1_5);
  static const int str2Len = strlen(str2);
  static const int str3Len = strlen(str3);
  static const int str3_2Len = strlen(str3_2);
  static const int str3_3Len = strlen(str3_3);
  static const int str3_4Len = strlen(str3_4);
  static const int str3_5Len = strlen(str3_5);
  static const int str3_6Len = strlen(str3_6);
  static const int str4Len = strlen(str4);

  // The graph Generator is a POST form, while the cluster graphs are
  // GET forms.  If we get nothing, assume that we have a postForm.
  post_data_ht = processFormSubmission(request->getBody());
  if (post_data_ht == NULL) {
    postForm = false;
    params = whc->query_data_ht;
    // If we still didn't get anything, there is nothing to be had
    if (params == NULL) {
      err = WEB_HTTP_ERR_REQUEST_ERROR;
      goto Ldone;
    }
  } else {
    params = post_data_ht;
  }

  if (postForm == false) {
    // We are trying to generate a cluster graph for a node variable
    int ink_hash_lookup_result = ink_hash_table_lookup(params, "cluster", (void **) &varName);
    if (!ink_hash_lookup_result || varName == NULL) {
      mgmt_log(stderr, "Invalid Graph Submission No graph will be generated\n");
      err = WEB_HTTP_ERR_REQUEST_ERROR;
      goto Ldone;
    }
    clusterGraph = true;
  } else {
    for (int i = 0; i < totalNumGraphs; i++) {
      if (ink_hash_table_lookup(params, graphNames[i], (void **) &varName)) {
        theGraphs[numGraphs] = (char *) varName;
        theGraphNames[numGraphs] = graphNames[i];
        numGraphs += 1;
      }
    }
    clusterGraph = false;
  }

  varNameLen = varName ? strlen(varName) : 0;

  // Build the reply
  replyMsg->copyFrom(str1, str1Len);
  if (clusterGraph == true && varName) {
    replyMsg->copyFrom(varName, varNameLen);
  } else {
    replyMsg->copyFrom(multiGraph, strlen(multiGraph));
    if (numGraphs == 0) {
      replyMsg->copyFrom(str1_5, str1_5Len);
      answerHdr->setStatus(STATUS_OK);
      goto Ldone;
    }
  }
  replyMsg->copyFrom(str2, str2Len);
  replyMsg->copyFrom(dimensions, strlen(dimensions));

  replyMsg->copyFrom(str3, str3Len);
  varStrFromName("proxy.node.hostname_FQ", tmpVal, MAX_VAL_LENGTH);
  replyMsg->copyFrom(tmpVal, strlen(tmpVal));

  replyMsg->copyFrom(str3_2, str3_2Len);
  varStrFromName("proxy.config.admin.overseer_port", tmpVal, MAX_VAL_LENGTH);
  replyMsg->copyFrom(tmpVal, strlen(tmpVal));

  replyMsg->copyFrom(str3_3, str3_3Len);
  varStrFromName("proxy.config.admin.web_interface_port", tmpVal, MAX_VAL_LENGTH);
  replyMsg->copyFrom(tmpVal, strlen(tmpVal));

  replyMsg->copyFrom(str3_4, str3_4Len);
  if (clusterGraph == true) {
    replyMsg->copyFrom("CLUSTER", 7);
  } else {
    snprintf(numGraphStr, sizeof(numGraphStr), "%d", numGraphs);
    replyMsg->copyFrom(numGraphStr, strlen(numGraphStr));
  }

  replyMsg->copyFrom(str3_5, str3_5Len);
  if (clusterGraph == true) {
    replyMsg->copyFrom(varName, varNameLen);
  } else {
    for (int j = 1; j < numGraphs; j++) {
      replyMsg->copyFrom(theGraphs[j], strlen(theGraphs[j]));
      replyMsg->copyFrom(",", 1);
      replyMsg->copyFrom(theGraphNames[j], strlen(theGraphNames[j]));

      replyMsg->copyFrom(",", 1);
    }
    replyMsg->copyFrom(theGraphs[0], strlen(theGraphs[0]));
    replyMsg->copyFrom(",", 1);
    replyMsg->copyFrom(theGraphNames[0], strlen(theGraphNames[0]));
  }

  replyMsg->copyFrom(str3_6, str3_6Len);
  if (whc->server_state & WEB_HTTP_SERVER_STATE_SSL_ENABLED) {
    replyMsg->copyFrom("enabled", strlen("enabled"));
  } else {
    replyMsg->copyFrom("disabled", strlen("disabled"));
  }

  replyMsg->copyFrom(str4, str4Len);
  answerHdr->setLength(strlen(replyMsg->bufPtr()));

Ldone:
  if (post_data_ht) {
    ink_hash_table_destroy_and_xfree_values(post_data_ht);
  }
  return err;

}

//-------------------------------------------------------------------------
// handle_record_info
//
// Warning!!! This is really hacky since we should not be directly
// accessing the librecords data structures.  Just do this here
// tempoarily until we can have something better.
//-------------------------------------------------------------------------

#include "P_RecCore.h"

#define LINE_SIZE 512
#define BUF_SIZE 128
#define NULL_STR "NULL"
static int
handle_record_info(WebHttpContext * whc, bool statistic_type, bool rec)
{

  //-----------------------------------------------------------------------
  // FIXME: HARD-CODED HTML HELL!!!
  //-----------------------------------------------------------------------

  int type_pcnt = 15;
  int name_pcnt = 25;
  // Doesn't seem to be used.
  int INK_UNUSED description_pcnt = 25;
  int value_type_pcnt = 15;
  int def_value_pcnt = 10;
  int cur_value_pcnt = 10;

  int r;
  int num_records;

  char line[LINE_SIZE];
  const char *random_html;
  char cur_value[BUF_SIZE + 1];
  char *cur_value_safe;
  char def_value_rec[BUF_SIZE + 1];
  const char *def_value;
  char *def_value_safe;
  const char *title;

  const char *rec_type_a[RECT_MAX];
  const char *data_type_a[RECD_MAX];
  const char *type_a[MAX_RECORD_TYPE];
  const char *value_type_a[MAX_MGMT_TYPE];

  bool found;
  bool same;

  if (rec) {
    textBuffer *replyMsg = whc->response_bdy;

    // init rec_type_a and data_type_a
    rec_type_a[RECT_CONFIG] = "CONFIG";
    rec_type_a[RECT_PROCESS] = "PROCESS";
    rec_type_a[RECT_NODE] = "NODE";
    rec_type_a[RECT_CLUSTER] = "CLUSTER";
    rec_type_a[RECT_LOCAL] = "LOCAL";
    rec_type_a[RECT_PLUGIN] = "PLUGIN";
    data_type_a[RECD_INT] = "INT";
    data_type_a[RECD_LLONG] = "LLONG";
    data_type_a[RECD_FLOAT] = "FLOAT";
    data_type_a[RECD_STRING] = "STRING";
    data_type_a[RECD_COUNTER] = "COUNTER";

    if (statistic_type) {
      title = "Statistics";
    } else {
      title = "Configurations";
    }

    random_html = "<html>\n" "<head><title>";
    replyMsg->copyFrom(random_html, strlen(random_html));
    replyMsg->copyFrom(title, strlen(title));
    random_html = "</title></head>"
      "<style>\n"
      ".large_font {font-family: Verdana, Arial, Helvetica, sans-serif; font-size: 18px; font-weight: bold; color=#000000}\n"
      ".small_font {font-family: Verdana, Arial, Helvetica, sans-serif; font-size: 11px}\n"
      "</style>\n"
      "<body bgcolor=#003366>\n"
      "<table border=\"1\" cellspacing=\"0\" cellpadding=\"3\" bordercolor=#CCCCCC bgcolor=\"white\" width=\"100%\" class=\"small_font\">\n"
      "<tr><td colspan=\"5\" align=\"right\" class=\"large_font\">";
    replyMsg->copyFrom(random_html, strlen(random_html));
    replyMsg->copyFrom(title, strlen(title));
    random_html = "&nbsp;</td></tr>\n"
      "<tr bgcolor=#EEEEEE><td>Record Type</td><td>Record Name</td><td>Data Type</td><td>Data</td><td>Default Data</td></tr>\n";
    replyMsg->copyFrom(random_html, strlen(random_html));

    // generate all other rows of the table
    num_records = g_num_records;
    Debug("web2", "# of records = %d", num_records);
    for (r = 0; r < num_records; r++) {
      bool okay;
      if (statistic_type) {
        okay = ((g_records[r].rec_type == RECT_PROCESS) ||
                (g_records[r].rec_type == RECT_NODE) ||
                (g_records[r].rec_type == RECT_PLUGIN) || (g_records[r].rec_type == RECT_CLUSTER));
      } else {
        okay = ((g_records[r].rec_type == RECT_CONFIG) ||
                (g_records[r].rec_type == RECT_PLUGIN) || (g_records[r].rec_type == RECT_LOCAL));
      }
      Debug("web2", "%s", g_records[r].name);
      if (okay) {
        snprintf(line, sizeof(line), "<tr>\n");
        replyMsg->copyFrom(line, strlen(line));
        // record type
        snprintf(line, sizeof(line), "<td>%s</td>\n", rec_type_a[g_records[r].rec_type]);
        replyMsg->copyFrom(line, strlen(line));
        // name
        snprintf(line, sizeof(line), "<td>%s</td>\n", g_records[r].name);
        replyMsg->copyFrom(line, strlen(line));
        // data type
        snprintf(line, sizeof(line), "<td>%s</td>\n", data_type_a[g_records[r].data_type]);
        replyMsg->copyFrom(line, strlen(line));
        // current value (computation)
        same = false;
        *cur_value = '\0';
        *def_value_rec = '\0';
        rec_mutex_acquire(&(g_records[r].lock));
        switch (g_records[r].data_type) {
        case RECD_INT:
          {
            RecInt data = g_records[r].data.rec_int;
            RecInt data_default = g_records[r].data_default.rec_int;
            snprintf(cur_value, BUF_SIZE, "%lld", data);
            snprintf(def_value_rec, BUF_SIZE, "%lld", data_default);
            same = (data == data_default);
          }
          break;
        case RECD_LLONG:
          {
            RecLLong data = g_records[r].data.rec_llong;
            RecLLong data_default = g_records[r].data_default.rec_llong;
            snprintf(cur_value, BUF_SIZE, "%lld", data);
            snprintf(def_value_rec, BUF_SIZE, "%lld", data_default);
            same = (data == data_default);
          }
          break;
        case RECD_FLOAT:
          {
            RecFloat data = g_records[r].data.rec_float;
            RecFloat data_default = g_records[r].data_default.rec_float;
            snprintf(cur_value, BUF_SIZE, "%f", data);
            snprintf(def_value_rec, BUF_SIZE, "%f", data_default);
            float d = data - data_default;
            same = ((d > -0.000001f) && (d < 0.000001f));
          }
          break;
        case RECD_STRING:
          {
            RecString data = g_records[r].data.rec_string;
            RecString data_default = g_records[r].data_default.rec_string;
            if (data) {
              strncpy(cur_value, data, BUF_SIZE);
              same = (data_default && (strcmp(data, data_default) == 0));
            } else {
              ink_strncpy(cur_value, NULL_STR, sizeof(cur_value));
              same = (data_default == NULL);
            }
            if (data_default) {
              strncpy(def_value_rec, data_default, BUF_SIZE);
            } else {
              ink_strncpy(def_value_rec, NULL_STR, sizeof(def_value_rec));
            }
          }
          break;
        case RECD_COUNTER:
          {
            RecCounter data = g_records[r].data.rec_counter;
            RecCounter data_default = g_records[r].data_default.rec_counter;
            snprintf(cur_value, BUF_SIZE, "%lld", data);
            snprintf(def_value_rec, BUF_SIZE, "%lld", data_default);
            same = (data == data_default);
          }
          break;
        default:
          // Handled here:
          // RECD_NULL, RECD_STAT_CONST, RECD_STAT_FX, RECD_MAX
          break;
        }

        rec_mutex_release(&(g_records[r].lock));

        // safify strings
        cur_value_safe = substituteForHTMLChars(cur_value);
        def_value_safe = substituteForHTMLChars(def_value_rec);

        // current value (print)
        if (same) {
          snprintf(line, sizeof(line), "<td bgcolor=\"#EEEEEE\">%s</td>", cur_value_safe);
        } else {
          snprintf(line, sizeof(line), "<td>%s</td>\n", cur_value_safe);
        }
        replyMsg->copyFrom(line, strlen(line));

        // default value (print)
        snprintf(line, sizeof(line), "<td bgcolor=\"#EEEEEE\">%s</td>\n", def_value_safe);
        replyMsg->copyFrom(line, strlen(line));

        snprintf(line, sizeof(line), "</tr>\n");
        replyMsg->copyFrom(line, strlen(line));

        // free mem
        delete[]def_value_safe;
        delete[]cur_value_safe;

      }
    }

    // finish up html
    random_html = "<tr bgcolor=#EEEEEE><td colspan=\"5\">&nbsp;</td></tr>\n" "</table>\n</body>\n</html>\n";
    replyMsg->copyFrom(random_html, strlen(random_html));

  } else {

    textBuffer *replyMsg = whc->response_bdy;

    // init type_a and value_type_a
    type_a[CONFIG] = "CONFIG";
    type_a[PROCESS] = "PROCESS";
    type_a[NODE] = "NODE";
    type_a[CLUSTER] = "CLUSTER";
    type_a[LOCAL] = "LOCAL";
    type_a[PLUGIN] = "PLUGIN";
    value_type_a[INK_INT] = "INT";
    value_type_a[INK_LLONG] = "LLONG";
    value_type_a[INK_FLOAT] = "FLOAT";
    value_type_a[INK_STRING] = "STRING";
    value_type_a[INK_COUNTER] = "COUNTER";

    if (statistic_type) {
      title = "Statistics";
    } else {
      title = "Configurations";
    }

    // start generating document
    snprintf(line, sizeof(line), "<html>\n<head>\n<title>%s</title>\n</head>\n<body>\n", title);
    replyMsg->copyFrom(line, strlen(line));
    snprintf(line, sizeof(line), "<body bgcolor=\"#FFFFFF\">\n");
    replyMsg->copyFrom(line, strlen(line));
    snprintf(line, sizeof(line), "<h1>%s</h1>\n", title);
    replyMsg->copyFrom(line, strlen(line));

    // start table
    snprintf(line, sizeof(line), "<table border=1 cellspacing=0 cellpadding=1 width=\"100%%\" bordercolor=#CCCCCC "
                 "style=\"font-size: smaller\">\n");
    replyMsg->copyFrom(line, strlen(line));

    // generate column title row
    snprintf(line, sizeof(line), "<tr>\n");
    replyMsg->copyFrom(line, strlen(line));
    snprintf(line, sizeof(line), "<td width=\"%d%%\" align=\"center\" bgcolor=\"#E0E0F6\" nowrap>"
                 "<p align=\"center\"><strong>Type</strong></td>\n", type_pcnt);
    replyMsg->copyFrom(line, strlen(line));
    snprintf(line, sizeof(line), "<td width=\"%d%%\" align=\"center\" bgcolor=\"#E0E0F6\" nowrap>"
                 "<p align=\"center\"><strong>Name</strong></td>\n", name_pcnt);
    replyMsg->copyFrom(line, strlen(line));
    snprintf(line, sizeof(line), "<td width=\"%d%%\" align=\"center\" bgcolor=\"#E0E0F6\" nowrap>"
                 "<p align=\"center\"><strong>Value Type</strong></td>\n", value_type_pcnt);
    replyMsg->copyFrom(line, strlen(line));
    snprintf(line, sizeof(line), "<td width=\"%d%%\" align=\"center\" bgcolor=\"#E0E0F6\" nowrap>"
                 "<p align=\"center\"><strong>Current Value</strong></td>\n", cur_value_pcnt);
    replyMsg->copyFrom(line, strlen(line));
    snprintf(line, sizeof(line), "<td width=\"%d%%\" align=\"center\" bgcolor=\"#E0E0F6\" nowrap>"
                 "<p align=\"center\"><strong>Default Value</strong></td>\n", def_value_pcnt);
    replyMsg->copyFrom(line, strlen(line));
    snprintf(line, sizeof(line), "</tr>\n");
    replyMsg->copyFrom(line, strlen(line));

    // generate all other rows of the table
    for (r = 0; RecordsConfig[r].value_type != INVALID; r++) {
      bool okay;
      if (statistic_type) {
        okay = ((RecordsConfig[r].type == PROCESS) ||
                (RecordsConfig[r].type == NODE) ||
                (RecordsConfig[r].type == PLUGIN) || (RecordsConfig[r].type == CLUSTER));
      } else {
        okay = ((RecordsConfig[r].type == CONFIG) ||
                (RecordsConfig[r].type == PLUGIN) || (RecordsConfig[r].type == LOCAL));
      }
      if (okay) {
        snprintf(line, sizeof(line), "<tr>\n");
        replyMsg->copyFrom(line, strlen(line));
        // type
        snprintf(line, sizeof(line), "<td width=\"%d%%\" align=\"center\" bgcolor=\"#FFFFFF\">"
                     "<p align=\"left\">%s</td>\n", type_pcnt, type_a[RecordsConfig[r].type]);
        replyMsg->copyFrom(line, strlen(line));
        // name
        snprintf(line, sizeof(line), "<td width=\"%d%%\" align=\"center\" bgcolor=\"#FFFFFF\">"
                     "<p align=\"left\">%s</td>\n", name_pcnt, RecordsConfig[r].name);
        replyMsg->copyFrom(line, strlen(line));
        // value type
        snprintf(line, sizeof(line), "<td width=\"%d%%\" align=\"center\" bgcolor=\"#FFFFFF\">"
                     "<p align=\"left\">%s</td>\n", value_type_pcnt, value_type_a[RecordsConfig[r].value_type]);
        replyMsg->copyFrom(line, strlen(line));
        // current value (computation)
        same = false;
        *cur_value = '\0';
        switch (RecordsConfig[r].value_type) {
        case INK_INT:
          {
            MgmtInt i = lmgmt->record_data->readInteger(RecordsConfig[r].name, &found);
            if (found) {
              snprintf(cur_value, BUF_SIZE, "%lld", i);
              if (i == ink_atoll(RecordsConfig[r].value)) {
                same = true;
              }
            }
          }
          break;
        case INK_LLONG:
          {
            MgmtLLong i = lmgmt->record_data->readLLong(RecordsConfig[r].name, &found);
            if (found) {
              snprintf(cur_value, BUF_SIZE, "%lld", i);
              if (i == ink_atoll(RecordsConfig[r].value)) {
                same = true;
              }
            }
          }
          break;
        case INK_FLOAT:
          {
            float d;
            MgmtFloat f = lmgmt->record_data->readFloat(RecordsConfig[r].name, &found);
            if (found) {
              snprintf(cur_value, BUF_SIZE, "%f", f);
              d = f - atof(RecordsConfig[r].value);
              if ((d > -0.000001f) && (d < 0.000001f)) {
                same = true;
              }
            }
          }
          break;
        case INK_STRING:
          {
            MgmtString s = lmgmt->record_data->readString(RecordsConfig[r].name, &found);
            if (found) {
              if (s) {
                strncpy(cur_value, s, BUF_SIZE);
                xfree(s);
                if ((RecordsConfig[r].value) && (strcmp(RecordsConfig[r].value, cur_value) == 0)
                  ) {
                  same = true;
                }
              } else {
                ink_strncpy(cur_value, NULL_STR, sizeof(cur_value));
                if (RecordsConfig[r].value == 0) {
                  same = true;
                }
              }
            } else {
              ink_strncpy(cur_value, NULL_STR, sizeof(cur_value));
            }
            if (s) {
              xfree(s);
            }
          }
          break;
        case INK_COUNTER:
          {
            MgmtIntCounter ic = lmgmt->record_data->readCounter(RecordsConfig[r].name, &found);
            if (found) {
              snprintf(cur_value, BUF_SIZE, "%lld", ic);
              if (ic == ink_atoll(RecordsConfig[r].value)) {
                same = true;
              }
            }
          }
          break;
        default:
          // Handled here:
          // INVALID, INK_STAT_CONST, INK_STAT_FX, MAX_MGMT_TYPE
          break;
        }

        // default value
        if ((def_value = RecordsConfig[r].value) == NULL) {
          def_value = NULL_STR;
        }
        // safify strings
        def_value_safe = substituteForHTMLChars(def_value);
        cur_value_safe = substituteForHTMLChars(cur_value);

        // current value (print)
        if (same) {
          snprintf(line, sizeof(line), "<td width=\"%d%%\" align=\"center\" bgcolor=\"#EEEEEE\">"
                       "<p align=\"left\">%s</td>\n", cur_value_pcnt, cur_value_safe);
        } else {
          snprintf(line, sizeof(line), "<td width=\"%d%%\" align=\"center\" bgcolor=\"#FFFFFF\">"
                       "<p align=\"left\">%s</td>\n", cur_value_pcnt, cur_value_safe);
        }
        replyMsg->copyFrom(line, strlen(line));

        // default value (print)
        snprintf(line, sizeof(line), "<td width=\"%d%%\" align=\"center\" bgcolor=\"#EEEEEE\">"
                     "<p align=\"left\">%s</td>\n", def_value_pcnt, def_value_safe);
        replyMsg->copyFrom(line, strlen(line));

        snprintf(line, sizeof(line), "</tr>\n");
        replyMsg->copyFrom(line, strlen(line));

        // free mem
        delete[]def_value_safe;
        delete[]cur_value_safe;

      }
    }

    // end table
    snprintf(line, sizeof(line), "</table>\n");
    replyMsg->copyFrom(line, strlen(line));

    // finish generating document
    snprintf(line, sizeof(line), "</body>\n</html>\n");
    replyMsg->copyFrom(line, strlen(line));

  }

  whc->response_hdr->setStatus(STATUS_OK);

  return WEB_HTTP_ERR_OKAY;
}

#undef LINE_SIZE
#undef BUF_SIZE
#undef NULL_STR

static int
handle_record_stats(WebHttpContext * whc, const char *file)
{
  return handle_record_info(whc, true, false);
}

static int
handle_record_configs(WebHttpContext * whc, const char *file)
{
  return handle_record_info(whc, false, false);
}

static int
handle_record_stats_rec(WebHttpContext * whc, const char *file)
{
  return handle_record_info(whc, true, true);
}

static int
handle_record_configs_rec(WebHttpContext * whc, const char *file)
{
  return handle_record_info(whc, false, true);
}

static int
handle_config_files(WebHttpContext * whc, const char *file)
{
  return WebHttpRender(whc, HTML_FILE_ALL_CONFIG);
}

static int
handle_debug_logs(WebHttpContext * whc, const char *file)
{
  return WebHttpRender(whc, HTML_VIEW_DEBUG_LOGS_FILE);
}

//-------------------------------------------------------------------------
// handle_synthetic
//-------------------------------------------------------------------------

static int
handle_synthetic(WebHttpContext * whc, const char *file)
{
  char buffer[28];
  char cur = 'a';
  whc->response_hdr->setContentType(TEXT_PLAIN);
  whc->response_hdr->setStatus(STATUS_OK);
  buffer[26] = '\n';
  buffer[27] = '\0';
  for (int i = 0; i < 26; i++) {
    *(buffer + i) = cur;
    cur++;
  }
  for (int j = 0; j < 60; j++) {
    whc->response_bdy->copyFrom(buffer, 27);
  }
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// handle_submit_alarm
//-------------------------------------------------------------------------

static int
handle_submit_alarm(WebHttpContext * whc, const char *file)
{
  resolveAlarm(whc->post_data_ht);
  whc->top_level_render_file = xstrdup(HTML_ALARM_FILE);
  return handle_ink_extn(whc, HTML_ALARM_FILE);
}

//-------------------------------------------------------------------------
// handle_submit_mgmt_auth
//-------------------------------------------------------------------------

// set_admin_passwd (sub-function)
static inline void
set_admin_passwd(WebHttpContext * whc)
{

  char *admin_orig_epasswd;
  char *admin_old_passwd;
  char *admin_old_epasswd;
  char *admin_new_passwd;
  char *admin_new_passwd_retype;
  char *admin_new_epasswd;

  char empty_str[1];
  *empty_str = '\0';

  if (!ink_hash_table_lookup(whc->post_data_ht, "admin_old_passwd", (void **) &admin_old_passwd))
    admin_old_passwd = NULL;
  if (!ink_hash_table_lookup(whc->post_data_ht, "admin_new_passwd", (void **) &admin_new_passwd))
    admin_new_passwd = NULL;
  if (!ink_hash_table_lookup(whc->post_data_ht, "admin_new_passwd_retype", (void **) &admin_new_passwd_retype))
    admin_new_passwd_retype = NULL;

  if ((admin_old_passwd != NULL) || (admin_new_passwd != NULL) || (admin_new_passwd_retype != NULL)) {

    if (admin_old_passwd == NULL)
      admin_old_passwd = empty_str;
    if (admin_new_passwd == NULL)
      admin_new_passwd = empty_str;
    if (admin_new_passwd_retype == NULL)
      admin_new_passwd_retype = empty_str;

    admin_orig_epasswd = (char *) alloca(INK_ENCRYPT_PASSWD_LEN + 1);
    varStrFromName("proxy.config.admin.admin_password", admin_orig_epasswd, INK_ENCRYPT_PASSWD_LEN + 1);

    // INKqa12084: do not encrypt password if empty_str
    if (strcmp(admin_old_passwd, empty_str) == 0) {
      admin_old_epasswd = xstrdup(empty_str);
    } else {
      INKEncryptPassword(admin_old_passwd, &admin_old_epasswd);
    }

    if (strncmp(admin_old_epasswd, admin_orig_epasswd, INK_ENCRYPT_PASSWD_LEN) == 0) {
      if (strcmp(admin_new_passwd, admin_new_passwd_retype) == 0) {
        // INKqa12084: do not encrypt password if empty_str
        if (strcmp(admin_new_passwd, empty_str) == 0) {
          admin_new_epasswd = xstrdup(empty_str);
        } else {
          INKEncryptPassword(admin_new_passwd, &admin_new_epasswd);
        }

        set_record_value(whc, "proxy.config.admin.admin_password", admin_new_epasswd);
        xfree(admin_new_epasswd);
        whc->request_state |= WEB_HTTP_STATE_SUBMIT_NOTE;
        HtmlRndrText(whc->submit_note, whc->lang_dict_ht, HTML_ID_NEW_ADMIN_PASSWD_SET);
        HtmlRndrBr(whc->submit_note);
      } else {
        ink_hash_table_insert(whc->submit_warn_ht, "proxy.config.admin.admin_password", NULL);
        whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
        HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_NEW_PASSWD_MISTYPE);
        HtmlRndrBr(whc->submit_warn);
      }
    } else {
      ink_hash_table_insert(whc->submit_warn_ht, "proxy.config.admin.admin_password", NULL);
      whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
      HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_OLD_PASSWD_INCORRECT);
      HtmlRndrBr(whc->submit_warn);
    }
    xfree(admin_old_epasswd);
  }

}

static int
handle_submit_mgmt_auth(WebHttpContext * whc, const char *file)
{

  bool recs_out_of_date;
  char *value;
  char *cancel;
  char *record_version;
  char *submit_from_page;
  char *aa_session_id, *aa_user_count;
  char *aa_user, *aa_access, *aa_delete;
  char *aa_new_user, *aa_new_passwd, *aa_new_passwd_retype, *aa_new_access;
  char *aa_new_epasswd;
  char admin_user[MAX_VAL_LENGTH + 1];
  int user, user_count;
  INKCfgContext ctx;
  INKAdminAccessEle *ele;
  INKActionNeedT action_need;
  INKAccessT access_t;
  bool ctx_updated;

  char tmp_a[32];
  char tmp_b[32];

  char empty_str[1];
  *empty_str = '\0';

  // initialize pointers we may assign memeory to
  aa_new_epasswd = NULL;

  // check for submit_from_page
  if (ink_hash_table_lookup(whc->post_data_ht, "submit_from_page", (void **) &submit_from_page)) {
    ink_hash_table_delete(whc->post_data_ht, "submit_from_page");
    whc->top_level_render_file = xstrdup(submit_from_page);
  } else {
    submit_from_page = NULL;
  }

  // check for cancel
  if (ink_hash_table_lookup(whc->post_data_ht, "cancel", (void **) &cancel))
    goto Ldone;

  // check for record_version
  recs_out_of_date = true;
  if (ink_hash_table_lookup(whc->post_data_ht, "record_version", (void **) &record_version)) {
    recs_out_of_date = !record_version_valid(record_version);
    ink_hash_table_delete(whc->post_data_ht, "record_version");
    xfree(record_version);
  }
  if (recs_out_of_date)
    goto Lout_of_date;

  // proxy.config.admin.basic_auth
  if (ink_hash_table_lookup(whc->post_data_ht, "proxy.config.admin.basic_auth", (void **) &value))
    set_record_value(whc, "proxy.config.admin.basic_auth", value);

  // proxy.config.admin.admin_user
  if (ink_hash_table_lookup(whc->post_data_ht, "proxy.config.admin.admin_user", (void **) &value))
    set_record_value(whc, "proxy.config.admin.admin_user", value);

  // proxy.config.admin.admin_password (call sub-function)
  set_admin_passwd(whc);

  // grab our session_id and user_count
  if (ink_hash_table_lookup(whc->post_data_ht, "session_id", (void **) &aa_session_id)) {
    if (!ink_hash_table_lookup(whc->post_data_ht, "user_count", (void **) &aa_user_count))
      goto Lunable_to_submit;
    // find our current session
    if (WebHttpSessionRetrieve(aa_session_id, (void **) &ctx) != WEB_HTTP_ERR_OKAY)
      goto Lout_of_date;
    // get new additional-user information
    if (!ink_hash_table_lookup(whc->post_data_ht, "new_user", (void **) &aa_new_user))
      aa_new_user = NULL;
    if (!ink_hash_table_lookup(whc->post_data_ht, "new_passwd", (void **) &aa_new_passwd))
      aa_new_passwd = NULL;
    if (!ink_hash_table_lookup(whc->post_data_ht, "new_passwd_retype", (void **) &aa_new_passwd_retype))
      aa_new_passwd_retype = NULL;
    if (!ink_hash_table_lookup(whc->post_data_ht, "new_access", (void **) &aa_new_access))
      aa_new_access = NULL;
    // check if the user is trying to add a new additional-user
    if (aa_new_user != NULL) {
      // kwt 12.March.2001 check for username length
      if (strlen(aa_new_user) > WEB_HTTP_AUTH_USER_MAX) {
        ink_hash_table_insert(whc->submit_warn_ht, "additional_administrative_accounts", NULL);
        ink_hash_table_insert(whc->submit_warn_ht, "add_new_administrative_user", NULL);
        whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
        HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_NEW_USERNAME_LENGTH);
        HtmlRndrBr(whc->submit_warn);
        aa_new_user = NULL;
      }
      // kwt
      if (aa_new_user != NULL) {
        if (aa_new_passwd == NULL)
          aa_new_passwd = empty_str;
        if (aa_new_passwd_retype == NULL)
          aa_new_passwd_retype = empty_str;
        if (strcmp(aa_new_passwd, aa_new_passwd_retype) == 0) {
          // allocating memory on aa_new_epasswd
          INKEncryptPassword(aa_new_passwd, &aa_new_epasswd);
        } else {
          ink_hash_table_insert(whc->submit_warn_ht, "additional_administrative_accounts", NULL);
          ink_hash_table_insert(whc->submit_warn_ht, "add_new_administrative_user", NULL);
          whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
          HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_NEW_PASSWD_MISTYPE);
          HtmlRndrBr(whc->submit_warn);
        }
      }
      // check if the new_user is the same as the proxy.config.admin.admin_user
      if (aa_new_user != NULL) {
        varStrFromName("proxy.config.admin.admin_user", admin_user, MAX_VAL_LENGTH + 1);
        if (strcmp(aa_new_user, admin_user) == 0) {
          ink_hash_table_insert(whc->submit_warn_ht, "additional_administrative_accounts", NULL);
          ink_hash_table_insert(whc->submit_warn_ht, "add_new_administrative_user", NULL);
          whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
          HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_NEW_USER_DUPLICATE);
          HtmlRndrBr(whc->submit_warn);
          aa_new_user = NULL;
        }
      }
    }
    // Walk through members and update settings in ctx backwards.
    // Client submitted values should be in the same order as the ctx
    // since we originally created this page from the same ctx.
    // Looping backwards helps so that we can delete elements by
    // index.
    ctx_updated = false;
    user_count = ink_atoi(aa_user_count);
    for (user = user_count - 1; user >= 0; user--) {
      snprintf(tmp_a, sizeof(tmp_a), "user:%d", user);
      snprintf(tmp_b, sizeof(tmp_b), "access:%d", user);
      if (ink_hash_table_lookup(whc->post_data_ht, tmp_a, (void **) &aa_user) &&
          ink_hash_table_lookup(whc->post_data_ht, tmp_b, (void **) &aa_access)) {
        snprintf(tmp_a, sizeof(tmp_a), "delete:%d", user);
        if (ink_hash_table_lookup(whc->post_data_ht, tmp_a, (void **) &aa_delete)) {
          INKCfgContextRemoveEleAt(ctx, user);
          ctx_updated = true;
          continue;
        }
        ele = (INKAdminAccessEle *) INKCfgContextGetEleAt(ctx, user);
        if (strcmp(ele->user, aa_user) != 0) {
          goto Lunable_to_submit;
        }
        if (aa_new_user && (strcmp(aa_new_user, aa_user) == 0)) {
          ink_hash_table_insert(whc->submit_warn_ht, "additional_administrative_accounts", NULL);
          ink_hash_table_insert(whc->submit_warn_ht, "add_new_administrative_user", NULL);
          whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
          HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_NEW_USER_DUPLICATE);
          HtmlRndrBr(whc->submit_warn);
          aa_new_user = NULL;
        }
        access_t = (INKAccessT) ink_atoi(aa_access);
        if (ele->access != access_t) {
          ele->access = access_t;
          ctx_updated = true;
        }
      } else {
        goto Lunable_to_submit;
      }
    }
    // add new user
    if ((aa_new_user != NULL) && (aa_new_epasswd != NULL)) {
      ele = INKAdminAccessEleCreate();
      ele->user = xstrdup(aa_new_user);
      ele->password = xstrdup(aa_new_epasswd);
      // FIXME: no access for now, add back later?
      //ele->access = aa_new_access ? (INKAccessT)ink_atoi(aa_new_access) : INK_ACCESS_NONE;
      ele->access = INK_ACCESS_NONE;
      INKCfgContextAppendEle(ctx, (INKCfgEle *) ele);
      ctx_updated = true;
    }
    if (ctx_updated) {
      if (INKCfgContextCommit(ctx, &action_need, NULL) != INK_ERR_OKAY) {
        WebHttpSessionDelete(aa_session_id);
        goto Lout_of_date;
      }
      INKActionDo(action_need);
    }
    WebHttpSessionDelete(aa_session_id);
  } else {
    goto Lunable_to_submit;
  }
  goto Ldone;

Lout_of_date:
  whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
  HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_OUT_OF_DATE);
  HtmlRndrBr(whc->submit_warn);
  goto Ldone;

Lunable_to_submit:
  whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
  HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_UNABLE_TO_SUBMIT);
  HtmlRndrBr(whc->submit_warn);
  goto Ldone;

Ldone:
  if (aa_new_epasswd) {
    xfree(aa_new_epasswd);
  }
  return WebHttpRender(whc, HTML_MGMT_LOGIN_FILE);
}






//-------------------------------------------------------------------------
// handle_submit_snapshot
//-------------------------------------------------------------------------

// Doesn't seem to be used.
static int INK_UNUSED
handle_submit_snapshot(WebHttpContext * whc, const char *file)
{
  int err = 0;
  SnapResult snap_result = SNAP_OK;
  char *submit_from_page;
  char *snap_action;
  char *snap_name;
  char *snap_location;
  char *snap_directory;
  char *ftp_server_name;
  char *ftp_remote_dir;
  char *ftp_login;
  char *ftp_password;
  char *snapDirFromRecordsConf;
  bool found = false;
  int ret_val;
  struct stat snapDirStat;
  char config_dir[PATH_NAME_MAX];
  struct stat s;

  if (varStrFromName("proxy.config.config_dir", config_dir, PATH_NAME_MAX) == false)
    mgmt_fatal(stderr,
               "[WebHttp::handle_submit_snapshot] Unable to find configuration directory from proxy.config.config_dir\n");

  if ((err = stat(config_dir, &s)) < 0) {
    ink_strncpy(config_dir, system_config_directory,sizeof(config_dir));
    if ((err = stat(config_dir, &s)) < 0) {
        mgmt_elog("[WebHttp::handle_submit_snapshot] unable to stat() directory '%s': %d %d, %s\n",
                config_dir, err, errno, strerror(errno));
        mgmt_fatal("[WebHttp::handle_submit_snapshot] please set config path via command line '-path <path>' or 'proxy.config.config_dir' \n");
    }
  }
  // check for submit_from_page
  if (ink_hash_table_lookup(whc->post_data_ht, "submit_from_page", (void **) &submit_from_page)) {
    ink_hash_table_delete(whc->post_data_ht, "submit_from_page");
    whc->top_level_render_file = xstrdup(submit_from_page);
  } else {
    submit_from_page = NULL;
  }

#ifndef _WIN32

  // FIXME: button names here are going to be hard to
  // internationalize.  we should put the button names into the
  // dictionary.

  // handle snapshot
  if (ink_hash_table_lookup(whc->post_data_ht, "snap_action", (void **) &snap_action)) {
    // take the snapshot action
    if (strcmp(snap_action, "  Change  ") == 0) {
      if (ink_hash_table_lookup(whc->post_data_ht, "Change Directory", (void **) &snap_directory)) {
        if (snap_directory == NULL) {
          mgmt_log(stderr, "Change Directory not specified.");
        } else {
          ink_assert(RecGetRecordString_Xmalloc("proxy.config.snapshot_dir", &snapDirFromRecordsConf)
                     == REC_ERR_OKAY);
          if (snapDirFromRecordsConf == NULL) {
            // coverity[size_is_strlen]
            snapDirFromRecordsConf = new char[strlen("snapshots")];
            snprintf(snapDirFromRecordsConf, strlen("snapshots"), "%s", "snapshots");
            ink_assert(RecGetRecordString_Xmalloc("proxy.config.snapshot_dir", &snapDirFromRecordsConf)
                       == REC_ERR_OKAY);
            ink_assert(RecGetRecordString_Xmalloc("proxy.config.snapshot_dir", &snapDirFromRecordsConf)
                       == REC_ERR_OKAY);
            RecSetRecordString("proxy.config.snapshot_dir", (RecString) snapDirFromRecordsConf);
          }
          if (strcasecmp(snapDirFromRecordsConf, snap_directory)) {
            RecSetRecordString("proxy.config.snapshot_dir", snap_directory);
            // Create a directory for the snap shot
            if (snap_directory[0] != '/') {
              // coverity[alloc_fn][var_assign]
              char *snap_dir_cpy = strdup(snap_directory);
              int newLen;

              // coverity[noescape]
              newLen = strlen(snap_dir_cpy) + strlen(config_dir) + 2;
              snap_directory = new char[newLen];
              ink_assert(snap_directory != NULL);
              // coverity[noescape]
              snprintf(snap_directory, newLen, "%s%s%s", config_dir, DIR_SEP, snap_dir_cpy);
              //snap_directory = newPathString(config_dir, snap_dir_cpy);
              RecSetRecordString("proxy.config.snapshot_dir", snap_dir_cpy);
              if (snap_dir_cpy)
                free(snap_dir_cpy);
            }

            if (stat(snap_directory, &snapDirStat)) {
              SimpleTokenizer snapDirPathTok(snap_directory, '/');
              int dirDepth = snapDirPathTok.getNumTokensRemaining();

              for (int i = 1; i <= dirDepth; i++) {
                if (snap_directory[0] == '/') {
                  int newLen;
                  char *absoluteDir;
                  const char *tok = snapDirPathTok.getNext(i);

                  newLen = strlen(tok) + 2;
                  absoluteDir = new char[newLen];
                  ink_assert(absoluteDir != NULL);
                  snprintf(absoluteDir, newLen, "/%s", tok);

                  if ((ret_val = mkdir(absoluteDir, DIR_MODE)) < 0) {
                    perror("Absolute snapPath Directory creation:");
                  }
                  delete[]absoluteDir;
                } else {
                  const char *tok = snapDirPathTok.getNext(i);
                  // These weren't used, so moved. /leif
                  //int newLen;
                  //char* absoluteDir;
                  //const char *config_dir = lmgmt->record_data ->readString("proxy.config.config_dir", &found);

                  //newLen = strlen(tok) + strlen(config_dir) + 2;
                  //absoluteDir = new char[newLen];
                  //ink_assert(absoluteDir != NULL);
                  //sprintf(absoluteDir, "%s%s%s",config_dir, DIR_SEP, tok);

                  //if ((ret_val = mkdir(absoluteDir, DIR_MODE)) < 0) {
                  //perror("Absolute snapPath Directory creation:");
                  //}
                  //delete [] absoluteDir;
                  if ((ret_val = mkdir(tok, DIR_MODE)) < 0) {
                    perror("Relative snapPath Directory creation:");
                  }
                }
                snapDirPathTok.setString(snap_directory);
              }
            }                   //else {
            //if(snap_directory[0] == '/') {
            //lmgmt->record_data ->setString("proxy.config.snapshot_dir", snap_directory);
            //} else {
            //int newLen;
            //char* relativeDir;
            //const char *config_dir = lmgmt->record_data ->readString("proxy.config.config_dir", &found);

            //newLen = strlen(snap_directory) + strlen(config_dir) + 2;
            //relativeDir = new char[newLen];
            //ink_assert(relativeDir != NULL);
            //sprintf(relativeDir, "%s%s%s",config_dir, DIR_SEP, snap_directory);
            //lmgmt->record_data ->setString("proxy.config.snapshot_dir", relativeDir);
            //}
            //}
          }
        }
      }
    } else if (strcmp(snap_action, "   Take   ") == 0) {
      if (ink_hash_table_lookup(whc->post_data_ht, "new_snap", (void **) &snap_name)) {
        if (snap_name == NULL) {
          mgmt_log(stderr, "Snapshots name on disk not specified.");
        }
      }
      if (ink_hash_table_lookup(whc->post_data_ht, "Snapshots Location", (void **) &snap_location)) {
        // coverity[var_compare_op]
        if (snap_location == NULL) {
          mgmt_log(stderr, "Snapshots Location not specified.");
        }
      }
      if (snap_location && strcmp(snap_location, "OnDisk") == 0) {
        RecGetRecordString_Xmalloc("proxy.config.snapshot_dir", &snap_directory);

        if (snap_directory[0] != '/') {
          // coverity[alloc_fn][var_assign]
          char *snap_dir_cpy = strdup(snap_directory);
          int newLen;

          // coverity[noescape]
          newLen = strlen(snap_dir_cpy) + strlen(config_dir) + 2;
          snap_directory = new char[newLen];
          ink_assert(snap_directory != NULL);
          // coverity[noescape]
          snprintf(snap_directory, newLen, "%s%s%s", config_dir, DIR_SEP, snap_dir_cpy);
          if (snap_dir_cpy)
            free(snap_dir_cpy);
        }
        snap_result = configFiles->takeSnap(snap_name, snap_directory);
      } else if (!(strcmp(snap_location, "FTPServerUpload")) || !(strcmp(snap_location, "FTPServerDownload"))) {
        if (ink_hash_table_lookup(whc->post_data_ht, "FTPServerName", (void **) &ftp_server_name)) {
          if (ftp_server_name == NULL) {
            mgmt_log(stderr, "FTPServerName not specified.");
          }
        }
        if (ink_hash_table_lookup(whc->post_data_ht, "FTPRemoteDir", (void **) &ftp_remote_dir)) {
          if (ftp_server_name == NULL) {
            mgmt_log(stderr, "FTPRemoteDir not specified.");
          }
        }
        if (ink_hash_table_lookup(whc->post_data_ht, "FTPLogin", (void **) &ftp_login)) {
          if (ftp_login == NULL) {
            mgmt_log(stderr, "FTPLogin not specified.");
          }
        }
        if (ink_hash_table_lookup(whc->post_data_ht, "FTPPassword", (void **) &ftp_password)) {
          if (ftp_password == NULL) {
            mgmt_log(stderr, "FTPPassword not specified.");
          }
        }
        if (!(strcmp(snap_location, "FTPServerUpload")) && snap_name) {
          int localDirLength, remoteDirLength;
          char *newStr;
          char *ftp_remote_dir_name;

          localDirLength = strlen(snap_name) + strlen("/tmp") + 2;
          remoteDirLength = strlen(snap_name) + strlen(ftp_remote_dir) + 2;

          newStr = new char[localDirLength];
          ink_assert(newStr != NULL);
          ftp_remote_dir_name = new char[remoteDirLength];
          ink_assert(ftp_remote_dir_name != NULL);
          snprintf(newStr, localDirLength, "/tmp%s%s", DIR_SEP, snap_name);
          snprintf(ftp_remote_dir_name, remoteDirLength, "%s%s%s", ftp_remote_dir, DIR_SEP, snap_name);
          snap_result = configFiles->takeSnap(snap_name, "/tmp");
          INKMgmtFtp("put", ftp_server_name, ftp_login, ftp_password, newStr, ftp_remote_dir_name, NULL);
        } else {
          RecGetRecordString_Xmalloc("proxy.config.snapshot_dir", &snapDirFromRecordsConf);
          ink_assert(found);

          if (snapDirFromRecordsConf[0] != '/') {
            char *snap_dir_cpy = strdup(snapDirFromRecordsConf);
            int newLen;

            newLen = strlen(snap_dir_cpy) + strlen(config_dir) + strlen(snap_name) + 3;
            snapDirFromRecordsConf = new char[newLen];
            ink_assert(snapDirFromRecordsConf != NULL);
            snprintf(snapDirFromRecordsConf, newLen, "%s%s%s%s%s", config_dir, DIR_SEP, snap_dir_cpy, DIR_SEP,
                         snap_name);
            if (snap_dir_cpy)
              free(snap_dir_cpy);
          } else {

            char *snap_dir_cpy = strdup(snapDirFromRecordsConf);
            int newLen;

            newLen = strlen(snap_dir_cpy) + strlen(config_dir) + strlen(snap_name) + 3;
            snapDirFromRecordsConf = new char[newLen];
            ink_assert(snapDirFromRecordsConf != NULL);
            snprintf(snapDirFromRecordsConf, newLen, "%s%s%s", snap_dir_cpy, DIR_SEP, snap_name);
            if (snap_dir_cpy)
              free(snap_dir_cpy);
          }
          int newLen;
          char *newStr;
          //const char *config_dir = lmgmt->record_data ->readString("proxy.config.config_dir", &found);

          newLen = strlen(snap_name) + strlen(ftp_remote_dir) + 2;
          newStr = new char[newLen];
          ink_assert(newStr != NULL);
          snprintf(newStr, newLen, "%s%s%s", ftp_remote_dir, DIR_SEP, snap_name);

          if ((ret_val = mkdir(snapDirFromRecordsConf, DIR_MODE)) < 0) {
            mgmt_log(stderr, "Cannot create %s\n", snapDirFromRecordsConf);
          }
          INKMgmtFtp("get", ftp_server_name, ftp_login, ftp_password, snapDirFromRecordsConf, newStr, NULL);

        }
      } else if (!(strcmp(snap_location, "FloppySave")) || !(strcmp(snap_location, "FloppyCopy"))) {
        char *floppyMountPoint;
        if (ink_hash_table_lookup(whc->post_data_ht, "FloppyDrive", (void **) &floppyMountPoint)) {
          //coverity [var_compare_op]
          if (floppyMountPoint == NULL)
            mgmt_log(stderr, "FloppyMountPoint not found.");
        }
        if (snap_location && strcmp(snap_location, "FloppySave") == 0) {
          snap_result = configFiles->takeSnap(snap_name, floppyMountPoint);
        } else {
          char args[256];
          RecGetRecordString_Xmalloc("proxy.config.snapshot_dir", &snapDirFromRecordsConf);
          snprintf(args, sizeof(args), "cp -fr %s/%s %s", floppyMountPoint, snap_name, snapDirFromRecordsConf);
          const char *argv[] = { args,
            NULL
          };
          processSpawn(argv, NULL, NULL, NULL, false, false);
        }
      } else {
        mgmt_log(stderr, "Illegal value for snapshot location.");
      }
      // take a snapshot for the current configuration files
    } else if (strcmp(snap_action, " Restore ") == 0) {
      // restore the selected snapshot
      if (ink_hash_table_lookup(whc->post_data_ht, "snap_name", (void **) &snap_name)) {
        if (strcmp(snap_name, "- select a snapshot -")) {
          RecGetRecordString_Xmalloc("proxy.config.snapshot_dir", &snapDirFromRecordsConf);
          ink_assert(found);
          if (snapDirFromRecordsConf[0] != '/') {
            char *snap_dir_cpy = strdup(snapDirFromRecordsConf);
            ink_assert(snap_dir_cpy);
            int newLen;

            newLen = strlen(snap_dir_cpy) + strlen(config_dir) + 2;
            snapDirFromRecordsConf = new char[newLen];
            ink_assert(snapDirFromRecordsConf != NULL);
            snprintf(snapDirFromRecordsConf, newLen, "%s%s%s", config_dir, DIR_SEP, snap_dir_cpy);
            if (snap_dir_cpy)
              free(snap_dir_cpy);
          }
          snap_result = configFiles->restoreSnap(snap_name, snapDirFromRecordsConf);
        }
      }
    } else if (strcmp(snap_action, "  Delete  ") == 0) {
      // delete the selected snapshot
      if (ink_hash_table_lookup(whc->post_data_ht, "snap_name", (void **) &snap_name)) {
        if (strcmp(snap_name, "- select a snapshot -")) {
          RecGetRecordString_Xmalloc("proxy.config.snapshot_dir", &snapDirFromRecordsConf);
          ink_assert(found);
          if (snapDirFromRecordsConf[0] != '/') {
            char *snap_dir_cpy = strdup(snapDirFromRecordsConf);
            ink_assert(snap_dir_cpy);
            int newLen;

            newLen = strlen(snap_dir_cpy) + strlen(config_dir) + 2;
            snapDirFromRecordsConf = new char[newLen];
            ink_assert(snapDirFromRecordsConf != NULL);
            snprintf(snapDirFromRecordsConf, newLen, "%s%s%s", config_dir, DIR_SEP, snap_dir_cpy);
            if (snap_dir_cpy)
              free(snap_dir_cpy);
          }
          snap_result = configFiles->removeSnap(snap_name, snapDirFromRecordsConf);
        }
      }
    } else {
      // show alarm error
      mgmt_log(stderr, "Unknown action is specified.");
    }
  } else {
    snap_action = NULL;
  }

#endif

  if (snap_result != SNAP_OK) {
    // FIXME: show alarm error for snapshot!
  }

  if (submit_from_page) {
    err = WebHttpRender(whc, submit_from_page);
    xfree(submit_from_page);
  } else {
    err = WebHttpRender(whc, HTML_DEFAULT_CONFIGURE_FILE);
  }
  return err;
}


//-------------------------------------------------------------------------
// handle_submit_snapshot_to_filesystem
//-------------------------------------------------------------------------

static int
handle_submit_snapshot_to_filesystem(WebHttpContext * whc, const char *file)
{
  int err = 0;
  SnapResult snap_result = SNAP_OK;
  char *submit_from_page;
  char *snap_action;
  char *snap_name;
  char *snap_directory;
  char *snapDirFromRecordsConf;
  char *cancel;
  bool found = false;
  int ret_val;
  struct stat snapDirStat;
  char config_dir[256];
  bool recs_out_of_date;
  char *record_version;
  ExpandingArray snap_list(25, true);
  int num_snaps;

  if (varStrFromName("proxy.config.config_dir", config_dir, 256) == false)
    mgmt_fatal(stderr,
               "[WebHttp::handle_submit_snapshot] Unable to find configuration directory from proxy.config.config_dir\n");

  // check for submit_from_page
  if (ink_hash_table_lookup(whc->post_data_ht, "submit_from_page", (void **) &submit_from_page)) {
    ink_hash_table_delete(whc->post_data_ht, "submit_from_page");
    whc->top_level_render_file = xstrdup(submit_from_page);
  } else {
    submit_from_page = NULL;
  }

  // check for cancel
  if (ink_hash_table_lookup(whc->post_data_ht, "cancel", (void **) &cancel)) {
    whc->post_data_ht = NULL;
    goto Ldone;
  }
  // check for record_version
  recs_out_of_date = true;
  if (ink_hash_table_lookup(whc->post_data_ht, "record_version", (void **) &record_version)) {
    recs_out_of_date = !record_version_valid(record_version);
    ink_hash_table_delete(whc->post_data_ht, "record_version");
    xfree(record_version);
  }

  if (ink_hash_table_lookup(whc->post_data_ht, "Change Directory", (void **) &snap_directory)) {
    if (snap_directory == NULL) {
      mgmt_log(stderr, "[WebHttp::handle_submit_snapshot_to_filesystem] Change Directory not specified.");
      ink_hash_table_insert(whc->submit_warn_ht, "proxy.config.snapshot_dir", snap_directory);
      if (!(whc->request_state & WEB_HTTP_STATE_SUBMIT_WARN)) {
        HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_MISSING_ENTRY);
        HtmlRndrBr(whc->submit_warn);
      }
      whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
      goto Ldone;
    } else {
#ifndef _WIN32
      const char *valid_chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890._-/\\";
#else
      const char *valid_chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890._-/\\ ";
#endif
      int snapnameLength = strlen(snap_directory);
      int validnameLength = strspn(snap_directory, valid_chars);
      if (snapnameLength != validnameLength) {
        ink_hash_table_insert(whc->submit_warn_ht, "proxy.config.snapshot_dir", snap_directory);
        if (!(whc->request_state & WEB_HTTP_STATE_SUBMIT_WARN)) {
          HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_INVALID_ENTRY);
          HtmlRndrBr(whc->submit_warn);
        }
        whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
        goto Ldone;
      }

      RecGetRecordString_Xmalloc("proxy.config.snapshot_dir", &snapDirFromRecordsConf);
      ink_assert(found);
      if (snapDirFromRecordsConf == NULL) {
        snapDirFromRecordsConf = new char[strlen("snapshots")];
        snprintf(snapDirFromRecordsConf, strlen("snapshots"), "%s", "snapshots");
        RecSetRecordString("proxy.config.snapshot_dir", snapDirFromRecordsConf);
      }
      if (strcasecmp(snapDirFromRecordsConf, snap_directory)) {
        RecSetRecordString("proxy.config.snapshot_dir", snapDirFromRecordsConf);
        // Create a directory for the snap shot
        if (snap_directory[0] != '/') {
          char *snap_dir_cpy = strdup(snap_directory);
          int newLen;

          newLen = strlen(snap_dir_cpy) + strlen(config_dir) + 2;
          snap_directory = new char[newLen];
          ink_assert(snap_directory != NULL);
          snprintf(snap_directory, newLen, "%s%s%s", config_dir, DIR_SEP, snap_dir_cpy);
          RecSetRecordString("proxy.config.snapshot_dir", snap_dir_cpy);
          if (snap_dir_cpy)
            free(snap_dir_cpy);
        }
        if (!stat(snap_directory, &snapDirStat)) {
          bool write_possible = true;
          bool read_possible = true;
#ifndef _WIN32
          if (snapDirStat.st_uid != getuid()) {
            if (snapDirStat.st_gid != getgid()) {
              if (!(snapDirStat.st_mode & 00002)) {
                write_possible = false;
              } else {
                write_possible = true;
              }
            } else {
              if (!(snapDirStat.st_mode & 00020)) {
                write_possible = false;
              } else {
                write_possible = true;
              }
            }
          }

          if (snapDirStat.st_uid != getuid()) {
            if (snapDirStat.st_gid != getgid()) {
              if (!(snapDirStat.st_mode & 00004)) {
                read_possible = false;
              } else {
                read_possible = true;
              }
            } else {
              if (!(snapDirStat.st_mode & 00040)) {
                read_possible = false;
              } else {
                read_possible = true;
              }
            }
          }
#else
          DWORD attr;
          attr = GetFileAttributes(snap_directory);
          if ((attr & FILE_ATTRIBUTE_READONLY) != 0) {  // read only dir
            write_possible = false;
            read_possible = false;
          } else {
            write_possible = true;
            read_possible = true;
          }
#endif // _WIN32
          if (!write_possible) {
            if (!read_possible) {
              ink_hash_table_insert(whc->submit_warn_ht, "proxy.config.snapshot_dir", snap_directory);
              if (!(whc->request_state & WEB_HTTP_STATE_SUBMIT_WARN)) {
                HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_PERMISSION_DENIED);
                HtmlRndrBr(whc->submit_warn);
              }
              whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
              RecSetRecordString("proxy.config.snapshot_dir", snap_directory);
              goto Ldone;
            }
          }
        }

        if (stat(snap_directory, &snapDirStat)) {
          SimpleTokenizer snapDirPathTok(snap_directory, '/');
          int dirDepth = snapDirPathTok.getNumTokensRemaining();

          for (int i = 1; i <= dirDepth; i++) {
            if (snap_directory[0] == '/') {
              int newLen;
              char *absoluteDir;
              const char *tok = snapDirPathTok.getNext(i);

              newLen = strlen(tok) + 2;
              absoluteDir = new char[newLen];
              ink_assert(absoluteDir != NULL);
              snprintf(absoluteDir, newLen, "/%s", tok);

#ifndef _WIN32
              if ((ret_val = mkdir(absoluteDir, DIR_MODE)) < 0) {
#else
              if ((ret_val = mkdir(absoluteDir)) < 0) {
#endif
                perror("[WebHttp::handle_submit_snapshot_to_filesystem] Absolute snapPath Directory creation:");
              }
              delete[]absoluteDir;
            } else {
              const char *tok = snapDirPathTok.getNext(i);
#ifndef _WIN32
              if ((ret_val = mkdir(tok, DIR_MODE)) < 0) {
#else
              if ((ret_val = mkdir(tok)) < 0) {
#endif
                perror("[WebHttp::handle_submit_snapshot_to_filesystem] Relative snapPath Directory creation:");
              }
            }
            snapDirPathTok.setString(snap_directory);
          }
        }
      }
    }
  }

  if (ink_hash_table_lookup(whc->post_data_ht, "SnapshotName", (void **) &snap_name)) {
    if (snap_name != NULL) {
#ifndef _WIN32
      const char *valid_chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890._";
#else
      const char *valid_chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890._ ";
#endif
      int snapnameLength = strlen(snap_name);
      int validnameLength = strspn(snap_name, valid_chars);
      if (snapnameLength != validnameLength) {
        ink_hash_table_insert(whc->submit_warn_ht, "SnapShotName", NULL);
        if (!(whc->request_state & WEB_HTTP_STATE_SUBMIT_WARN)) {
          HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_INVALID_ENTRY);
          HtmlRndrBr(whc->submit_warn);
        }
        whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
        goto Ldone;
      }


      snap_result = configFiles->WalkSnaps(&snap_list);
      if (snap_result == SNAP_OK) {
        num_snaps = snap_list.getNumEntries();
        if (num_snaps > 0) {
          for (int i = 0; i < num_snaps; i++)
            if (!strcmp((char *) snap_list[i], snap_name)) {
              if (ink_hash_table_lookup(whc->post_data_ht, "Delete Snapshot", (void **) &snap_action)) {
                if (ink_hash_table_lookup(whc->post_data_ht, "restore_delete_name", (void **) &snap_action)) {
                  if (snap_action != NULL) {
                    if (!strcmp(snap_name, snap_action))
                      goto Ldelete;
                  }
                }
              }
              ink_hash_table_insert(whc->submit_warn_ht, "SnapShotName", NULL);
              if (!(whc->request_state & WEB_HTTP_STATE_SUBMIT_WARN)) {
                HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_DUPLICATE_ENTRY);
                HtmlRndrBr(whc->submit_warn);
              }
              whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
              goto Ldone;
            }
        }
        ink_hash_table_delete(whc->post_data_ht, "SnapshotName");
      }

      RecGetRecordString_Xmalloc("proxy.config.snapshot_dir", &snap_directory);
      ink_assert(found);

      if (snap_directory[0] != '/') {
        char *snap_dir_cpy = strdup(snap_directory);
        int newLen;

        newLen = strlen(snap_dir_cpy) + strlen(config_dir) + 2;
        snap_directory = new char[newLen];
        ink_assert(snap_directory != NULL);
        snprintf(snap_directory, newLen, "%s%s%s", config_dir, DIR_SEP, snap_dir_cpy);
        if (snap_dir_cpy)
          free(snap_dir_cpy);
      }
      snap_result = configFiles->takeSnap(snap_name, snap_directory);
      if (snap_result == 3) {
        ink_hash_table_insert(whc->submit_warn_ht, "proxy.config.snapshot_dir", snap_directory);
        if (!(whc->request_state & WEB_HTTP_STATE_SUBMIT_WARN)) {
          HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_PERMISSION_DENIED);
          HtmlRndrBr(whc->submit_warn);
        }
        whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
        RecSetRecordString("proxy.config.snapshot_dir", snap_directory);
        goto Ldone;
      }
    }
  }

  if (ink_hash_table_lookup(whc->post_data_ht, "Restore Snapshot", (void **) &snap_action)) {
    if (ink_hash_table_lookup(whc->post_data_ht, "restore_delete_name", (void **) &snap_name)) {
      if (strcmp(snap_name, "- select a snapshot -")) {
        RecGetRecordString_Xmalloc("proxy.config.snapshot_dir", &snapDirFromRecordsConf);
        ink_assert(found);
        if (snapDirFromRecordsConf[0] != '/') {
          char *snap_dir_cpy = strdup(snapDirFromRecordsConf);
          ink_assert(snap_dir_cpy);
          int newLen;
          newLen = strlen(snap_dir_cpy) + strlen(config_dir) + 2;
          snapDirFromRecordsConf = new char[newLen];
          ink_assert(snapDirFromRecordsConf != NULL);
          snprintf(snapDirFromRecordsConf, newLen, "%s%s%s", config_dir, DIR_SEP, snap_dir_cpy);
          if (snap_dir_cpy)
            free(snap_dir_cpy);
        }
        snap_result = configFiles->restoreSnap(snap_name, snapDirFromRecordsConf);
        if (snap_result < 0) {
          mgmt_log(stderr, "[WebHttp::handle_submit_snapshot_to_filesystem] Restore snapshot failed");
        }
      }
    }
  }

Ldelete:
  if (ink_hash_table_lookup(whc->post_data_ht, "Delete Snapshot", (void **) &snap_action)) {
    if (ink_hash_table_lookup(whc->post_data_ht, "restore_delete_name", (void **) &snap_name)) {
      if (strcmp(snap_name, "- select a snapshot -")) {
        RecGetRecordString_Xmalloc("proxy.config.snapshot_dir", &snapDirFromRecordsConf);
        ink_assert(found);
        if (snapDirFromRecordsConf[0] != '/') {
          char *snap_dir_cpy = strdup(snapDirFromRecordsConf);
          ink_assert(snap_dir_cpy);
          int newLen;
          newLen = strlen(snap_dir_cpy) + strlen(config_dir) + 2;
          snapDirFromRecordsConf = new char[newLen];
          ink_assert(snapDirFromRecordsConf != NULL);
          snprintf(snapDirFromRecordsConf, newLen, "%s%s%s", config_dir, DIR_SEP, snap_dir_cpy);
          if (snap_dir_cpy)
            free(snap_dir_cpy);
        }
        snap_result = configFiles->removeSnap(snap_name, snapDirFromRecordsConf);
        if (snap_result < 0) {
          mgmt_log(stderr, "[WebHttp::handle_submit_snapshot_to_filesystem] Remove snapshot failed");
        }
      }
    }
  }



Ldone:
  if (submit_from_page) {
    err = WebHttpRender(whc, submit_from_page);
    xfree(submit_from_page);
  } else {
    err = WebHttpRender(whc, HTML_DEFAULT_CONFIGURE_FILE);
  }
  return err;
}







//-------------------------------------------------------------------------
// handle_submit_snapshot_to_ftpserver
//-------------------------------------------------------------------------

static int
handle_submit_snapshot_to_ftpserver(WebHttpContext * whc, const char *file)
{
  int err = 0;
  SnapResult snap_result = SNAP_OK;
  char *submit_from_page;
  char *snap_name;
  char *ftp_server_name;
  char *ftp_remote_dir;
  char *ftp_login;
  char *ftp_password;
  char *snapDirFromRecordsConf;
  char *tempDirFromRecordsConf;
  char *cancel;
  bool found;
  int ret_val;
  // Doesn't seem to be used.
  //struct stat snapDirStat;
  char config_dir[256];
  bool recs_out_of_date;
  char *record_version;
  ExpandingArray snap_list(25, true);

  if (varStrFromName("proxy.config.config_dir", config_dir, 256) == false)
    mgmt_fatal(stderr,
               "[WebHttp::handle_submit_snapshot] Unable to find configuration directory from proxy.config.config_dir\n");

  // check for submit_from_page
  if (ink_hash_table_lookup(whc->post_data_ht, "submit_from_page", (void **) &submit_from_page)) {
    ink_hash_table_delete(whc->post_data_ht, "submit_from_page");
    whc->top_level_render_file = xstrdup(submit_from_page);
  } else {
    submit_from_page = NULL;
  }

  // check for cancel
  if (ink_hash_table_lookup(whc->post_data_ht, "cancel", (void **) &cancel)) {
    whc->post_data_ht = NULL;
    goto Ldone;
  }
  // check for record_version
  recs_out_of_date = true;
  if (ink_hash_table_lookup(whc->post_data_ht, "record_version", (void **) &record_version)) {
    recs_out_of_date = !record_version_valid(record_version);
    ink_hash_table_delete(whc->post_data_ht, "record_version");
    xfree(record_version);
  }
#ifndef _WIN32

  if (ink_hash_table_lookup(whc->post_data_ht, "FTPServerName", (void **) &ftp_server_name)) {
    if (ftp_server_name == NULL) {
      mgmt_log(stderr, "[WebHttp::handle_submit_snapshot_to_ftpsystem] FTPServerName not specified.");
      ink_hash_table_insert(whc->submit_warn_ht, "FTPServerNameError", NULL);
      if (!(whc->request_state & WEB_HTTP_STATE_SUBMIT_WARN)) {
        HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_MISSING_ENTRY);
        HtmlRndrBr(whc->submit_warn);
      }
      whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
    }
  }


  if (ink_hash_table_lookup(whc->post_data_ht, "FTPUserName", (void **) &ftp_login)) {
    if (ftp_login == NULL) {
      mgmt_log(stderr, "[WebHttp::handle_submit_snapshot_to_ftpsystem] FTPlogin not specified.");
      ink_hash_table_insert(whc->submit_warn_ht, "FTPUserNameError", NULL);
      if (!(whc->request_state & WEB_HTTP_STATE_SUBMIT_WARN)) {
        HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_MISSING_ENTRY);
        HtmlRndrBr(whc->submit_warn);
      }
      whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
    }
  }


  if (ink_hash_table_lookup(whc->post_data_ht, "FTPPassword", (void **) &ftp_password)) {
    if (ftp_password == NULL) {
      mgmt_log(stderr, "[WebHttp::handle_submit_snapshot_to_ftpsystem] FTPpassword not specified.");
      ink_hash_table_insert(whc->submit_warn_ht, "FTPPasswordError", NULL);
      if (!(whc->request_state & WEB_HTTP_STATE_SUBMIT_WARN)) {
        HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_MISSING_ENTRY);
        HtmlRndrBr(whc->submit_warn);
      }
      whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
    }
  }


  if (ink_hash_table_lookup(whc->post_data_ht, "FTPRemoteDir", (void **) &ftp_remote_dir)) {
    if (ftp_remote_dir == NULL) {
      mgmt_log(stderr, "[WebHttp::handle_submit_snapshot_to_ftpsystem] FTPremote_dir not specified.");
      ink_hash_table_insert(whc->submit_warn_ht, "FTPRemoteDirError", NULL);
      if (!(whc->request_state & WEB_HTTP_STATE_SUBMIT_WARN)) {
        HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_MISSING_ENTRY);
        HtmlRndrBr(whc->submit_warn);
      }
      whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
    }
  }

  if (ink_hash_table_lookup(whc->post_data_ht, "ftp_select", (void **) &snap_name)) {
    if (strcmp(snap_name, "- select a snapshot -")) {
      RecGetRecordString_Xmalloc("proxy.config.snapshot_dir", &snapDirFromRecordsConf);
      /*
         char *snap_dir;
         snapDirFromRecordsConf = lmgmt->record_data ->readString("proxy.config.snapshot_dir", &found);
         ink_assert(found);

         if(snapDirFromRecordsConf[0] != '/') {
         const char *snap_dir_cpy = strdup(snapDirFromRecordsConf);
         int newLen;

         newLen = strlen(snap_dir_cpy) + strlen(config_dir) + strlen(snap_name) + 3;
         snapDirFromRecordsConf = new char[newLen];
         ink_assert(snapDirFromRecordsConf != NULL);
         sprintf(snapDirFromRecordsConf, "%s%s%s%s%s", config_dir, DIR_SEP, snap_dir_cpy, DIR_SEP, snap_name);

         newLen = strlen(snap_dir_cpy) + strlen(config_dir) + 2;
         snap_dir = new char[newLen];
         ink_assert(snap_dir != NULL);
         sprintf(snap_dir, "%s%s%s", config_dir, DIR_SEP, snap_dir_cpy);
         } else {

         char *snap_dir_cpy = strdup(snapDirFromRecordsConf);
         int newLen;

         newLen = strlen(snap_dir_cpy) + strlen(snap_name) + 2;
         snapDirFromRecordsConf = new char[newLen];
         ink_assert(snapDirFromRecordsConf != NULL);
         sprintf(snapDirFromRecordsConf, "%s%s%s", snap_dir_cpy, DIR_SEP, snap_name);

         snap_dir = snap_dir_cpy;
         } */

      found = (RecGetRecordString_Xmalloc("proxy.config.temp_dir", &tempDirFromRecordsConf)
               == REC_ERR_OKAY);

      ink_assert(found);

      int newLen;
      char *newStr;

      newLen = strlen(tempDirFromRecordsConf) + strlen(snap_name) + 2;
      char *tmp_ftp_snap = new char[newLen];
      ink_assert(tmp_ftp_snap != NULL);
      snprintf(tmp_ftp_snap, newLen, "%s%s%s", tempDirFromRecordsConf, DIR_SEP, snap_name);

      newLen = strlen(snap_name) + strlen(ftp_remote_dir) + 2;
      newStr = new char[newLen];
      ink_assert(newStr != NULL);
      snprintf(newStr, newLen, "%s%s%s", ftp_remote_dir, DIR_SEP, snap_name);

      if ((ret_val = mkdir(tmp_ftp_snap, DIR_MODE)) < 0) {
        mgmt_log(stderr, "[WebHttp::handle_submit_snapshot_to_ftpsystem] Cannot create %s\n", tmp_ftp_snap);
      }
      char ftpOutput[4096];
      INKMgmtFtp("get", ftp_server_name, ftp_login, ftp_password, tmp_ftp_snap, newStr, ftpOutput);
      if (!strncmp(ftpOutput, "ERROR:", 6)) {
        mgmt_log(stderr, "[WebHttp::handle_submit_snapshot_to_ftpsystem] FTP get failed : %s", ftpOutput);
        goto Ldone;
      }
      snap_result = configFiles->restoreSnap(snap_name, tempDirFromRecordsConf);
      snap_result = configFiles->removeSnap(snap_name, tempDirFromRecordsConf);



    }
  }


  if (ink_hash_table_lookup(whc->post_data_ht, "FTPSaveName", (void **) &snap_name)) {
    if (snap_name != NULL) {
      int localDirLength, remoteDirLength;
      char *newStr;
      char *ftp_remote_dir_name;

      localDirLength = strlen(snap_name) + strlen("/tmp") + 2;
      remoteDirLength = strlen(snap_name) + strlen(ftp_remote_dir) + 2;

      newStr = new char[localDirLength];
      ink_assert(newStr != NULL);
      ftp_remote_dir_name = new char[remoteDirLength];
      ink_assert(ftp_remote_dir_name != NULL);
      snprintf(newStr, localDirLength, "/tmp%s%s", DIR_SEP, snap_name);
      snprintf(ftp_remote_dir_name, remoteDirLength, "%s%s%s", ftp_remote_dir, DIR_SEP, snap_name);
      snap_result = configFiles->takeSnap(snap_name, "/tmp");
      char ftpOutput[4096];
      INKMgmtFtp("put", ftp_server_name, ftp_login, ftp_password, newStr, ftp_remote_dir_name, ftpOutput);
      if (!strncmp(ftpOutput, "ERROR:", 6)) {
        //mgmt_log(stderr, "[WebHttp::handle_submit_snapshot_to_ftpsystem] FTP put failed : %s", ftpOutput);
        fprintf(stderr, "[WebHttp::handle_submit_snapshot_to_ftpsystem] FTP put failed : %s", ftpOutput);
        if (!strncmp(ftpOutput, "ERROR: FTP Put:: permission", 27)) {
          ink_hash_table_insert(whc->submit_warn_ht, "FTPRemoteDirError", NULL);
          if (!(whc->request_state & WEB_HTTP_STATE_SUBMIT_WARN)) {
            HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_PERMISSION_DENIED);
            HtmlRndrBr(whc->submit_warn);
          }
          whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
        }
        goto Ldone;
      }
    }
  }
#endif

Ldone:
  if (submit_from_page) {
    err = WebHttpRender(whc, submit_from_page);
    xfree(submit_from_page);
  } else {
    err = WebHttpRender(whc, HTML_DEFAULT_CONFIGURE_FILE);
  }
  return err;
}




//-------------------------------------------------------------------------
// handle_submit_snapshot_to_floppy
//-------------------------------------------------------------------------

static int
handle_submit_snapshot_to_floppy(WebHttpContext * whc, const char *file)
{
  int err = 0;
  SnapResult snap_result = SNAP_OK;
  char *submit_from_page;
  char *snapDirFromRecordsConf;
  char *cancel;
  char *floppy_drive_mount_point;
  char *floppy_selected_snap_name;
  char *floppy_snap_name;
  // Not used here.
  //struct stat snapDirStat;
  char config_dir[256];
  bool recs_out_of_date;
  char *record_version;
  char *UnmountFloppy;
  int old_euid;
  const char *linkFile;
  ExpandingArray snap_list(25, true);

  if (varStrFromName("proxy.config.config_dir", config_dir, 256) == false)
    mgmt_fatal(stderr,
               "[WebHttp::handle_submit_snapshot] Unable to find configuration directory from proxy.config.config_dir\n");

  // check for submit_from_page
  if (ink_hash_table_lookup(whc->post_data_ht, "submit_from_page", (void **) &submit_from_page)) {
    ink_hash_table_delete(whc->post_data_ht, "submit_from_page");
    whc->top_level_render_file = xstrdup(submit_from_page);
  } else {
    submit_from_page = NULL;
  }

  // check for cancel
  if (ink_hash_table_lookup(whc->post_data_ht, "cancel", (void **) &cancel)) {
    whc->post_data_ht = NULL;
    goto Ldone;
  }
  // check for record_version
  recs_out_of_date = true;
  if (ink_hash_table_lookup(whc->post_data_ht, "record_version", (void **) &record_version)) {
    recs_out_of_date = !record_version_valid(record_version);
    ink_hash_table_delete(whc->post_data_ht, "record_version");
    xfree(record_version);
  }
#ifndef _WIN32

   /**
     Unmount Floppy
    **/
  if (ink_hash_table_lookup(whc->post_data_ht, "Unmount Floppy", (void **) &UnmountFloppy)) {
    if (UnmountFloppy != NULL) {
      int ret = 0;

      if (strcmp(UnmountFloppy, "Unmount Floppy") == 0) {
        char unmountPath[1024];

        if (ink_hash_table_lookup(whc->post_data_ht, "FloppyPath", (void **) &floppy_drive_mount_point)) {
          if (floppy_drive_mount_point != NULL)
            snprintf(unmountPath, sizeof(unmountPath), "/bin/umount %s", floppy_drive_mount_point);
          else {
            NOWARN_UNUSED_RETURN(system("sync;sync;sync"));
            linkFile = "/configure/c_snapshot_floppy.ink";
            ink_hash_table_insert(whc->submit_warn_ht, "CouldnotUnmount", NULL);
            if (!(whc->request_state & WEB_HTTP_STATE_SUBMIT_WARN)) {
              HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_FLOPPY_UNMOUNT_ERR);
              HtmlRndrBr(whc->submit_warn);
            }
            whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
            if (submit_from_page)
              xfree(submit_from_page);
            submit_from_page = xstrdup(linkFile);
            goto Ldone;
          }
        }
        pid_t pid;
        if ((pid = fork()) < 0) {
          goto Ldone;
        } else if (pid == 0) {
          old_euid = getuid();
          seteuid(0);
          setreuid(0, 0);
          ret = system(unmountPath);
          setreuid(old_euid, old_euid);
          exit(ret / 256);
        } else {
          wait(&ret);
        }
      }
      if ((ret / 256)) {
        linkFile = "/configure/c_snapshot_floppy.ink";
        ink_hash_table_insert(whc->submit_warn_ht, "CouldnotUnmount", NULL);
        if (!(whc->request_state & WEB_HTTP_STATE_SUBMIT_WARN)) {
          HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_FLOPPY_UNMOUNT_ERR);
          HtmlRndrBr(whc->submit_warn);
        }
        whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
        if (submit_from_page)
          xfree(submit_from_page);
        submit_from_page = xstrdup(linkFile);
        goto Ldone;
      } else {
        linkFile = "/configure/c_basic.ink";
      }

      char *link = WebHttpGetLink_Xmalloc(linkFile);
      whc->response_hdr->setRefresh(0);
      whc->response_hdr->setRefreshURL(link);
      if (submit_from_page)
        xfree(submit_from_page);
      submit_from_page = xstrdup(linkFile);
      xfree(link);
      goto Ldone;
    }
  }

  if (ink_hash_table_lookup(whc->post_data_ht, "floppy_select", (void **) &floppy_selected_snap_name)) {
    if (floppy_selected_snap_name != NULL) {
      if (strcmp(floppy_selected_snap_name, "- select a snapshot -")) {
        if (ink_hash_table_lookup(whc->post_data_ht, "FloppyPath", (void **) &floppy_drive_mount_point)) {

          RecGetRecordString_Xmalloc("proxy.config.snapshot_dir", &snapDirFromRecordsConf);

          if (snapDirFromRecordsConf[0] != '/') {
            // coverity[alloc_fn][var_assign]
            char *snap_dir_cpy = strdup(snapDirFromRecordsConf);
            int newLen;

            // coverity[noescape]
            newLen = strlen(snap_dir_cpy) + strlen(config_dir) + 2;
            snapDirFromRecordsConf = new char[newLen];
            ink_assert(snapDirFromRecordsConf != NULL);
            // coverity[noescape]
            snprintf(snapDirFromRecordsConf, newLen, "%s%s%s", config_dir, DIR_SEP, snap_dir_cpy);
            if (snap_dir_cpy)
              free(snap_dir_cpy);
          }
          if (ink_hash_table_lookup(whc->post_data_ht, "FloppyPath", (void **) &floppy_drive_mount_point)) {
            snap_result = configFiles->restoreSnap(floppy_selected_snap_name, floppy_drive_mount_point);
            if (snap_result < 0) {
              mgmt_log(stderr, "[WebHttp::handle_submit_snapshot_to_floppy] Restore snap failed");
            }

          }
        }
      }
    }
  }


  if (ink_hash_table_lookup(whc->post_data_ht, "FloppySnapName", (void **) &floppy_snap_name)) {
    if (floppy_snap_name != NULL) {
      if (ink_hash_table_lookup(whc->post_data_ht, "FloppyPath", (void **) &floppy_drive_mount_point)) {

        struct dirent *dirEntry;
        DIR *dir;
        char *fileName;
        // Doesn't seem to be used.
        //struct stat fileInfo;
        //struct stat records_config_fileInfo;
        //fileEntry* fileListEntry;

        dir = opendir(floppy_drive_mount_point);

        if (dir == NULL) {
          mgmt_log(stderr, "[WebHttp::handle_submit_snapshot_to_floppy] Unable to open %s directory: %s\n",
                   floppy_drive_mount_point, strerror(errno));
          return -1;
        }
        // The fun of Solaris - readdir_r requires a buffer passed into it
        //   The man page says this obscene expression gives us the proper
        //     size
        dirEntry = (struct dirent *) xmalloc(sizeof(struct dirent) + pathconf(".", _PC_NAME_MAX) + 1);
        struct dirent *result;
        while (ink_readdir_r(dir, dirEntry, &result) == 0) {
          if (!result)
            break;
          fileName = dirEntry->d_name;
          if (!strcmp(fileName, floppy_snap_name)) {
            ink_hash_table_insert(whc->submit_warn_ht, "FloppyError", NULL);
            if (!(whc->request_state & WEB_HTTP_STATE_SUBMIT_WARN)) {
              HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_DUPLICATE_ENTRY);
              HtmlRndrBr(whc->submit_warn);
            }
            whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
            xfree(dirEntry);
            closedir(dir);
            goto Ldone;
          }
        }

        xfree(dirEntry);
        closedir(dir);


        snap_result = configFiles->takeSnap(floppy_snap_name, floppy_drive_mount_point);
        if (snap_result < 0) {
          mgmt_log(stderr, "[WebHttp::handle_submit_snapshot_to_floppy] Take snap failed");
        } else if (snap_result == 6) {
          // BZ50256
          ink_hash_table_insert(whc->submit_warn_ht, "FloppySaveFailed", NULL);
          if (!(whc->request_state & WEB_HTTP_STATE_SUBMIT_WARN)) {
            HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_FLOPPY_NO_SPACE);
            HtmlRndrBr(whc->submit_warn);
          }
          whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
          goto Ldone;
        }
        ink_hash_table_delete(whc->post_data_ht, "FloppySnapName");
      }
    }
  }
#endif

Ldone:
  if (submit_from_page) {
    err = WebHttpRender(whc, submit_from_page);
    xfree(submit_from_page);
  } else {
    err = WebHttpRender(whc, HTML_DEFAULT_CONFIGURE_FILE);
  }
  return err;
}



//-------------------------------------------------------------------------
// handle_submit_inspector
//-------------------------------------------------------------------------
static int
handle_submit_inspector(WebHttpContext * whc, const char *file)
{
  int err = 0;
  char *submit_from_page;
  char *regex;
  char *regex_action;
  char *list;

  // check for submit_from_page
  if (ink_hash_table_lookup(whc->post_data_ht, "submit_from_page", (void **) &submit_from_page)) {
    ink_hash_table_delete(whc->post_data_ht, "submit_from_page");
    whc->top_level_render_file = xstrdup(submit_from_page);
  } else {
    submit_from_page = NULL;
  }

  // FIXME: button names here are going to be hard to
  // internationalize.  we should put the button names into the
  // dictionary.

  // handle URL Regex operation
  if (ink_hash_table_lookup(whc->post_data_ht, "regex_op", (void **) &regex_action)) {
    if (strcmp(regex_action, "Lookup") == 0) {
      // handle regex lookup
      if (ink_hash_table_lookup(whc->post_data_ht, "regex", (void **) &regex)) {
        if ((err = INKLookupFromCacheUrlRegex(regex, &list)) == INK_ERR_OKAY) {
          whc->cache_query_result = list;
        }
      }
    } else if (strcmp(regex_action, "Delete") == 0) {
      // handle regex delete
      if (ink_hash_table_lookup(whc->post_data_ht, "regex", (void **) &regex)) {
        if ((err = INKDeleteFromCacheUrlRegex(regex, &list)) == INK_ERR_OKAY) {
          whc->cache_query_result = list;
        }
      }
    } else if (strcmp(regex_action, "Invalidate") == 0) {
      // handle regex invalidate
      if (ink_hash_table_lookup(whc->post_data_ht, "regex", (void **) &regex)) {
        if ((err = INKInvalidateFromCacheUrlRegex(regex, &list)) == INK_ERR_OKAY) {
          whc->cache_query_result = list;
        }
      }
    }
  }
  // Error: unknown action
  else {
    mgmt_log(stderr, "Unknown action is specified.");
  }

  if (err != INK_ERR_OKAY) {
    // FIXME: show alarm error for cache inspector!
  }

  if (submit_from_page) {
    err = WebHttpRender(whc, submit_from_page);
    xfree(submit_from_page);
  } else {
    err = WebHttpRender(whc, HTML_DEFAULT_CONFIGURE_FILE);
  }
  return err;
}


//-------------------------------------------------------------------------
// handle_submit_inspector_display
//-------------------------------------------------------------------------

static int
handle_submit_inspector_display(WebHttpContext * whc, const char *file)
{
  int err = 0;
  char *url;
  char *url_action;
  char *buf;
  InkHashTable *ht;
  InkHashTable *query_ht;

  // processFormSubmission will substituteUnsafeChars()
  ht = whc->query_data_ht;
  // extract some basic info for easier access later
  if (ink_hash_table_lookup(ht, "url_op", (void **) &url_action)) {
    // handle URL operation
    if (strcmp(url_action, "Lookup") == 0) {
      // handle url lookup
      if (ink_hash_table_lookup(ht, "url", (void **) &url)) {
        if ((err = INKLookupFromCacheUrl(url, &buf)) == INK_ERR_OKAY) {
          whc->cache_query_result = buf;
        }
      }
    } else if (strcmp(url_action, "Delete") == 0) {
      // the url that cache_inspector takes has to be w/o substituteUnsafeChars()
      if ((query_ht = processFormSubmission_noSubstitute((char *) (whc->request->getQuery()))) != NULL) {
        if (ink_hash_table_lookup(query_ht, "url", (void **) &url)) {
          // handle url delete
          if ((err = INKDeleteFromCacheUrl(url, &buf)) == INK_ERR_OKAY) {
            whc->cache_query_result = buf;
          }
        }
        ink_hash_table_destroy_and_xfree_values(query_ht);
      }
    }
  }
  // Error: unknown action
  else {
    mgmt_log(stderr, "Unknown action is specified.");
  }

  err = WebHttpRender(whc, HTML_INSPECTOR_DISPLAY_FILE);
  return err;
}

//-------------------------------------------------------------------------
// handle_submit_view_logs
//-------------------------------------------------------------------------
static int
handle_submit_view_logs(WebHttpContext * whc, const char *file)
{

  int err;
  char *submit_from_page;
  char *nlines;
  char *substring;
  char *action = NULL;
  char *logfile = NULL;
  char tmp[MAX_TMP_BUF_LEN + 1];
  int file_size;
  time_t file_date_gmt;

  // check for submit_from_page
  if (ink_hash_table_lookup(whc->post_data_ht, "submit_from_page", (void **) &submit_from_page)) {
    ink_hash_table_delete(whc->post_data_ht, "submit_from_page");
    whc->top_level_render_file = xstrdup(submit_from_page);
  } else {
    submit_from_page = NULL;
  }

#if !defined(_WIN32)

  // handle remove/save file action before rendering
  if (!ink_hash_table_lookup(whc->post_data_ht, "logfile", (void **) &logfile))
    goto Ldone;
  if (!ink_hash_table_lookup(whc->post_data_ht, "action", (void **) &action))
    goto Ldone;
  if (!logfile || !action)
    goto Ldone;
  if (strcmp(logfile, "default") == 0)
    goto Ldone;

  if (strcmp(action, "view_last") == 0) {
    if (!ink_hash_table_lookup(whc->post_data_ht, "nlines", (void **) &nlines))
      goto Ldone;
    // 'nlines' entry is missing
    if (nlines == NULL) {
      ink_hash_table_insert(whc->submit_warn_ht, "view_last", NULL);
      goto Lmiss;
    }

  } else if (strcmp(action, "view_subset") == 0) {
    if (!ink_hash_table_lookup(whc->post_data_ht, "substring", (void **) &substring))
      goto Ldone;
    // 'substring' entry is missing
    if (substring == NULL) {
      ink_hash_table_insert(whc->submit_warn_ht, "view_subset", NULL);
      goto Lmiss;
    }

  } else if (strcmp(action, "remove") == 0) {

    snprintf(tmp, MAX_TMP_BUF_LEN, "/bin/rm -f %s", logfile);
    if (system(tmp)) {
      Debug("web2", "[handle_submit_view_logs] unable to execute \"%s\"", tmp);
      if (!(whc->request_state & WEB_HTTP_STATE_SUBMIT_WARN)) {
        whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
        HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_LOG_REMOVE_FAILED);
        HtmlRndrBr(whc->submit_warn);
      }
    } else {
      // done removal - remove from post_data_ht not to display previous action
      ink_hash_table_delete(whc->post_data_ht, "action");
      ink_hash_table_delete(whc->post_data_ht, "logfile");
      xfree(logfile);
      xfree(action);
    }
  } else if (strcmp(action, "save") == 0) {
    WebHandle h_file;
    if ((h_file = WebFileOpenR(logfile)) == WEB_HANDLE_INVALID) {
      Debug("web2", "[handle_submit_view_logs] unable to open logfile \"%s\"", logfile);

      if (!(whc->request_state & WEB_HTTP_STATE_SUBMIT_WARN)) {
        whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
        HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_LOG_SAVE_FAILED);
        HtmlRndrBr(whc->submit_warn);
      }
    } else {
      file_size = WebFileGetSize(h_file);
      file_date_gmt = WebFileGetDateGmt(h_file);
      whc->response_hdr->setStatus(STATUS_OK);
      whc->response_hdr->setLength(file_size);
      whc->response_hdr->setLastMod(file_date_gmt);
      whc->response_hdr->setContentType(TEXT_UNKNOWN);
      while (whc->response_bdy->rawReadFromFile(h_file) > 0);
      WebFileClose(h_file);
      return WEB_HTTP_ERR_OKAY;
    }
  } else {
    Debug("web2", "[handle_submit_view_logs] unknown action '%s' on '%s'", action, logfile);
  }
  goto Ldone;

#endif

Lmiss:
  if (!(whc->request_state & WEB_HTTP_STATE_SUBMIT_WARN)) {
    whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
    HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_MISSING_ENTRY);
    HtmlRndrBr(whc->submit_warn);
  }

Ldone:
  // nothing needs to be done, just start rendering
  if (submit_from_page) {
    err = WebHttpRender(whc, submit_from_page);
    xfree(submit_from_page);
  } else {
    err = WebHttpRender(whc, HTML_DEFAULT_MONITOR_FILE);
  }
  return err;
}

//-------------------------------------------------------------------------
// handle_submit_update
//-------------------------------------------------------------------------

static int
handle_submit_update(WebHttpContext * whc, const char *file)
{

  int err;
  char *submit_from_page;
  bool recs_out_of_date;
  bool file_out_of_date;
  char *record_version;
  char *file_version;
  char *file_contents;
  char *file_checksum;
  char *apply;
  char *cancel;
  char *clear;
  char *clear_cluster;
  bool use_ssl_updated;

  // check for submit_from_page
  if (ink_hash_table_lookup(whc->post_data_ht, "submit_from_page", (void **) &submit_from_page)) {
    ink_hash_table_delete(whc->post_data_ht, "submit_from_page");
    whc->top_level_render_file = xstrdup(submit_from_page);
  } else {
    submit_from_page = NULL;
  }

  // check for cancel
  if (ink_hash_table_lookup(whc->post_data_ht, "cancel", (void **) &cancel)) {
    goto Ldone;
  }
  // check for restart
  if (ink_hash_table_lookup(whc->post_data_ht, "restart", (void **) &cancel)) {
    char *link = WebHttpGetLink_Xmalloc(HTML_DEFAULT_CONFIGURE_FILE);
    lmgmt->ccom->sendClusterMessage(CLUSTER_MSG_SHUTDOWN_MANAGER);
    whc->response_hdr->setRefresh(15);
    whc->response_hdr->setRefreshURL(link);
    if (submit_from_page)
      xfree(submit_from_page);
    submit_from_page = xstrdup("/restart.ink");
    xfree(link);
    goto Ldone;
  }
  // check for clear statistics
  if (ink_hash_table_lookup(whc->post_data_ht, "clear_stats", (void **) &clear)) {
    lmgmt->clearStats();
    goto Ldone;
  }
  // check for cluster clear statistics
  if (ink_hash_table_lookup(whc->post_data_ht, "clear_cluster_stats", (void **) &clear_cluster)) {
    lmgmt->clearStats();
    lmgmt->ccom->sendClusterMessage(CLUSTER_MSG_CLEAR_STATS);
    goto Ldone;
  }
  // check for roll_logs
  if (ink_hash_table_lookup(whc->post_data_ht, "roll_logs", (void **) &cancel)) {
    lmgmt->rollLogFiles();
    goto Ldone;
  }
  // check for apply
  if (ink_hash_table_lookup(whc->post_data_ht, "apply", (void **) &apply)) {
    ink_hash_table_delete(whc->post_data_ht, "apply");
    xfree(apply);
  }
  // check for record_version
  recs_out_of_date = true;
  if (ink_hash_table_lookup(whc->post_data_ht, "record_version", (void **) &record_version)) {
    recs_out_of_date = !record_version_valid(record_version);
    ink_hash_table_delete(whc->post_data_ht, "record_version");
    xfree(record_version);
  }
  // check for a file_version and file_contents
  file_out_of_date = false;
  if (ink_hash_table_lookup(whc->post_data_ht, "file_version", (void **) &file_version)) {
    if (ink_hash_table_lookup(whc->post_data_ht, "file_contents", (void **) &file_contents)) {
      file_out_of_date = true;
      if (ink_hash_table_lookup(whc->post_data_ht, "file_checksum", (void **) &file_checksum)) {
        if (!file_version) {
          file_version = (char *) xstrdup("");
        }
        if (!file_contents) {
          file_contents = (char *) xstrdup("");
        }
        if (!file_checksum) {
          file_checksum = (char *) xstrdup("");
        }
        file_out_of_date = !set_config_file(whc, file_version, file_contents, file_checksum);
        ink_hash_table_delete(whc->post_data_ht, "file_checksum");
        if (file_checksum)
          xfree(file_checksum);
      }
      ink_hash_table_delete(whc->post_data_ht, "file_contents");
      if (file_contents)
        xfree(file_contents);
    }
    ink_hash_table_delete(whc->post_data_ht, "file_version");
    if (file_version)
      xfree(file_version);
  }
  // everything else should be records. if the user modifies the
  // 'proxy.config.admin.use_ssl' variable, we'll have to redirect
  // them appropriately.
  use_ssl_updated = false;
  if (!recs_out_of_date) {
    InkHashTableIteratorState htis;
    InkHashTableEntry *hte;
    char *record;
    char *value;
    for (hte = ink_hash_table_iterator_first(whc->post_data_ht, &htis);
         hte != NULL; hte = ink_hash_table_iterator_next(whc->post_data_ht, &htis)) {
      record = (char *) ink_hash_table_entry_key(whc->post_data_ht, hte);
      value = (char *) ink_hash_table_entry_value(whc->post_data_ht, hte);
      // check for ssl redirect
      if (strcasecmp(record, "proxy.config.admin.use_ssl") == 0) {
        char use_ssl_value[MAX_VAL_LENGTH];     // The value of the current variable
        if ((varStrFromName(record, use_ssl_value, MAX_VAL_LENGTH)) && (ink_atoi(value) != ink_atoi(use_ssl_value))
          ) {
          use_ssl_updated = true;
        }
      }
      // check if entering radius password
      RecString old_pwd_file;
      char *new_pwd_file;
      if (strcasecmp(record, "proxy.config.radius.proc.radius.primary_server.shared_key_file") == 0 ||
          strcasecmp(record, "proxy.config.radius.proc.radius.secondary_server.shared_key_file") == 0) {
        if (value && strcmp(value, FAKE_PASSWORD) == 0)
          continue;             // no new password entered

        // delete the old password file and create a new one
        RecGetRecordString_Xmalloc(record, &old_pwd_file);
        if (old_pwd_file) {     // remove the old_pwd_file
          if (remove(old_pwd_file) != 0)
            Debug("web2", "[handle_submit_update] Failed to remove password file %s", old_pwd_file);
          xfree(old_pwd_file);
        }
        if (value) {
          new_pwd_file = encryptToFileAuth_malloc(value);       // encrypt new pwd if specified
          if (new_pwd_file) {
            set_record_value(whc, record, new_pwd_file);
            xfree(new_pwd_file);
          }
        } else {
          set_record_value(whc, record, NULL);  // no pwd specified by user
        }
      }
      if (strcasecmp(record, "proxy.config.radius.proc.radius.primary_server.shared_key_file") != 0 &&
          strcasecmp(record, "proxy.config.radius.proc.radius.secondary_server.shared_key_file") != 0)
        set_record_value(whc, record, value);
    }
  }
  // warn if out of date submission
  if (recs_out_of_date || file_out_of_date) {
    HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_OUT_OF_DATE);
    HtmlRndrBr(whc->submit_warn);
    whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
  }

  if (use_ssl_updated) {
    if (submit_from_page) {
      xfree(submit_from_page);
    }
    submit_from_page = xstrdup("/ssl_redirect.ink");
  }

  if (submit_from_page && strcmp(submit_from_page, HTML_FEATURE_ON_OFF_FILE) == 0) {
    WebHttpTreeRebuildJsTree();
  }

Ldone:
  if (submit_from_page) {
    err = WebHttpRender(whc, submit_from_page);
    xfree(submit_from_page);
  } else {
    err = WebHttpRender(whc, HTML_DEFAULT_CONFIGURE_FILE);
  }
  return err;

}

//-------------------------------------------------------------------------
// handle_submit_update_config
//-------------------------------------------------------------------------
// This handler is called when submit a post form for Configuration File Editor.
// Uses the hidden tag values to construct and write  new config file.
// If the user presses Cancel, then it should also close
// the current window without committing any changes. If hit "Apply", then
// commits the changes before closing editor window.
//    Since the Configuration File Editor opens in a separate window,
// each time a user hits "Apply", we need to also update the table listing all
// the config rules on the original tab page from which the File Editor window
// was launched - the orignal page is refreshed regularly in order to keep the
// the values in sync with the Configuration File Editor page (is there a
// better way to do this??)
//    The file parameter is not used in this handler because a generic
// c_config_display.ink is used for all files. We determine which file
// is being revised by using the filename tag that's passed in with the
// GET request.
static int
handle_submit_update_config(WebHttpContext * whc, const char *file)
{
  char **rules = NULL;
  char name[10];                // "rule#"
  char *close;
  char *apply;
  char *apply_pwd;
  char *filename;
  char *frecord;
  INKFileNameT type;
  int i, numRules = 0;
  int err = WEB_HTTP_ERR_OKAY;
  char *errBuff = NULL;

  // check for close
  if (ink_hash_table_lookup(whc->post_data_ht, "close", (void **) &close)) {
    //goto Ldone;
    return WEB_HTTP_ERR_OKAY;
  }
  // check for apply
  if (ink_hash_table_lookup(whc->post_data_ht, "apply", (void **) &apply)) {
    ink_hash_table_delete(whc->post_data_ht, "apply");
    xfree(apply);
  }
  // This portion of the code handles parsing the hidden tags with all
  // the current ruleList information; commits this information as new config file

  // get the filename to create the INKCfgContext; do NOT delete the
  // HTML_CONFIG_FILE_TAG entry because we need to use the filename
  // binding to refresh the page
  if (!ink_hash_table_lookup(whc->post_data_ht, HTML_CONFIG_FILE_TAG, (void **) &filename)) {
    // ERROR: no config file specified!!
    whc->response_hdr->setStatus(STATUS_NOT_FOUND);
    WebHttpSetErrorResponse(whc, STATUS_NOT_FOUND);
    goto Lerror;
  }
  // CONFIG-SPECIFIC
  if (ink_hash_table_lookup(g_display_config_ht, filename, (void **) &type)) {

    int maxRules = 0;
    INKCfgContext ctx = INKCfgContextCreate(type);
    if (ctx && (INKCfgContextGet(ctx) == INK_ERR_OKAY)) {
      maxRules = INKCfgContextGetCount(ctx) + MAX_ADD_RULES;
      INKCfgContextDestroy(ctx);
    }

    // read all the rules from the post form into an array of strings
    numRules = 0;
    if (maxRules > 0) {
      rules = new char *[maxRules];
      for (i = 0; i < maxRules; i++) {
        memset(name, 0, 10);
        snprintf(name, sizeof(name), "rule%d", i);

        if (ink_hash_table_lookup(whc->post_data_ht, name, (void **) &(rules[i]))) {
          // do not delete entry from table yet
          if (rules[i])
            numRules++;
          else
            break;              // exit because no more valid rules to read
        }
      }
    }

    switch (type) {
    case INK_FNAME_CACHE_OBJ:
      err = updateCacheConfig(rules, numRules, &errBuff);
      break;
    case INK_FNAME_FILTER:
      // check if regular Apply or special Apply Password
      if (ink_hash_table_lookup(whc->post_data_ht, "apply_pwd", (void **) &apply_pwd)) {
        ink_hash_table_delete(whc->post_data_ht, "apply_pwd");
        xfree(apply_pwd);
        err = updateFilterConfigPassword(whc, &errBuff);
      } else {
        err = updateFilterConfig(rules, numRules, &errBuff);
      }
      break;
    case INK_FNAME_HOSTING:
      err = updateHostingConfig(rules, numRules, &errBuff);
      break;
    case INK_FNAME_ICP_PEER:
      err = updateIcpConfig(rules, numRules, &errBuff);
      break;
    case INK_FNAME_IP_ALLOW:
      err = updateIpAllowConfig(rules, numRules, &errBuff);
      break;
    case INK_FNAME_MGMT_ALLOW:
      err = updateMgmtAllowConfig(rules, numRules, &errBuff);
      break;
    case INK_FNAME_PARENT_PROXY:
      err = updateParentConfig(rules, numRules, &errBuff);
      break;
    case INK_FNAME_PARTITION:
      err = updatePartitionConfig(rules, numRules, &errBuff);
      break;
    case INK_FNAME_REMAP:
      err = updateRemapConfig(rules, numRules, &errBuff);
      break;
    case INK_FNAME_SOCKS:
      err = updateSocksConfig(rules, numRules, &errBuff);
      break;
    case INK_FNAME_SPLIT_DNS:
      err = updateSplitDnsConfig(rules, numRules, &errBuff);
      break;
    case INK_FNAME_UPDATE_URL:
      err = updateUpdateConfig(rules, numRules, &errBuff);
      break;
    case INK_FNAME_VADDRS:
      err = updateVaddrsConfig(rules, numRules, &errBuff);
      break;
    default:
      err = WEB_HTTP_ERR_FAIL;
      break;
    }

    // do not delete the strings in the array because
    // the binding still exists in the hashtable, so memory will
    // be freed when post_data_ht destroyed
    if (rules)
      delete[]rules;

  } else {                      // missing binding from f_xx_config.ink to INKFileNameT
    whc->response_hdr->setStatus(STATUS_NOT_FOUND);
    WebHttpSetErrorResponse(whc, STATUS_NOT_FOUND);
    goto Lerror;
  }

  if (err == WEB_HTTP_ERR_INVALID_CFG_RULE) {
    whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
    HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_CFG_INVALID_RULE);
    HtmlRndrBr(whc->submit_warn);

    if (errBuff) {
      whc->submit_warn->copyFrom(errBuff, strlen(errBuff));
      xfree(errBuff);
    }
  } else if (err != WEB_HTTP_ERR_OKAY) {
    HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_CFG_COMMIT_ERROR);
    HtmlRndrBr(whc->submit_warn);
    whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
  }
  // do not remove and free frecord from post_data_ht because
  // the renderer fn will use it to write the hidden tag
  if (ink_hash_table_lookup(whc->post_data_ht, "frecord", (void **) &frecord)) {
    if (recordRestartCheck(frecord)) {
      ink_hash_table_insert(whc->submit_note_ht, frecord, NULL);
      if (!(whc->request_state & WEB_HTTP_STATE_SUBMIT_NOTE)) {
        HtmlRndrText(whc->submit_note, whc->lang_dict_ht, HTML_ID_RESTART_REQUIRED_FILE);
        HtmlRndrBr(whc->submit_note);
      }
      whc->request_state |= WEB_HTTP_STATE_SUBMIT_NOTE;
    }
  }

  err = WebHttpRender(whc, HTML_CONFIG_DISPLAY_FILE);
  return err;

Lerror:
  mgmt_log("[handle_submit_update_config] Error updating config file");
  return WEB_HTTP_ERR_REQUEST_ERROR;

}

//-------------------------------------------------------------------------
// handle_submit_config_display
//-------------------------------------------------------------------------
// This handler is called when user wants to open the Configuration Editor
// window to edit a config file; so it main purpose is simply to
// render the configurator.ink page
static int
handle_submit_config_display(WebHttpContext * whc, const char *file)
{

  // same HTML_CONFIG_DISPLAY_FILE for all config files
  return WebHttpRender(whc, HTML_CONFIG_DISPLAY_FILE);
}


//-------------------------------------------------------------------------
// network configuration
//-------------------------------------------------------------------------


bool
NICCheck(WebHttpContext * whc, char *updown, char *arg)
{
  bool result;

  result = true;

  if (strcmp(updown, "0") == 0) {
    result = false;
    ink_hash_table_insert(whc->submit_warn_ht, arg, NULL);
    if (!(whc->request_state & WEB_HTTP_STATE_SUBMIT_WARN)) {
      HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_INVALID_ENTRY);
      HtmlRndrBr(whc->submit_warn);
    }
    whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
  }
  return result;
}

void
SetWarning(WebHttpContext * whc, char *arg)
{
  ink_hash_table_insert(whc->submit_warn_ht, arg, NULL);
  if (!(whc->request_state & WEB_HTTP_STATE_SUBMIT_WARN)) {
    HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_INVALID_ENTRY);
    HtmlRndrBr(whc->submit_warn);
  }
  whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
}

//-------------------------------------------------------------------------
// handle_submit_net_config
//-------------------------------------------------------------------------
// This handler is called when user wants to setup network of the appliance

static int
handle_submit_net_config(WebHttpContext * whc, const char *file)
{
  bool recs_out_of_date;
  char *cancel;
  char *record_version;
  char *submit_from_page;
//  FILE* tmp;

//  tmp=fopen("tmp","a");
//  fprintf(tmp,"enter submit\n");

  if (ink_hash_table_lookup(whc->post_data_ht, "submit_from_page", (void **) &submit_from_page)) {
    ink_hash_table_delete(whc->post_data_ht, "submit_from_page");
    whc->top_level_render_file = xstrdup(submit_from_page);
  } else {
    submit_from_page = NULL;
  }
  // check for cancel
  if (ink_hash_table_lookup(whc->post_data_ht, "cancel", (void **) &cancel))
    return WebHttpRender(whc, submit_from_page);

  // check for record_version
  recs_out_of_date = true;
  if (ink_hash_table_lookup(whc->post_data_ht, "record_version", (void **) &record_version)) {
    recs_out_of_date = !record_version_valid(record_version);
    ink_hash_table_delete(whc->post_data_ht, "record_version");
    xfree(record_version);
  }
//  if (recs_out_of_date)
//    goto Lout_of_date;

#if (HOST_OS == linux) || (HOST_OS == solaris)

  InkHashTableIteratorState htis;
  InkHashTableEntry *hte;
  char *key;
  char *value;
  int hn_change, gw_change, dn_change, dns_change;
  int nic_change[5];
  //  int nic_up[5];
  char *dns_ip[3], old_value[265], old_hostname[80], old_gw_ip[80];
  char nic_name[5][10], *nic[5][6], interface[80], *param, *old_ip[5];
  char *hostname = 0, *gw_ip = 0;
  const char *dn = 0;
  int i, j, no;
  char dns_ips[80];
  bool warning, fail, rni;

  //This will be used as flags to verify whether anything changed by the user
  hn_change = 0;
  gw_change = 0;
  dn_change = 0;
  dns_change = 0;
  warning = (fail = (rni = false));
  //FIXNOW - we need to use SysAPI to find the numver of NICs instead of just constant 5
  for (i = 0; i < 5; i++) {
    nic_change[i] = 0;
    ink_strncpy(nic_name[i], "", 1);
    for (j = 0; j < 6; j++) {
      nic[i][j] = NULL;
    }
  }

  int old_euid;
  Config_User_Root(&old_euid);
  // Get the values the user entered
  for (hte = ink_hash_table_iterator_first(whc->post_data_ht, &htis);
       hte != NULL; hte = ink_hash_table_iterator_next(whc->post_data_ht, &htis)) {
    key = (char *) ink_hash_table_entry_key(whc->post_data_ht, hte);
    value = (char *) ink_hash_table_entry_value(whc->post_data_ht, hte);

//    fprintf(stderr,"key=%s, value=%s\n",key,value);

    if (strcmp(key, "HOSTNAME") == 0) {
      hostname = value;
      if (!Net_IsValid_Hostname(hostname)) {
        SetWarning(whc, key);
        warning = true;
      } else {
        if (!Config_GetHostname(old_value, sizeof(old_value))) {
          if (hostname != NULL && strcmp(hostname, old_value) != 0) {
            hn_change = 1;
            ink_strncpy(old_hostname, old_value, sizeof(old_hostname)); //old hostname is used in MGMT API
          }
        } else if (hostname != NULL) {
          hn_change = 1;
        }
      }
    } else if (strcmp(key, "GATEWAY") == 0) {
      gw_ip = value;
      if (!Net_IsValid_IP(gw_ip)) {
        SetWarning(whc, key);
        warning = true;
      } else {
        if (!Config_GetDefaultRouter(old_value, sizeof(old_value))) {
          if (gw_ip != NULL && strcmp(gw_ip, old_value) != 0) {
            ink_strncpy(old_gw_ip, old_value, sizeof(old_gw_ip));
            gw_change = 1;
          }
        } else if (gw_ip != NULL) {
          gw_change = 1;
        }
      }
    } else if (strcmp(key, "domain") == 0) {
      dn = value;
      if (!Config_GetDomain(old_value, sizeof(old_value))) {
        if (dn != NULL && strcmp(dn, old_value) != 0) {
          dn_change = 1;
        } else if (dn == NULL) {
          dn_change = 1;
          dn = "";
        }
      } else if (dn != NULL) {
        dn_change = 1;
      }
    } else if (strstr(key, "DNS") != NULL) {
      no = atoi(key + 3) - 1;
      dns_ip[no] = value;
      if (!Net_IsValid_IP(dns_ip[no])) {
        SetWarning(whc, key);
        warning = true;
      } else {
        if (Config_GetDNS_Server(old_value, sizeof(old_value), no)) {
          if (dns_ip[no] != NULL && strcmp(dns_ip[no], old_value) != 0) {
            dns_change = 1;
          } else if (dns_ip[no] == NULL) {
            dns_change = 1;
          }
        } else if (dns_ip[no] != NULL) {
          dns_change = 1;
        }
      }
    } else if (strstr(key, "NIC") != NULL) {
      ink_strncpy(interface, key + 4, sizeof(interface));
      param = strchr(interface, '_');
      *param = '\0';
      param++;
      no = atoi(interface + 3);
      ink_strncpy(nic_name[no], interface, sizeof(nic_name[no]));

      //No DHCP support any more, hacking way is set it be static always
      nic[no][2] = xstrdup("1");
      if (strcmp(param, "enabled") == 0) {
        nic[no][0] = value;

        if (no == 0) {
          //FIXNOW use SysAPI
          if (strcmp(nic[no][0], "0") == 0) {
            SetWarning(whc, key);
            warning = true;
          }
        }
        //FIXNOW - Use SysAPI
        Config_GetNIC_Status(interface, old_value, sizeof(old_value));
        if (strcmp(old_value, "up") == 0 && strcmp(nic[no][0], "0") == 0) {
          nic_change[no] = 1;
        } else if (strcmp(old_value, "down") == 0 && strcmp(nic[no][0], "1") == 0) {
          nic_change[no] = 1;
        }
      } else if (strcmp(param, "ONBOOT") == 0) {
        nic[no][1] = value;

        if (no == 0) {
          //FIXNOW use SysAPI
          if (strcmp(nic[no][1], "0") == 0) {
            SetWarning(whc, key);
            warning = true;
          }
        }
        //FIXNOW - Use SysAPI
        if (!Config_GetNIC_Start(interface, old_value, sizeof(old_value))) {
          if (strcmp(nic[no][1], "1") == 0 && strcmp(old_value, "not-onboot") == 0) {
            nic_change[no] = 1;
          } else if (strcmp(nic[no][1], "0") == 0 && strcmp(old_value, "onboot") == 0) {
            nic_change[no] = 1;
          }
        } else {
          nic_change[no] = 1;
        }
      } else if (strcmp(param, "BOOTPROTO") == 0) {
        nic[no][2] = value;
        //FIXNOW - Use SysAPI
        if (!Config_GetNIC_Protocol(interface, old_value, sizeof(old_value))) {
          if (strcmp(nic[no][2], "0") == 0 && (strcmp(old_value, "none") == 0 || strcmp(old_value, "static") == 0)) {
            nic_change[no] = 1;
          } else if (strcmp(nic[no][2], "1") == 0 && strcmp(old_value, "dhcp") == 0) {
            nic_change[no] = 1;
          }
        } else {
          nic_change[no] = 1;
        }

        //currently, force the protocol to become static if the old one is dhcp
        if (strcmp(old_value, "dhcp") == 0) {
          // XXX - changed it so we don't change value and instead duplicate the string
          nic[no][2] = xstrdup("1");
          nic_change[no] = 1;
        }
      } else if (strcmp(param, "IPADDR") == 0) {
        nic[no][3] = value;
        //FIXNOW - Use SysAPI
        if (!Net_IsValid_IP(nic[no][3])) {
          SetWarning(whc, key);
          warning = true;
        }
        //FIXNOW - Use SysAPI
        if (!Config_GetNIC_IP(interface, old_value, sizeof(old_value))) {
          if (nic[no][3] != NULL && strcmp(nic[no][3], old_value) != 0) {
            nic_change[no] = 1;
            old_ip[no] = old_value;
          }
          //For dhcp start, the static IP maybe same with the dhcp value
          else {
            char protocol[80];
            Config_GetNIC_Protocol(interface, protocol, sizeof(protocol));
            if (strcmp(protocol, "dhcp") == 0) {
              nic_change[no] = 1;
            }

            if (nic[no][3] == NULL) {
              nic_change[no] = 1;
            }
          }
        } else if (nic[no][3] != NULL) {
          nic_change[no] = 1;
        }
      } else if (strcmp(param, "NETMASK") == 0) {
        nic[no][4] = value;
        //FIXNOW - Use SysAPI
        if (!Net_IsValid_IP(nic[no][4])) {
          SetWarning(whc, key);
          warning = true;
        }
        //FIXNOW - Use SysAPI
        if (!Config_GetNIC_Netmask(interface, old_value, sizeof(old_value))) {
          if (nic[no][4] != NULL && strcmp(nic[no][4], old_value) != 0) {
            nic_change[no] = 1;
          }
          //For dhcp start, the static netmask maybe same with the dhcp value
          else {
            char protocol[80];
            Config_GetNIC_Protocol(interface, protocol, sizeof(protocol));
            if (strcmp(protocol, "dhcp") == 0) {
              nic_change[no] = 1;
            }

            if (nic[no][4] == NULL) {
              nic_change[no] = 1;
            }
          }
        } else if (nic[no][4] != NULL) {
          nic_change[no] = 1;
        }
      } else if (strcmp(param, "GATEWAY") == 0) {
        nic[no][5] = value;
        //FIXNOW - Use SysAPI
        if (!Net_IsValid_IP(nic[no][5])) {
          SetWarning(whc, key);
          warning = true;
        }
        //FIXNOW - Use SysAPI
        if (!Config_GetNIC_Gateway(interface, old_value, sizeof(old_value))) {
          if (nic[no][5] != NULL && strcmp(nic[no][5], old_value) != 0) {
            nic_change[no] = 1;
          }
          //For dhcp start, the gateway maybe same with the dhcp value
          else {
            char protocol[80];
            Config_GetNIC_Protocol(interface, protocol, sizeof(protocol));
            if (strcmp(protocol, "dhcp") == 0) {
              nic_change[no] = 1;
            }

            if (nic[no][5] == NULL) {
              nic_change[no] = 1;
            }
          }
        } else if (nic[no][5] != NULL) {
          nic_change[no] = 1;
        }
      }
    }
  }
  Config_User_Inktomi(old_euid);

  if (!warning) {
    if (hn_change) {
      if (Config_SetHostname(hostname) != 0) {
        fail = true;
      }
    }
    if (gw_change) {
      if (Config_SetDefaultRouter(gw_ip) != 0) {
        fail = true;
      }
    }
    if (dn_change) {
      if (Config_SetDomain(dn) != 0) {
        fail = true;
      }
    }
    if (dns_change) {
      ink_strncpy(dns_ips, "", sizeof(dns_ips));
      //FIXNOW - do we have no. of dns servers from SysAPI?
      for (i = 0; i < 3; i++) {
        if (dns_ip[i] != NULL) {
          strncat(dns_ips, dns_ip[i], sizeof(dns_ips) - strlen(dns_ips) - 1);
          strncat(dns_ips, " ", sizeof(dns_ips) - strlen(dns_ips) - 1);
        }
      }

      if (Config_SetDNS_Servers(dns_ips) != 0) {
        fail = true;
      }
    }
    //FIXNOW - get the no. from SysAPI
    for (i = 0; i < 5; i++) {
      if (strlen(nic_name[i]) != 0) {
        if (nic_change[i]) {
          if (nic[i][0] != NULL && strcmp(nic[i][0], "1") == 0) {
            char onboot[20], protocol[20];
            if (strcmp(nic[i][1], "1") == 0) {
              ink_strncpy(onboot, "onboot", sizeof(onboot));
            } else {
              ink_strncpy(onboot, "not-onboot", sizeof(onboot));
            }

            if (strcmp(nic[i][2], "1") == 0) {
              ink_strncpy(protocol, "static", sizeof(protocol));
            } else {
              ink_strncpy(protocol, "dhcp", sizeof(protocol));
            }
            if (Config_SetNIC_Up(nic_name[i], onboot, protocol, nic[i][3], nic[i][4], nic[i][5]) != 0) {
              fail = true;
            }
          } else {
            char status[80];
            Config_GetNIC_Status(nic_name[i], status, sizeof(status));
            if (strcmp(status, "up") == 0) {    //NIC is disabled
              if (Config_SetNIC_Down(nic_name[i]) != 0) {
                fail = true;
              }
            } else {            //NIC is down&changed, such changes are disallowed.
              if (!(whc->request_state & WEB_HTTP_STATE_SUBMIT_WARN)) {
                HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_NETWORK_CONFIG_DISALLOW);
                HtmlRndrBr(whc->submit_warn);
              }
              whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
            }
          }                     //nic[i][0]
        }                       //nic_change
      }                         //nic_name
    }                           //for
  }                             //warning
//FIXME, need a complete fail message system
  if (fail == true) {
    if (!(whc->request_state & WEB_HTTP_STATE_SUBMIT_WARN)) {
      HtmlRndrText(whc->submit_warn, whc->lang_dict_ht, HTML_ID_NETWORK_CONFIG_FAIL);
      HtmlRndrBr(whc->submit_warn);
    }
    whc->request_state |= WEB_HTTP_STATE_SUBMIT_WARN;
  }

  if (hn_change) {
    if (submit_from_page)
      xfree(submit_from_page);
    submit_from_page = xstrdup("/rename.ink");
  }
#endif
  return WebHttpRender(whc, submit_from_page);
}

//-------------------------------------------------------------------------
// handle_submit_otw_upgrade
//-------------------------------------------------------------------------
static int
handle_submit_otw_upgrade(WebHttpContext * whc, const char *file)
{

  int err = WEB_HTTP_ERR_OKAY;
  char *action;
  char *working_dir;
  char *submit_from_page;
  char tmp[MAX_TMP_BUF_LEN];
  char *link;
  const char *cgi_path;

  if (ink_hash_table_lookup(whc->post_data_ht, "submit_from_page", (void **) &submit_from_page)) {
    ink_hash_table_delete(whc->post_data_ht, "submit_from_page");
    whc->top_level_render_file = xstrdup(submit_from_page);
  } else {
    submit_from_page = NULL;
  }

#ifndef _WIN32

  if (ink_hash_table_lookup(whc->post_data_ht, "action", (void **) &action)) {
    if (strcmp(action, "Cancel") == 0) {
      // upgrade cancelled = return to HTML_OTW_UPGRADE_FILE
      if (ink_hash_table_lookup(whc->post_data_ht, "working_dir", (void **) &working_dir)) {
        // cleanup
        snprintf(tmp, MAX_TMP_BUF_LEN, "/bin/rm -rf %s", working_dir);
        NOWARN_UNUSED_RETURN(system(tmp));
      }
      if (submit_from_page)
        xfree(submit_from_page);
      submit_from_page = xstrdup(HTML_OTW_UPGRADE_FILE);
      if (whc->top_level_render_file)
        xfree(whc->top_level_render_file);
      whc->top_level_render_file = xstrdup(submit_from_page);

    } else {
      // start upgrade = render upgrade page + spawn traffic_shell.cgi script
      link = WebHttpGetLink_Xmalloc(HTML_DEFAULT_MONITOR_FILE);
      cgi_path = WebHttpAddDocRoot_Xmalloc(whc, HTML_OTW_UPGRADE_CGI_FILE);
      int old_euid, old_egid;
      Config_User_Root(&old_euid);
      Config_Grp_Root(&old_egid);
      spawn_cgi(whc, cgi_path, NULL, true, true);
      Config_User_Inktomi(old_euid);
      Config_Grp_Inktomi(old_egid);
      if (submit_from_page)
        xfree(submit_from_page);
      submit_from_page = xstrdup("/upgrade.ink");
      xfree(link);
      xfree((char *) cgi_path);
    }
  }
#endif
  if (submit_from_page) {
    err = WebHttpRender(whc, submit_from_page);
    xfree(submit_from_page);
  } else {
    err = WebHttpRender(whc, HTML_DEFAULT_CONFIGURE_FILE);
  }
  return err;
}

//-------------------------------------------------------------------------
// handle_default
//-------------------------------------------------------------------------

static int
handle_default(WebHttpContext * whc, const char *file)
{

  char *doc_root_file;
  int file_size;
  time_t file_date_gmt;
  WebHandle h_file;

  httpMessage *request = whc->request;
  httpResponse *response_hdr = whc->response_hdr;
  textBuffer *response_bdy = whc->response_bdy;

  const char *request_file = file;
  time_t request_file_ims;
  int request_file_len;

  // requests are supposed to begin with a "/"
  if (*request_file != '/') {
    response_hdr->setStatus(STATUS_NOT_FOUND);
    WebHttpSetErrorResponse(whc, STATUS_NOT_FOUND);
    return WEB_HTTP_ERR_REQUEST_ERROR;
  }
  // first, make sure there are no ..'s in path or root directory
  // access in name for security reasons
  if (strstr(request_file, "..") != NULL || strncmp(request_file, "//", 2) == 0) {
    response_hdr->setStatus(STATUS_FORBIDDEN);
    WebHttpSetErrorResponse(whc, STATUS_FORBIDDEN);
    return WEB_HTTP_ERR_REQUEST_ERROR;
  }

  if (strcmp("/", request_file) == 0) {
    request_file = whc->default_file;
  }
  // check file type and set document type if appropiate
  request_file_len = strlen(request_file);
  if (strcmp(request_file + (request_file_len - 4), ".htm") == 0) {
    response_hdr->setContentType(TEXT_HTML);
  } else if (strcmp(request_file + (request_file_len - 5), ".html") == 0) {
    response_hdr->setContentType(TEXT_HTML);
  } else if (strcmp(request_file + (request_file_len - 4), ".css") == 0) {
    response_hdr->setContentType(TEXT_CSS);
  } else if (strcmp(request_file + (request_file_len - 4), ".gif") == 0) {
    response_hdr->setContentType(IMAGE_GIF);
  } else if (strcmp(request_file + (request_file_len - 4), ".jpg") == 0) {
    response_hdr->setContentType(IMAGE_JPEG);
  } else if (strcmp(request_file + (request_file_len - 5), ".jpeg") == 0) {
    response_hdr->setContentType(IMAGE_JPEG);
  } else if (strcmp(request_file + (request_file_len - 4), ".png") == 0) {
    response_hdr->setContentType(IMAGE_PNG);
  } else if (strcmp(request_file + (request_file_len - 4), ".jar") == 0) {
    response_hdr->setContentType(APP_JAVA);
  } else if (strcmp(request_file + (request_file_len - 3), ".js") == 0) {
    response_hdr->setContentType(APP_JAVASCRIPT);
  } else if (strcmp(request_file + (request_file_len - 4), ".der") == 0) {
    response_hdr->setContentType(APP_X509);
  } else if (strcmp(request_file + (request_file_len - 4), ".dat") == 0) {
    response_hdr->setContentType(APP_AUTOCONFIG);
    response_hdr->setCachable(0);
  } else if (strcmp(request_file + (request_file_len - 4), ".pac") == 0) {
    response_hdr->setContentType(APP_AUTOCONFIG);
    // Fixed INKqa04312 - 02/21/1999 elam
    // We don't want anyone to cache .pac files.
    response_hdr->setCachable(0);
  } else if (strcmp(request_file + (request_file_len - 4), ".zip") == 0) {
    response_hdr->setContentType(APP_ZIP);
  } else {
    // don't serve file types that we don't know about; helps to lock
    // down the webserver.  for example, when serving files out the
    // etc/trafficserver/plugins directory, we don't want to allow the users to
    // access the .so/.dll plugin files.
    response_hdr->setStatus(STATUS_NOT_FOUND);
    WebHttpSetErrorResponse(whc, STATUS_NOT_FOUND);
    return WEB_HTTP_ERR_REQUEST_ERROR;
  }

  // append the appropriate doc_root on to the file
  doc_root_file = WebHttpAddDocRoot_Xmalloc(whc, request_file);

  // open the requested file
  if ((h_file = WebFileOpenR(doc_root_file)) == WEB_HANDLE_INVALID) {
    //could not find file
    xfree(doc_root_file);
    response_hdr->setStatus(STATUS_NOT_FOUND);
    WebHttpSetErrorResponse(whc, STATUS_NOT_FOUND);
    return WEB_HTTP_ERR_REQUEST_ERROR;
  }
  // get the file
  file_size = WebFileGetSize(h_file);
  file_date_gmt = WebFileGetDateGmt(h_file);
  request_file_ims = request->getModTime();

  // special logic for the autoconf port
  if ((whc->server_state & WEB_HTTP_SERVER_STATE_AUTOCONF) && (file_size == 0)) {
    response_hdr->setStatus(STATUS_NOT_FOUND);
    WebHttpSetErrorResponse(whc, STATUS_NOT_FOUND);
    WebFileClose(h_file);
    xfree(doc_root_file);
    return WEB_HTTP_ERR_REQUEST_ERROR;
  }
  // Check to see if the clients copy is up to date.  Ignore the
  // stupid content length that Netscape Navigator sends on the
  // If-Modified-Since line since it not in the HTTP 1.0 standard

  // Since the client sends If-Modified-Since in GMT, make sure that
  // we transform mtime to GMT
  if (request_file_ims != -1 && request_file_ims >= file_date_gmt) {
    response_hdr->setStatus(STATUS_NOT_MODIFIED);
  } else {
    // fetch the file from disk to memory
    response_hdr->setStatus(STATUS_OK);
    response_hdr->setLength(file_size);
    while (response_bdy->rawReadFromFile(h_file) > 0);
  }
  // set the document last-modified header
  response_hdr->setLastMod(file_date_gmt);

  WebFileClose(h_file);
  xfree(doc_root_file);

  return WEB_HTTP_ERR_OKAY;

}



//-------------------------------------------------------------------------
// read_request
//-------------------------------------------------------------------------

int
read_request(WebHttpContext * whc)
{

  const int buffer_size = 2048;
  char *buffer = (char *) alloca(buffer_size);

  httpMessage *request = whc->request;
  httpResponse *response_hdr = whc->response_hdr;

  // first get the request line
  if (sigfdrdln(whc->si, buffer, buffer_size) < 0) {
    // if we can not get the request line, update the status code so
    // it can get logged correctly but do not bother trying to send a
    // response
    response_hdr->setStatus(STATUS_BAD_REQUEST);
    return WEB_HTTP_ERR_REQUEST_FATAL;
  }


  if (request->addRequestLine(buffer) != 0) {
    response_hdr->setStatus(STATUS_BAD_REQUEST);
    WebHttpSetErrorResponse(whc, STATUS_BAD_REQUEST);
    return WEB_HTTP_ERR_REQUEST_ERROR;
  }
  // Check for a scheme we do not understand
  //
  //  If we undertand the scheme, it has
  //   to be HTTP
  if (request->getScheme() == SCHEME_UNKNOWN) {
    response_hdr->setStatus(STATUS_NOT_IMPLEMENTED);
    WebHttpSetErrorResponse(whc, STATUS_NOT_IMPLEMENTED);
    return WEB_HTTP_ERR_REQUEST_ERROR;
  }

  if (request->getMethod() != METHOD_GET && request->getMethod() != METHOD_POST && request->getMethod() != METHOD_HEAD) {
    response_hdr->setStatus(STATUS_NOT_IMPLEMENTED);
    WebHttpSetErrorResponse(whc, STATUS_NOT_IMPLEMENTED);
    return WEB_HTTP_ERR_REQUEST_ERROR;
  }
  // Read the headers of http request line by line until
  //   we get a line that is solely composed of "\r" (or
  //   just "" since not everyone follows the HTTP standard
  //
  do {
    if (sigfdrdln(whc->si, buffer, buffer_size) < 0) {
      response_hdr->setStatus(STATUS_BAD_REQUEST);
      return WEB_HTTP_ERR_REQUEST_FATAL;
    }
    request->addHeader(buffer);
  } while (strcmp(buffer, "\r") != 0 && *buffer != '\0');

  // If there is a content body, read it in
  if (request->addRequestBody(whc->si) < 0) {
    // There was error on reading the response body
    response_hdr->setStatus(STATUS_BAD_REQUEST);
    WebHttpSetErrorResponse(whc, STATUS_NOT_IMPLEMENTED);
    return WEB_HTTP_ERR_REQUEST_ERROR;
  }

  // Drain read channel: In the case of Linux, OS sends reset to the
  // socket if we close it when there is data left on it ot be read
  // (in compliance with TCP). This causes problems with the "POST"
  // method. (for example with update.html). With IE, we found ending
  // "\r\n" were not read.  The following work around is to read all
  // that is left in the socket before closing it.  The same problem
  // applies for Windows 2000 as well.
#if !defined(_WIN32)
#define MAX_DRAIN_BYTES 32
  // INKqa11524: If the user is malicious and keeps sending us data,
  // we'll go into an infinite spin here.  Fix is to only drain up
  // to 32 bytes to allow for funny browser behavior but to also
  // prevent reading forever.
  int drain_bytes = 0;
  if (fcntl(whc->si.fd, F_SETFL, O_NONBLOCK) >= 0) {
    char ch;
    while ((read(whc->si.fd, &ch, 1) > 0) && (drain_bytes < MAX_DRAIN_BYTES)) {
      drain_bytes++;
    }
  }
#else
  {
    unsigned long i;
    if (ioctlsocket(whc->si.fd, FIONREAD, &i) != SOCKET_ERROR) {
      if (i) {
        char *buf = (char *) alloca(i * sizeof(char));
        ink_read_socket(whc->si.fd, buf, i);
      }
    }
  }
#endif

  return WEB_HTTP_ERR_OKAY;

}

//-------------------------------------------------------------------------
// write_response
//-------------------------------------------------------------------------

int
write_response(WebHttpContext * whc)
{
  char *buf_p;
  int bytes_to_write;
  int bytes_written;
  // Make sure that we have a content length
  if (whc->response_hdr->getLength() < 0) {
    whc->response_hdr->setLength(whc->response_bdy->spaceUsed());
  }
  whc->response_hdr->writeHdr(whc->si);
  if (whc->request->getMethod() != METHOD_HEAD) {
    buf_p = whc->response_bdy->bufPtr();
    bytes_to_write = whc->response_bdy->spaceUsed();
    bytes_written = 0;
    while (bytes_to_write) {
      bytes_written = socket_write(whc->si, buf_p, bytes_to_write);
      if (bytes_written < 0) {
        if (errno == EINTR || errno == EAGAIN)
          continue;
        else
          return WEB_HTTP_ERR_FAIL;
      } else {
        bytes_to_write -= bytes_written;
        buf_p += bytes_written;
      }
    }
  }
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// process_query
//-------------------------------------------------------------------------

int
process_query(WebHttpContext * whc)
{
  int err;
  InkHashTable *ht;
  char *value;
  // processFormSubmission will substituteUnsafeChars()
  if ((ht = processFormSubmission((char *) (whc->request->getQuery()))) != NULL) {
    whc->query_data_ht = ht;
    // extract some basic info for easier access later
    if (ink_hash_table_lookup(ht, "mode", (void **) &value)) {
      if (strcmp(value, "1") == 0)
        whc->request_state |= WEB_HTTP_STATE_CONFIGURE;
    }
    if (ink_hash_table_lookup(ht, "detail", (void **) &value)) {
      if (strcmp(value, "more") == 0)
        whc->request_state |= WEB_HTTP_STATE_MORE_DETAIL;
    }
    err = WEB_HTTP_ERR_OKAY;
  } else {
    err = WEB_HTTP_ERR_FAIL;
  }
  return err;
}

//-------------------------------------------------------------------------
// process_post
//-------------------------------------------------------------------------

int
process_post(WebHttpContext * whc)
{
  int err;
  InkHashTable *ht;
  // processFormSubmission will substituteUnsafeChars()
  if ((ht = processFormSubmission(whc->request->getBody())) != NULL) {
    whc->post_data_ht = ht;
    err = WEB_HTTP_ERR_OKAY;
  } else {
    err = WEB_HTTP_ERR_FAIL;
  }
  return err;
}

//-------------------------------------------------------------------------
// signal_handler_init
//-------------------------------------------------------------------------

void
signal_handler_do_nothing(int x)
{
  //  A small function thats whole purpose is to give the signal
  //  handler for breaking out of a network read, somethng to call
  x = x;
}

int
signal_handler_init()
{
  // Setup signal handling.  We want to able to unstick stuck socket
  // connections.  This is accomplished by a watcher thread doing a
  // half close on the incoming socket after a timeout.  To break, out
  // the current read which is likely stuck we have a signal handler
  // on SIGUSR1 which does nothing except by side effect of break the
  // read.  All future reads from the socket should fail since
  // incoming traffic is shutdown on the connection and thread should
  // exit normally
#if !defined(_WIN32)
  sigset_t sigsToBlock;
  // FreeBSD and Linux use SIGUSR1 internally in the threads library
#if (HOST_OS != linux) && (HOST_OS != freebsd) && (HOST_OS != darwin)
  // Set up the handler for SIGUSR1
  struct sigaction sigHandler;
  sigHandler.sa_handler = signal_handler_do_nothing;
  sigemptyset(&sigHandler.sa_mask);
  sigHandler.sa_flags = 0;
  sigaction(SIGUSR1, &sigHandler, NULL);
#endif
  // Block all other signals
  sigfillset(&sigsToBlock);
  sigdelset(&sigsToBlock, SIGUSR1);
  ink_thread_sigsetmask(SIG_SETMASK, &sigsToBlock, NULL);
#endif // !_WIN32
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// ssl_init
//-------------------------------------------------------------------------

int
ssl_init(WebHttpContext * whc)
{
#ifdef HAVE_LIBSSL
  SSL *SSL_con = NULL;
  unsigned int sslErrno;
  char ssl_Error[256];
  SSL_con = SSL_new(whc->ssl_ctx);
  SSL_set_fd(SSL_con, whc->si.fd);
  if (SSL_accept(SSL_con) < 0) {
    sslErrno = ERR_get_error();
    ERR_error_string(sslErrno, ssl_Error);
    mgmt_log(stderr, "[ssl_init] SSL_accept failed: %s", ssl_Error);
    return WEB_HTTP_ERR_FAIL;
  }
  whc->si.SSLcon = SSL_con;
#else
  mgmt_fatal(stderr, "[ssl_init] attempt to use SSL in non-SSL enabled build");
#endif
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// ssl_free
//-------------------------------------------------------------------------

int
ssl_free(WebHttpContext * whc)
{
#ifdef HAVE_LIBSSL
  if (whc->si.SSLcon != NULL) {
    SSL_free((SSL *) whc->si.SSLcon);
  }
#else
  ink_debug_assert(!"[ssl_free] attempt to free SSL context in non-SSL build");
#endif
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// WebHttpInit
//-------------------------------------------------------------------------

void
WebHttpInit()
{

  static int initialized = 0;

  if (initialized != 0) {
    mgmt_log(stderr, "[WebHttpInit] error, initialized twice (%d)", initialized);
  }
  initialized++;

  // initialize autoconf allow files
  g_autoconf_allow_ht = ink_hash_table_create(InkHashTableKeyType_String);
  ink_hash_table_insert(g_autoconf_allow_ht, "/proxy.pac", NULL);
  ink_hash_table_insert(g_autoconf_allow_ht, "/wpad.dat", NULL);
  ink_hash_table_insert(g_autoconf_allow_ht, "/public_key.der", NULL);
  ink_hash_table_insert(g_autoconf_allow_ht, "/synthetic.txt", NULL);

  // initialize submit bindings
  g_submit_bindings_ht = ink_hash_table_create(InkHashTableKeyType_String);
  ink_hash_table_insert(g_submit_bindings_ht, HTML_SUBMIT_ALARM_FILE, (void *) handle_submit_alarm);
  ink_hash_table_insert(g_submit_bindings_ht, HTML_SUBMIT_MGMT_AUTH_FILE, (void *) handle_submit_mgmt_auth);
  //ink_hash_table_insert(g_submit_bindings_ht, HTML_SUBMIT_SNAPSHOT_FILE, handle_submit_snapshot);
  ink_hash_table_insert(g_submit_bindings_ht, HTML_SUBMIT_SNAPSHOT_FILESYSTEM,
                        (void *) handle_submit_snapshot_to_filesystem);
  ink_hash_table_insert(g_submit_bindings_ht, HTML_SUBMIT_SNAPSHOT_FTPSERVER,
                        (void *) handle_submit_snapshot_to_ftpserver);
  ink_hash_table_insert(g_submit_bindings_ht, HTML_SUBMIT_SNAPSHOT_FLOPPY, (void *) handle_submit_snapshot_to_floppy);
  ink_hash_table_insert(g_submit_bindings_ht, HTML_SUBMIT_INSPECTOR_FILE, (void *) handle_submit_inspector);
  ink_hash_table_insert(g_submit_bindings_ht, HTML_SUBMIT_INSPECTOR_DPY_FILE, (void *) handle_submit_inspector_display);
  ink_hash_table_insert(g_submit_bindings_ht, HTML_SUBMIT_VIEW_LOGS_FILE, (void *) handle_submit_view_logs);
  ink_hash_table_insert(g_submit_bindings_ht, HTML_SUBMIT_UPDATE_FILE, (void *) handle_submit_update);
  ink_hash_table_insert(g_submit_bindings_ht, HTML_SUBMIT_UPDATE_CONFIG, (void *) handle_submit_update_config);
  ink_hash_table_insert(g_submit_bindings_ht, HTML_SUBMIT_CONFIG_DISPLAY, (void *) handle_submit_config_display);
  ink_hash_table_insert(g_submit_bindings_ht, HTML_SUBMIT_NET_CONFIG, (void *) handle_submit_net_config);
  ink_hash_table_insert(g_submit_bindings_ht, HTML_SUBMIT_OTW_UPGRADE_FILE, (void *) handle_submit_otw_upgrade);
  // initialize file bindings
  g_file_bindings_ht = ink_hash_table_create(InkHashTableKeyType_String);
  ink_hash_table_insert(g_file_bindings_ht, HTML_CHART_FILE, (void *) handle_chart);
  ink_hash_table_insert(g_file_bindings_ht, HTML_BACKDOOR_STATS, (void *) handle_record_stats);
  ink_hash_table_insert(g_file_bindings_ht, HTML_BACKDOOR_CONFIGS, (void *) handle_record_configs);
  ink_hash_table_insert(g_file_bindings_ht, HTML_BACKDOOR_STATS_REC, (void *) handle_record_stats_rec);
  ink_hash_table_insert(g_file_bindings_ht, HTML_BACKDOOR_CONFIGS_REC, (void *) handle_record_configs_rec);
  ink_hash_table_insert(g_file_bindings_ht, HTML_BACKDOOR_CONFIG_FILES, (void *) handle_config_files);
  ink_hash_table_insert(g_file_bindings_ht, HTML_BACKDOOR_DEBUG_LOGS, (void *) handle_debug_logs);
  ink_hash_table_insert(g_file_bindings_ht, HTML_SYNTHETIC_FILE, (void *) handle_synthetic);

  // initialize extension bindings
  g_extn_bindings_ht = ink_hash_table_create(InkHashTableKeyType_String);
  ink_hash_table_insert(g_extn_bindings_ht, ".cgi", (void *) handle_cgi_extn);
  ink_hash_table_insert(g_extn_bindings_ht, ".ink", (void *) handle_ink_extn);

  // initialize the configurator editing bindings which binds
  // configurator display filename (eg. f_cache_config.ink) to
  // its mgmt API config file type (INKFileNameT)
  g_display_config_ht = ink_hash_table_create(InkHashTableKeyType_String);
  ink_hash_table_insert(g_display_config_ht, HTML_FILE_CACHE_CONFIG, (void *) INK_FNAME_CACHE_OBJ);
  ink_hash_table_insert(g_display_config_ht, HTML_FILE_FILTER_CONFIG, (void *) INK_FNAME_FILTER);
  ink_hash_table_insert(g_display_config_ht, HTML_FILE_HOSTING_CONFIG, (void *) INK_FNAME_HOSTING);
  ink_hash_table_insert(g_display_config_ht, HTML_FILE_ICP_CONFIG, (void *) INK_FNAME_ICP_PEER);
  ink_hash_table_insert(g_display_config_ht, HTML_FILE_IP_ALLOW_CONFIG, (void *) INK_FNAME_IP_ALLOW);
  ink_hash_table_insert(g_display_config_ht, HTML_FILE_MGMT_ALLOW_CONFIG, (void *) INK_FNAME_MGMT_ALLOW);
  ink_hash_table_insert(g_display_config_ht, HTML_FILE_PARENT_CONFIG, (void *) INK_FNAME_PARENT_PROXY);
  ink_hash_table_insert(g_display_config_ht, HTML_FILE_PARTITION_CONFIG, (void *) INK_FNAME_PARTITION);
  ink_hash_table_insert(g_display_config_ht, HTML_FILE_REMAP_CONFIG, (void *) INK_FNAME_REMAP);
  ink_hash_table_insert(g_display_config_ht, HTML_FILE_SOCKS_CONFIG, (void *) INK_FNAME_SOCKS);
  ink_hash_table_insert(g_display_config_ht, HTML_FILE_SPLIT_DNS_CONFIG, (void *) INK_FNAME_SPLIT_DNS);
  ink_hash_table_insert(g_display_config_ht, HTML_FILE_UPDATE_CONFIG, (void *) INK_FNAME_UPDATE_URL);
  ink_hash_table_insert(g_display_config_ht, HTML_FILE_VADDRS_CONFIG, (void *) INK_FNAME_VADDRS);

  // initialize other modules
  WebHttpAuthInit();
  WebHttpLogInit();
  WebHttpRenderInit();
  WebHttpSessionInit();
  WebHttpTreeInit();

  return;

}

//-------------------------------------------------------------------------
// WebHttpHandleConnection
//
// Handles http requests across the web management port
//-------------------------------------------------------------------------

void
WebHttpHandleConnection(WebHttpConInfo * whci)
{

  int err = WEB_HTTP_ERR_OKAY;

  WebHttpContext *whc;
  WebHttpHandler handler;
  char *file;
  char *extn;
  int drain_bytes;
  char ch;

  // initialization
  if ((whc = WebHttpContextCreate(whci)) == NULL)
    goto Ltransaction_close;
  if (signal_handler_init() != WEB_HTTP_ERR_OKAY)
    goto Ltransaction_close;
  if (whc->server_state & WEB_HTTP_SERVER_STATE_SSL_ENABLED)
    if (ssl_init(whc) != WEB_HTTP_ERR_OKAY)
      goto Ltransaction_close;

  // read request
  if ((err = read_request(whc)) != WEB_HTTP_ERR_OKAY)
    goto Lerror_switch;

  // authentication
  if (whc->server_state & WEB_HTTP_SERVER_STATE_AUTH_ENABLED)
    if (WebHttpAuthenticate(whc) != WEB_HTTP_ERR_OKAY)
      goto Ltransaction_send;

  // get our file information
  file = (char *) (whc->request->getFile());
  if (strcmp("/", file) == 0) {
    file = (char *) (whc->default_file);
  }

  Debug("web2", "[WebHttpHandleConnection] request file: %s", file);


  if (whc->server_state & WEB_HTTP_SERVER_STATE_AUTOCONF) {

    // security concern: special treatment if we're handling a request
    // on the autoconf port.  can't have users downloading arbitrary
    // files under the config directory!
    if (!ink_hash_table_isbound(g_autoconf_allow_ht, file)) {
      mgmt_elog(stderr,"[WebHttpHandleConnection] %s not valid autoconf file",file);
      whc->response_hdr->setStatus(STATUS_NOT_FOUND);
      WebHttpSetErrorResponse(whc, STATUS_NOT_FOUND);
      goto Ltransaction_send;
    }

  } else {

    if (WebHttpTreeReturnRefresh(file)) {
      // if we are handling a monitor/mrtg page, configure it to refresh
      if (strncmp(file, "/monitor/", 9) == 0) {
        whc->response_hdr->setRefresh(wGlobals.refreshRate);
      } else if (strncmp(file, "/mrtg/", 6) == 0) {
        whc->response_hdr->setRefresh(REFRESH_RATE_MRTG);
      } else {                  // default
        whc->response_hdr->setRefresh(wGlobals.refreshRate);
      }
    }
    // Make a note if we are a plugin.  Being a plugin will affect our
    // doc_root and how request files and doc_roots are joined to
    // generate an absolute path.  See WebHttpAddDocRoot_Xmalloc()
    if (strncmp(file, "/plugins/", 9) == 0) {
      whc->request_state |= WEB_HTTP_STATE_PLUGIN;
    }

  }

  // process query
  process_query(whc);

  // check submit_binding;
  // if nothing, check file_binding;
  // if nothing, check extn_binding;
  // if still nothing, use the default handler;
  if (ink_hash_table_lookup(g_submit_bindings_ht, file, (void **) &handler)) {
    // workaround: sometimes we receive a GET for our submit cgi's
    // (rather than a resubmitted POST).  In this case, just render
    // the default page since we can't do much else
    if (whc->request->getMethod() != METHOD_POST) {
      if ((strcmp(file, HTML_SUBMIT_INSPECTOR_DPY_FILE) != 0) && (strcmp(file, HTML_SUBMIT_CONFIG_DISPLAY) != 0)) {
        err = WebHttpRender(whc, HTML_DEFAULT_MONITOR_FILE);
        goto Lerror_switch;
      }
    }
    // process post
    process_post(whc);
    // only allow one submission at a time
    ink_mutex_acquire(&wGlobals.submitLock);
    err = handler(whc, file);
    ink_mutex_release(&wGlobals.submitLock);
  } else {
    if (!ink_hash_table_lookup(g_file_bindings_ht, file, (void **) &handler)) {
      extn = file;
      while (*extn != '\0')
        extn++;
      while ((extn > file) && (*extn != '.'))
        extn--;
      if (!ink_hash_table_lookup(g_extn_bindings_ht, extn, (void **) &handler)) {
        handler = handle_default;
      }
    }
    err = handler(whc, file);
  }

Lerror_switch:

  switch (err) {
  case WEB_HTTP_ERR_OKAY:
  case WEB_HTTP_ERR_REQUEST_ERROR:
    goto Ltransaction_send;
  case WEB_HTTP_ERR_FAIL:
  case WEB_HTTP_ERR_REQUEST_FATAL:
  default:
    goto Ltransaction_close;
  }

Ltransaction_send:

  // write response
  if ((err = write_response(whc)) != WEB_HTTP_ERR_OKAY)
    goto Ltransaction_close;

  // close the connection before logging it to reduce latency
#ifndef _WIN32
  shutdown(whc->si.fd, 1);
  drain_bytes = 0;
  if (fcntl(whc->si.fd, F_SETFL, O_NONBLOCK) >= 0) {
    while ((read(whc->si.fd, &ch, 1) > 0) && (drain_bytes < MAX_DRAIN_BYTES)) {
      drain_bytes++;
    }
  }
#endif
  ink_close_socket(whc->si.fd);
  whc->si.fd = -1;

  // log transaction
  if (wGlobals.logFD >= 0)
    WebHttpLogTransaction(whc);

Ltransaction_close:

  // if we didn't close already, close connection
  if (whc->si.fd != -1) {
#ifndef _WIN32
    shutdown(whc->si.fd, 1);
    drain_bytes = 0;
    if (fcntl(whc->si.fd, F_SETFL, O_NONBLOCK) >= 0) {
      while ((read(whc->si.fd, &ch, 1) > 0) && (drain_bytes < MAX_DRAIN_BYTES)) {
        drain_bytes++;
      }
    }
#endif
    ink_close_socket(whc->si.fd);
  }
  // clean up ssl
  if (whc->server_state & WEB_HTTP_SERVER_STATE_SSL_ENABLED)
    ssl_free(whc);

  // clean up memory
  WebHttpContextDestroy(whc);

  return;

}

//-------------------------------------------------------------------------
// WebHttpSetErrorResponse
//
// Formulates a page to return on an HttpStatus condition
//-------------------------------------------------------------------------

void
WebHttpSetErrorResponse(WebHttpContext * whc, HttpStatus_t error)
{

  //-----------------------------------------------------------------------
  // FIXME: HARD-CODED HTML HELL!!!
  //-----------------------------------------------------------------------

  static const char a[] = "<HTML>\n<Head>\n<TITLE>";
  static const char b[] = "</TITLE>\n</HEAD>\n<BODY bgcolor=\"#FFFFFF\"><h1>\n";
  static const char c[] = "</h1>\n</BODY>\n</HTML>\n";
  int errorMsgLen = strlen(httpStatStr[error]);

  // reset the buffer
  whc->response_bdy->reUse();

  // fill in the buffer
  whc->response_bdy->copyFrom(a, strlen(a));
  whc->response_bdy->copyFrom(httpStatStr[error], errorMsgLen);
  whc->response_bdy->copyFrom(b, strlen(b));
  whc->response_bdy->copyFrom(httpStatStr[error], errorMsgLen);
  whc->response_bdy->copyFrom(c, strlen(c));

}

//-------------------------------------------------------------------------
// WebHttpAddDocRoot_Xmalloc
//-------------------------------------------------------------------------

char *
WebHttpAddDocRoot_Xmalloc(WebHttpContext * whc, const char *file)
{

  int file_len = 0;
  char *doc_root_file;
  bool is_plugin;

  is_plugin = whc->request_state & WEB_HTTP_STATE_PLUGIN;

  file_len = strlen(file);
  file_len += is_plugin ? strlen(whc->plugin_doc_root) : strlen(whc->doc_root);
  doc_root_file = (char *) xmalloc(file_len + 1);

  if (is_plugin) {
    ink_strncpy(doc_root_file, whc->plugin_doc_root, file_len);
    strncat(doc_root_file, file + strlen("/plugins"), file_len - strlen(doc_root_file));
  } else {
    ink_strncpy(doc_root_file, whc->doc_root, file_len);
    strncat(doc_root_file, file, file_len - strlen(doc_root_file));
  }

  return doc_root_file;

}
