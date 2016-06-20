/** @file

  Parse the records.config configuration file.

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

#include "ts/ink_platform.h"
#include "ts/ink_memory.h"

#include "ts/TextBuffer.h"
#include "ts/Tokenizer.h"
#include "ts/ink_defs.h"
#include "ts/ink_string.h"

#include "P_RecFile.h"
#include "P_RecUtils.h"
#include "P_RecMessage.h"
#include "P_RecCore.h"
#include "ts/I_Layout.h"

const char *g_rec_config_fpath         = NULL;
LLQ *g_rec_config_contents_llq         = NULL;
InkHashTable *g_rec_config_contents_ht = NULL;
ink_mutex g_rec_config_lock;

//-------------------------------------------------------------------------
// RecConfigFileInit
//-------------------------------------------------------------------------
void
RecConfigFileInit(void)
{
  ink_mutex_init(&g_rec_config_lock, NULL);
  g_rec_config_contents_llq = create_queue();
  g_rec_config_contents_ht  = ink_hash_table_create(InkHashTableKeyType_String);
}

//-------------------------------------------------------------------------
// RecFileImport_Xmalloc
//-------------------------------------------------------------------------
static int
RecFileImport_Xmalloc(const char *file, char **file_buf, int *file_size)
{
  int err = REC_ERR_FAIL;
  RecHandle h_file;
  int bytes_read;

  if (file && file_buf && file_size) {
    *file_buf  = 0;
    *file_size = 0;
    if ((h_file = RecFileOpenR(file)) != REC_HANDLE_INVALID) {
      *file_size = RecFileGetSize(h_file);
      *file_buf  = (char *)ats_malloc(*file_size + 1);
      if (RecFileRead(h_file, *file_buf, *file_size, &bytes_read) != REC_ERR_FAIL && bytes_read == *file_size) {
        (*file_buf)[*file_size] = '\0';
        err                     = REC_ERR_OKAY;
      } else {
        ats_free(*file_buf);
        *file_buf  = 0;
        *file_size = 0;
      }
      RecFileClose(h_file);
    }
  }

  return err;
}

//-------------------------------------------------------------------------
// RecConfigOverrideFromEnvironment
//-------------------------------------------------------------------------
const char *
RecConfigOverrideFromEnvironment(const char *name, const char *value)
{
  ats_scoped_str envname(ats_strdup(name));
  const char *envval = NULL;

  // Munge foo.bar.config into FOO_BAR_CONFIG.
  for (char *c = envname; *c != '\0'; ++c) {
    switch (*c) {
    case '.':
      *c = '_';
      break;
    default:
      *c = ParseRules::ink_toupper(*c);
      break;
    }
  }

  envval = getenv((const char *)envname);
  if (envval) {
    return envval;
  }

  return value;
}

//-------------------------------------------------------------------------
// RecParseConfigFile
//-------------------------------------------------------------------------
int
RecConfigFileParse(const char *path, RecConfigEntryCallback handler, bool inc_version)
{
  char *fbuf;
  int fsize;

  const char *line;
  int line_num;

  char *rec_type_str, *name_str, *data_type_str, *data_str;
  char const *value_str;
  RecT rec_type;
  RecDataT data_type;

  Tokenizer line_tok("\r\n");
  tok_iter_state line_tok_state;

  RecConfigFileEntry *cfe;

  RecDebug(DL_Note, "Reading '%s'", path);

  // watch out, we're altering our g_rec_config_xxx structures
  ink_mutex_acquire(&g_rec_config_lock);

  if (RecFileImport_Xmalloc(path, &fbuf, &fsize) == REC_ERR_FAIL) {
    RecLog(DL_Warning, "Could not import '%s'", path);
    ink_mutex_release(&g_rec_config_lock);
    return REC_ERR_FAIL;
  }
  // clear our g_rec_config_contents_xxx structures
  while (!queue_is_empty(g_rec_config_contents_llq)) {
    cfe = (RecConfigFileEntry *)dequeue(g_rec_config_contents_llq);
    ats_free(cfe->entry);
    ats_free(cfe);
  }
  ink_hash_table_destroy(g_rec_config_contents_ht);
  g_rec_config_contents_ht = ink_hash_table_create(InkHashTableKeyType_String);

  line_tok.Initialize(fbuf, SHARE_TOKS);
  line     = line_tok.iterFirst(&line_tok_state);
  line_num = 1;
  while (line) {
    char *lc = ats_strdup(line);
    char *lt = lc;
    char *ln;

    while (isspace(*lt))
      lt++;
    rec_type_str = strtok_r(lt, " \t", &ln);

    // check for blank lines and comments
    if ((!rec_type_str) || (rec_type_str && (*rec_type_str == '#'))) {
      goto L_next_line;
    }

    name_str      = strtok_r(NULL, " \t", &ln);
    data_type_str = strtok_r(NULL, " \t", &ln);

    // extract the string data (a little bit tricker since it can have spaces)
    if (ln) {
      // 'ln' will point to either the next token or a bunch of spaces
      // if the user didn't supply a value (e.g. 'STRING   ').  First
      // scan past all of the spaces.  If we hit a '\0', then we we
      // know we didn't have a valid value.  If not, set 'data_str' to
      // the start of the token and scan until we find the end.  Once
      // the end is found, back-peddle to remove any trailing spaces.
      while (isspace(*ln))
        ln++;
      if (*ln == '\0') {
        data_str = NULL;
      } else {
        data_str = ln;
        while (*ln != '\0')
          ln++;
        ln--;
        while (isspace(*ln) && (ln > data_str))
          ln--;
        ln++;
        *ln = '\0';
      }
    } else {
      data_str = NULL;
    }

    // check for errors
    if (!(rec_type_str && name_str && data_type_str && data_str)) {
      RecLog(DL_Warning, "Could not parse line at '%s:%d' -- skipping line: '%s'", path, line_num, line);
      goto L_next_line;
    }

    // record type
    if (strcmp(rec_type_str, "CONFIG") == 0) {
      rec_type = RECT_CONFIG;
    } else if (strcmp(rec_type_str, "PROCESS") == 0) {
      rec_type = RECT_PROCESS;
    } else if (strcmp(rec_type_str, "NODE") == 0) {
      rec_type = RECT_NODE;
    } else if (strcmp(rec_type_str, "CLUSTER") == 0) {
      rec_type = RECT_CLUSTER;
    } else if (strcmp(rec_type_str, "LOCAL") == 0) {
      rec_type = RECT_LOCAL;
    } else {
      RecLog(DL_Warning, "Unknown record type '%s' at '%s:%d' -- skipping line", rec_type_str, path, line_num);
      goto L_next_line;
    }

    // data_type
    if (strcmp(data_type_str, "INT") == 0) {
      data_type = RECD_INT;
    } else if (strcmp(data_type_str, "FLOAT") == 0) {
      data_type = RECD_FLOAT;
    } else if (strcmp(data_type_str, "STRING") == 0) {
      data_type = RECD_STRING;
    } else if (strcmp(data_type_str, "COUNTER") == 0) {
      data_type = RECD_COUNTER;
    } else {
      RecLog(DL_Warning, "Unknown data type '%s' at '%s:%d' -- skipping line", data_type_str, path, line_num);
      goto L_next_line;
    }

    // OK, we parsed the record, send it to the handler ...
    value_str = RecConfigOverrideFromEnvironment(name_str, data_str);
    handler(rec_type, data_type, name_str, value_str, value_str == data_str ? REC_SOURCE_EXPLICIT : REC_SOURCE_ENV, inc_version);

    // update our g_rec_config_contents_xxx
    cfe             = (RecConfigFileEntry *)ats_malloc(sizeof(RecConfigFileEntry));
    cfe->entry_type = RECE_RECORD;
    cfe->entry      = ats_strdup(name_str);
    enqueue(g_rec_config_contents_llq, (void *)cfe);
    ink_hash_table_insert(g_rec_config_contents_ht, name_str, NULL);
    goto L_done;

  L_next_line:
    // store this line into g_rec_config_contents_llq so that we can
    // write it out later
    cfe             = (RecConfigFileEntry *)ats_malloc(sizeof(RecConfigFileEntry));
    cfe->entry_type = RECE_COMMENT;
    cfe->entry      = ats_strdup(line);
    enqueue(g_rec_config_contents_llq, (void *)cfe);

  L_done:
    line = line_tok.iterNext(&line_tok_state);
    line_num++;
    ats_free(lc);
  }

  ink_mutex_release(&g_rec_config_lock);
  ats_free(fbuf);

  return REC_ERR_OKAY;
}
