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

#include "libts.h"
#include "MgmtSchema.h"
#include "Main.h"

//********************************************************
// XML Schema Validation
//********************************************************
static InkHashTable *g_record_ht = NULL;

//********************************************************
// validateDefault
//********************************************************
bool
validateDefault(XMLNode * exposure, char *recName, char *default_value)
{
  char *rec_default = 0;

  char *exposure_level = exposure->getAttributeValueByName("level");
  if (!exposure_level) {
    fprintf(stderr, "invalid <exposure> - no attribute 'level' %s\n", recName);
    return false;
  }

  if (strcmp(exposure_level, "system") != 0 && strcmp(exposure_level, "unused") != 0) {
    RecGetRecordDefaultDataString_Xmalloc(recName, &rec_default);

    if (rec_default == NULL) {
      if (default_value != NULL)
        goto Lerror;

    } else {
      if (default_value == NULL)
        goto Lerror;
      if (!(strcmp(rec_default, default_value) == 0))
        if (atof(rec_default) != atof(default_value))
          goto Lerror;
    }
  }
  ats_free(rec_default);

  return true;

Lerror:
  fprintf(stderr, "invalid - default mismatch: %s (%s != %s)\n", recName,
          rec_default ? rec_default : "NULL", default_value ? default_value : "NULL");
  return false;
}


//********************************************************
// validateType
//********************************************************
bool
validateType(XMLNode * record)
{
  RecDataT rec_type = RECD_NULL;
  char *attrName;

  if (!(attrName = record->getAttributeValueByName("name"))) {
    fprintf(stderr, "invalid <record> - 'name' undefined\n");
    goto Lerror;
  }
  if (RecGetRecordDataType((char *) attrName, &rec_type) != REC_ERR_OKAY) {
    fprintf(stderr, "invalid <record> - undefined %s\n", attrName);
    goto Lerror;
  }
  if (!(attrName = record->getAttributeValueByName("type"))) {
    fprintf(stderr, "invalid <record> - 'type' undefined\n");
    goto Lerror;
  }
  if (!((strcmp(attrName, "INT") == 0 && rec_type == RECD_INT)) &&
      !((strcmp(attrName, "STRING") == 0 && rec_type == RECD_STRING)) &&
      !((strcmp(attrName, "FLOAT") == 0 && rec_type == RECD_FLOAT))) {
    fprintf(stderr, "invalid <record> - unknown type: %s\n", attrName);
    goto Lerror;
  }
  return true;

Lerror:
  return false;
}

//********************************************************
// validateRestart
//********************************************************
bool
validateRestart(XMLNode * reconfigure, char *recName)
{

  SCHM_UpdateT restart_t;
  char *nodeValue;

  RecGetRecordUpdateType(recName, &restart_t);

  if (!(nodeValue = reconfigure->getNodeValue())) {
    fprintf(stderr, "invalid <reconfigure> - empty\n");
    goto Lerror;
  }
  if (restart_t != SCHM_RU_NULL) {
    // check only the ones with RU type defined
    if (!(restart_t == SCHM_RU_RESTART_TS && (strcmp(nodeValue, "traffic_server") == 0)) &&
        !(restart_t == SCHM_RU_RESTART_TM && (strcmp(nodeValue, "traffic_manager") == 0)) &&
        !(restart_t == SCHM_RU_RESTART_TC && (strcmp(nodeValue, "traffic_cop") == 0)) &&
        !(restart_t == SCHM_RU_REREAD && (strcmp(nodeValue, "traffic_manager") == 0))) {
      fprintf(stderr, "invalid <reconfigure> - unknown value: %s, %s\n", nodeValue, recName);
      goto Lerror;
    }
  } else {                      // at least check the value defined
    if (!(strcmp(nodeValue, "traffic_server") == 0) &&
        !(strcmp(nodeValue, "traffic_manager") == 0) && !(strcmp(nodeValue, "traffic_cop") == 0)) {
      fprintf(stderr, "invalid <reconfigure> - unknown value: %s, %s\n", nodeValue, recName);
    }
  }
  return true;

Lerror:
  return false;
}


//********************************************************
// validateSyntax
//********************************************************
bool
validateSyntax(XMLNode * validate, char *recName)
{
  SCHM_CheckT check_t;
  char *pattern;
  char *nodeValue;
  char *attrValue;

  RecGetRecordCheckType(recName, &check_t);
  RecGetRecordCheckExpr(recName, &pattern);

  if (!(attrValue = validate->getAttributeValueByName("type"))) {
    fprintf(stderr, "invalid <validate> - 'type' undefined\n");
    goto Lerror;
  }
  if (!(nodeValue = validate->getNodeValue())) {
    // 'integer' type can have no nodeValue
    if (strcmp(attrValue, "integer") != 0) {
      fprintf(stderr, "invalid <validate> - empty: %s\n", recName);
      goto Lerror;
    }
  }

  if (check_t == SCHM_RC_STR && (strcmp(attrValue, "match_regexp") == 0)) {
    if (!nodeValue || !strcmp(nodeValue, pattern))
      goto Lerror;
  }
  if (check_t == SCHM_RC_INT && (strcmp(attrValue, "int_range") == 0)) {
    // RecordsConfig.cc side
    Tokenizer dashTok("-");
    char *p = pattern;
    while (*p != '[') {
      p++;
    }                           // skip to '['
    if (dashTok.Initialize(++p, COPY_TOKS) == 2) {
      int l_limit = atoi(dashTok[0]);
      int u_limit = atoi(dashTok[1]);

      // schema side
      Tokenizer commaTok(",");
      char *s = nodeValue;
      if (commaTok.Initialize(s, COPY_TOKS) == 2) {
        int l_limit_s = atoi(commaTok[0]);
        int u_limit_s = atoi(commaTok[1]);
        if (l_limit != l_limit_s || u_limit != u_limit_s) {
          fprintf(stderr, "invalid <validate> - range mismatch: %s\n", recName);
          goto Lerror;
        }
      } else {
        fprintf(stderr, "invalid <validate> - unknown format: %s\n", recName);
        goto Lerror;
      }
    }
  }


  return true;

Lerror:
  return false;
}


//********************************************************
// validateNode
//********************************************************
bool
validateNode(XMLNode * node, char *default_value)
{
  XMLNode *child;
  //  RecordUpdateType update_t;
  char *varName = NULL;
  //  char *nodeName;
  int err = true;

  if (node) {
    if (strcmp(node->getNodeName(), "appinfo") == 0) {

      // get record node
      child = node->getNodeByPath("record");
      if (!child)
        goto Ldone;

      varName = child->getAttributeValueByName("name");
      if (varName) {
        ink_hash_table_insert(g_record_ht, varName, NULL);
      }
      // validate record type
      err = validateType(child);
      if (!err)
        goto Lerror;

      // validate record restart type
      child = node->getNodeByPath("reconfigure");
      if (child) {
        err = validateRestart(child, varName);
        if (!err)
          goto Lerror;
      }
      // validate record value syntax
      child = node->getNodeByPath("validate");
      if (child) {
        err = validateSyntax(child, varName);
        if (!err)
          goto Lerror;
      }
      // validate record exposure
      child = node->getNodeByPath("exposure");
      if (child) {
        err = validateDefault(child, varName, default_value);
        if (!err)
          goto Lerror;
      }
    }
  }
Ldone:
  return err;

Lerror:
  err = false;
  return err;
}

//********************************************************
// validateSchemaNode
//********************************************************
bool
validateSchemaNode(XMLNode * node)
{
  bool err = true;
  XMLNode *child;
  char *default_value = NULL;

  while (node) {
    validateNode(node, default_value);

    for (int i = 0; i < node->getChildCount(); i++) {
      child = node->getChildNode(i);
      if (strcmp(child->getNodeName(), "attribute") == 0) {
        default_value = child->getAttributeValueByName("default");
        child = child->getNodeByPath("annotation/appinfo");
        if (child) {
          if (!validateNode(child, default_value)) {
            err = false;
          }
        }
      } else {
        if (!validateSchemaNode(child)) {
          err = false;
        }
      }
    }
    break;
  }
  return err;
}

//********************************************************
// validateRecordCoverage
//********************************************************
bool
validateRecordCoverage()
{
  bool err = true;
  //fix me
  return err;
}


//********************************************************
// validateRecordsConfig
//********************************************************
bool
validateRecordsConfig(XMLNode * schema)
{
  bool err = true;

  g_record_ht = ink_hash_table_create(InkHashTableKeyType_String);

  validateSchemaNode(schema);
  validateRecordCoverage();
  return err;

  /*
     Lerror:
     err = false;
     return err;
   */
}
