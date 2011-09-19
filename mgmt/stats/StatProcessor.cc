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
 *  StatProcessor.cc - Functions for computing node and cluster stat
 *                          aggregation
 *
 *
 ****************************************************************************/

#include "ink_config.h"
#include "StatProcessor.h"
#include "ink_unused.h"

#define STAT_CONFIG_FILE "stats.config.xml"

StatObjectList statObjectList;
StatXMLTag currentTag = INVALID_TAG;
StatObject *statObject = NULL;
char *exprContent = NULL;
static unsigned statCount = 0;  // global statistics object counter
bool nodeVar;
bool sumClusterVar;


void
startElement(void *userData, const char *name, const char **atts)
{
  NOWARN_UNUSED(userData);
  NOWARN_UNUSED(name);
  int i = 0;

  if (!strcmp(name, "ink:statistics"))
    currentTag = ROOT_TAG;
  else if (!strcmp(name, "statistics"))
    currentTag = STAT_TAG;
  else if (!strcmp(name, "destination"))
    currentTag = DST_TAG;
  else if (!strcmp(name, "expression"))
    currentTag = EXPR_TAG;
  else
    currentTag = INVALID_TAG;

  switch (currentTag) {
  case STAT_TAG:
    statObject = NEW(new StatObject(++statCount));
    Debug(MODULE_INIT, "\nStat #: ----------------------- %d -----------------------\n", statCount);

    for (i = 0; atts[i]; i += 2) {
      ink_debug_assert(atts[i + 1]);    // Attribute comes in pairs, hopefully.

      if (!strcmp(atts[i], "minimum")) {
        statObject->m_stats_min = (MgmtFloat) atof(atts[i + 1]);
      } else if (!strcmp(atts[i], "maximum")) {
        statObject->m_stats_max = (MgmtFloat) atof(atts[i + 1]);
      } else if (!strcmp(atts[i], "interval")) {
        statObject->m_update_interval = (ink_hrtime) atoi(atts[i + 1]);
      } else if (!strcmp(atts[i], "debug")) {
        statObject->m_debug = (atts[i + 1] && atts[i + 1][0] == '1');
      }

      Debug(MODULE_INIT, "\tDESTINTATION w/ attribute: %s -> %s\n", atts[i], atts[i + 1]);
    }
    break;

  case EXPR_TAG:
    exprContent = (char*)ats_malloc(BUFSIZ * 10);
    memset(exprContent, 0, BUFSIZ * 10);
    break;

  case DST_TAG:
    nodeVar = true;
    sumClusterVar = true;       // Should only be used with cluster variable

    for (i = 0; atts[i]; i += 2) {
      ink_debug_assert(atts[i + 1]);    // Attribute comes in pairs, hopefully.
      if (!strcmp(atts[i], "scope")) {
        nodeVar = (!strcmp(atts[i + 1], "node") ? true : false);
      } else if (!strcmp(atts[i], "operation")) {
        sumClusterVar = (!strcmp(atts[i + 1], "sum") ? true : false);
      }

      Debug(MODULE_INIT, "\tDESTINTATION w/ attribute: %s -> %s\n", atts[i], atts[i + 1]);
    }

    break;

  case INVALID_TAG:
    Debug(MODULE_INIT, "==========================================>%s<=\n", name);
    break;

  default:
    break;
  }
}


void
endElement(void *userData, const char *name)
{
  NOWARN_UNUSED(userData);
  NOWARN_UNUSED(name);
  switch (currentTag) {
  case STAT_TAG:
    statObjectList.enqueue(statObject);
    currentTag = ROOT_TAG;
    break;

  case EXPR_TAG:
    statObject->assignExpr(exprContent); // This hands over ownership of exprContent
    // fall through

  default:
    currentTag = STAT_TAG;
    break;
  }
}


void
charDataHandler(void *userData, const XML_Char * name, int len)
{
  NOWARN_UNUSED(userData);
  NOWARN_UNUSED(len);
  if (currentTag != EXPR_TAG && currentTag != DST_TAG) {
    return;
  }

  char content[BUFSIZ * 10];
  if (XML_extractContent(name, content, BUFSIZ * 10) == 0) {
    return;
  }

  if (currentTag == EXPR_TAG) {
    ink_strlcat(exprContent, content, BUFSIZ * 10); // see above for the size

  } else {
    statObject->assignDst(content, nodeVar, sumClusterVar);
  }
}


StatProcessor::StatProcessor():m_lmgmt(NULL), m_overviewGenerator(NULL)
{
  rereadConfig();
}


void
StatProcessor::rereadConfig()
{
  textBuffer *fileContent = NULL;
  Rollback *fileRB = NULL;
  char *fileBuffer = NULL;
  version_t fileVersion;
  int fileLen;

  statObjectList.clean();
  statCount = 0;                // reset statistics counter

  int ret = configFiles->getRollbackObj(STAT_CONFIG_FILE, &fileRB);
  if (!ret) {
    Debug(MODULE_INIT, " Can't get Rollback for file: %s\n", STAT_CONFIG_FILE);
  }
  fileVersion = fileRB->getCurrentVersion();
  fileRB->getVersion(fileVersion, &fileContent);
  fileBuffer = fileContent->bufPtr();
  fileLen = strlen(fileBuffer);

  /*
   * Start the XML Praser -- the package used is EXPAT
   */
  XML_Parser parser = XML_ParserCreate(NULL);
  XML_SetUserData(parser, NULL);
  XML_SetElementHandler(parser, startElement, endElement);
  XML_SetCharacterDataHandler(parser, charDataHandler);

  /*
   * Substiture every newline with a space to get around
   * the SetCharacterDataHandler problem.
   */
  char *newlinePtr;
  while ((newlinePtr = strchr(fileBuffer, '\n')) != NULL || (newlinePtr = strchr(fileBuffer, '\r')) != NULL) {
    *newlinePtr = ' ';
  }

  /*
   * Parse the input file according to XML standard.
   * Print error if we encounter any
   */
  int status = XML_Parse(parser, fileBuffer, fileLen, true);
  if (!status) {
    mgmt_log(stderr, "%s at line %d\n", XML_ErrorString(XML_GetErrorCode(parser)), XML_GetCurrentLineNumber(parser));
  }

  /*
   * Cleaning upt
   */
  XML_ParserFree(parser);
  delete fileContent;

  Debug(MODULE_INIT, "\n\n---------- END OF PARSING & INITIALIZING ---------\n\n");
}


StatProcessor::~StatProcessor()
{

  Debug(MODULE_INIT, "[StatProcessor] Destructing Statistics Processor\n");

}


void
setTest()
{
  char var_name[64];

  for (int i = 1; i <= 5; i++) {
    memset(var_name, 0, 64);
    snprintf(var_name, sizeof(var_name), "proxy.node.stats.test%d", i);
    if (i == 4) {
      MgmtFloat tmp;
      varFloatFromName("proxy.node.stats.test4", &tmp);
      varSetFloat(var_name, tmp + 1, true);
    } else {
      varSetFloat(var_name, i, true);
    }
  }
}


void
verifyTest()
{
  MgmtFloat tmp1, tmp2;

  // 1. simple copy
  varFloatFromName("proxy.node.stats.test1", &tmp1);
  varFloatFromName("proxy.node.stats.test2", &tmp2);
  if (tmp1 == tmp2) {
    Debug(MODULE_INIT, "PASS -- simple copy");
  } else {
    Debug(MODULE_INIT, "FAIL -- simple copy");
  }

  // 2. simple interval
  varFloatFromName("proxy.node.stats.test3", &tmp2);
  if (tmp2 >= 10) {
    Debug(MODULE_INIT, "PASS -- simple interval & constant");
  } else {
    Debug(MODULE_INIT, "FAIL -- simple interval & constant %f", tmp2);
  }

  // 3. delta
  varFloatFromName("proxy.node.stats.test4", &tmp2);
  if ((tmp2 > 150) && (tmp2 < 250)) {
    Debug(MODULE_INIT, "PASS -- delta");
  } else {
    Debug(MODULE_INIT, "FAIL -- delta %f", tmp2);
  }
}


/**
 * Updating the statistics NOW.
 **/
unsigned short
StatProcessor::processStat()
{
  unsigned short result = 0;

  Debug(MODULE_INIT, "[StatProcessor] Processing Statistics....\n");

//    setTest();
  statObjectList.Eval();
//    verifyTest();

  return (result);
}


/**
 * ExpressionEval
 * --------------
 *
 */
MgmtFloat
ExpressionEval(char *exprString)
{
  StatObject statObject;

  char content[BUFSIZ * 10];
  XML_extractContent(exprString, content, BUFSIZ * 10);

  statObject.assignExpr(content);
  return statObject.NodeStatEval(false);
}
