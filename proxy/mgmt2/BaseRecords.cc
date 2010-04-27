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

/**************************************
 *
 * BaseRecords.cc
 *   Member function definitions for the BaseRecords class.
 *
 */

#include "inktomi++.h"
#include "Main.h"
#include "Main.h"
#include "BaseRecords.h"
#include "RecordsConfig.h"
#include "MgmtSocket.h"

// void destroyRecord(Record* to_destroy)
//
//   A function to free up all memory
//     associated with a Record*
//
void
destroyRecord(Record * to_destroy)
{
  CallBackList *cur;
  CallBackList *next;

  if (to_destroy != NULL) {
    xfree(to_destroy->name);
    if (to_destroy->stype == INK_STRING) {
      xfree(to_destroy->data.string_data);
    }
    // Free up the call back list
    cur = to_destroy->list;
    while (cur != NULL) {
      next = cur->next;
      xfree(cur);
      cur = next;
    }
  }
}

// void destroyRecords(Records* to_destroy)
//
//   A function to free up all memory associated with
//    a Record*
//
void
destroyRecords(Records * to_destroy)
{
  Record *cur;

  if (to_destroy != NULL) {
    for (int i = 0; i < to_destroy->num_recs; i++) {
      cur = to_destroy->recs + i;
      destroyRecord(cur);
    }
    xfree(to_destroy->recs);
  }
}

BaseRecords::BaseRecords(char *mpath, const char *cfile, char *efile)
{
  char fpath[PATH_NAME_MAX];
  InkHashTableEntry *hash_entry;
  InkHashTableIteratorState hash_iterator_state;

  /* Record our pid, for passing to local manager */
  pid = getpid();
  ink_snprintf(str_pid, sizeof(str_pid), "%ld", pid);

  for (int j = 0; j < MAX_RECORD_TYPE; j++) {
    char name[80];
    ink_snprintf(name, sizeof(name), "mgmt-mutex-%d", j);
    ink_mutex_init(&mutex[j], name);
    updateCount[j] = 0;
  }
  // initialze RecordsConfig module
  RecordsConfigInit();

  record_files = new MgmtHashTable("record_files", false, InkHashTableKeyType_String);

  // keep track of configs modified by the user
  user_modified_configs_ht = new MgmtHashTable("user_modified_configs_ht", true, InkHashTableKeyType_String);
  ink_mutex_init(&record_textbuffer_lock, "record_textbuffer_lock");

  config_data.recs = process_data.recs = node_data.recs = cluster_data.recs = local_data.recs = plugin_data.recs = NULL;
  config_data.num_recs = process_data.num_recs = node_data.num_recs = cluster_data.num_recs =
    local_data.num_recs = plugin_data.num_recs = 0;
  if (!cfile) {
    mgmt_fatal(stderr, "[BaseRecords::BaseRecords] No config file specified\n");
  }
  defineRecords();
  ink_strncpy(config_file, cfile, sizeof(config_file));
  if (efile) {
    ink_debug_assert(!"lm.config has been depreicated");
  }

  /* We don't know the number of plugin variables at this time,
     so we will allocate a big array for holding them */
  plugin_data.recs = (Record *) xmalloc(MAX_PLUGIN_RECORDS * sizeof(Record));

  /* For now, we are using a dbm for record sharing */
  snprintf(fpath, sizeof(fpath), "%s%s%s", system_local_state_dir,DIR_SEP,MGMT_DB_FILENAME);
  unlink(fpath);
  record_db = new MgmtDBM(fpath);

  /* Setup RecordType->record_array mappings */
  record_type_map = new MgmtHashTable("record_type_map", false, InkHashTableKeyType_Word);
  if (config_data.recs) {
    record_type_map->mgmt_hash_table_insert((InkHashTableKey) CONFIG, &config_data);
  }
  if (process_data.recs) {
    record_type_map->mgmt_hash_table_insert((InkHashTableKey) PROCESS, &process_data);
  }
  if (node_data.recs) {
    record_type_map->mgmt_hash_table_insert((InkHashTableKey) NODE, &node_data);
  }
  if (cluster_data.recs) {
    record_type_map->mgmt_hash_table_insert((InkHashTableKey) CLUSTER, &cluster_data);
  }
  if (local_data.recs) {
    record_type_map->mgmt_hash_table_insert((InkHashTableKey) LOCAL, &local_data);
  }
  if (plugin_data.recs) {
    record_type_map->mgmt_hash_table_insert((InkHashTableKey) PLUGIN, &plugin_data);
  }

  /* Setup name->(id, RecordType) mappings */
  record_id_map = new MgmtHashTable("record_id_map", true, InkHashTableKeyType_String);
  for (hash_entry = record_type_map->mgmt_hash_table_iterator_first(&hash_iterator_state);
       hash_entry != NULL; hash_entry = record_type_map->mgmt_hash_table_iterator_next(&hash_iterator_state)) {

    Records *recs = (Records *) record_type_map->mgmt_hash_table_entry_value(hash_entry);
    for (int i = 0; i < recs->num_recs; i++) {
      RecordID *tmp;

      ink_assert((tmp = (RecordID *) xmalloc(sizeof(RecordID))));
      tmp->rtype = recs->recs[i].rtype;
      tmp->index = recs->recs[i].id;

      record_id_map->mgmt_hash_table_insert((InkHashTableKey) recs->recs[i].name, (tmp));
    }
  }

  f_update_lock = NULL;

  // read in records file (override precompiled defaults)
  // defer records.config reading until librecords is initialized.

  return;
}                               /* End BaseRecords::BaseRecords */


BaseRecords::~BaseRecords()
{
  InkHashTableEntry *hash_entry;
  InkHashTableIteratorState hash_iterator_state;

  delete record_db;

  /* Cleanup the text buffers */
  for (hash_entry = record_files->mgmt_hash_table_iterator_first(&hash_iterator_state);
       hash_entry != NULL; hash_entry = record_files->mgmt_hash_table_iterator_next(&hash_iterator_state)) {
    textBuffer *tmp = (textBuffer *) record_files->mgmt_hash_table_entry_value(hash_entry);
    delete tmp;
  }

  destroyRecords(&config_data);
  destroyRecords(&process_data);
  destroyRecords(&node_data);
  destroyRecords(&cluster_data);
  destroyRecords(&local_data);
  destroyRecords(&plugin_data);
  delete user_modified_configs_ht;
  delete record_files;
  delete record_type_map;
  delete record_id_map;

  return;
}                               /* End BaseRecords::~BaseRecords */


static inline int
mystrcmp(const char *s1, const char *e1, const char *s2)
{
  if ((size_t) (e1 - s1) != strlen(s2)) {
    return 0;
  } else if (strncmp(s1, s2, e1 - s1) != 0) {
    return 0;
  } else {
    return 1;
  }
}

static int
validate_line(char *buf, int cur_line, const char *cur_file)
{
  char *s;
  char *e;
  int rectype;
  int type;

  s = buf;
  while ((*s != '\0') && isspace(*s)) {
    s += 1;
  }

  if ((s[0] == '#') || (s[0] == '\0')) {
    return 1;
  }

  e = strchr(s, ' ');
  if (!e) {
    goto error;
  }

  if (mystrcmp(s, e, "CONFIG")) {
    rectype = 1;
  } else if (mystrcmp(s, e, "PROCESS")) {
    rectype = 2;
  } else if (mystrcmp(s, e, "NODE")) {
    rectype = 3;
  } else if (mystrcmp(s, e, "CLUSTER")) {
    rectype = 4;
  } else if (mystrcmp(s, e, "LOCAL")) {
    rectype = 5;
  } else {
    goto error;
  }

  s = e;
  while ((*s != '\0') && isspace(*s)) {
    s += 1;
  }

  e = strchr(s, ' ');
  if (!e) {
    goto error;
  }

  s = e;
  while ((*s != '\0') && isspace(*s)) {
    s += 1;
  }

  e = strchr(s, ' ');
  if (!e) {
    goto error;
  }

  if (mystrcmp(s, e, "INT")) {
    type = 1;
  } else if (mystrcmp(s, e, "FLOAT")) {
    type = 2;
  } else if (mystrcmp(s, e, "STRING")) {
    type = 3;
  } else if (mystrcmp(s, e, "COUNTER")) {
    type = 4;
  } else {
    goto error;
  }

  s = e;
  while ((*s != '\0') && isspace(*s)) {
    s += 1;
  }

  if ((e = strchr(s, '\n')) == NULL) {
    // special case, there may not be a newline at the end of the
    // records.config line (e.g. the last line of the file could end
    // with an EOF rather than an '\n'.
    e = s;
    while (*e != '\0')
      e++;
  }
  // check if we're completly missing the record value
  if (s == e) {
    goto error;
  }

  e -= 1;
  while ((e > s) && isspace(*e)) {
    e -= 1;
  }
  e += 1;

  switch (type) {
  case 1:
    while (s != e) {
      if (!isdigit(*s) && (*s != '-')) {
        goto error;
      }
      s += 1;
    }
    break;
  case 2:
    while (s != e) {
      if (!isdigit(*s) && (*s != '-') && (*s != '.') && (*s != 'e')) {
        goto error;
      }
      s += 1;
    }
    break;
  case 3:
    break;
  case 4:
    break;
  }

  return 1;

error:
  if (cur_file) {
    char *n;
    if ((n = strchr(buf, '\n'))) {
      *n = '\0';
    }
    mgmt_elog(stderr, "[BaseRecords] Invalid line '%d:%d' in file '%s' [%s]\n", cur_line, s - buf + 1, cur_file, buf);
  }
  return 0;
}

void
BaseRecords::defineRecords()
{

  int *cur_rec = 0;
  int config_recs = 0, process_recs = 0, node_recs = 0, cluster_recs = 0, local_recs = 0;
  int cur_config = 0, cur_process = 0, cur_node = 0, cur_cluster = 0, cur_loc = 0;
  Record *recs = 0;
  int r;

  // count the records in the RecordsConfig defined in
  // RecordsConfig.h, add each to our records_config hash
  for (r = 0; RecordsConfig[r].value_type != INVALID; r++) {
    switch (RecordsConfig[r].type) {
    case CONFIG:
      config_recs++;
      break;
    case PROCESS:
      process_recs++;
      break;
    case NODE:
      node_recs++;
      break;
    case CLUSTER:
      cluster_recs++;
      break;
    case LOCAL:
      local_recs++;
      break;
    }
  }

  // Allocate space for the various records
  if (config_recs) {
    config_data.num_recs = config_recs;
    ink_assert((config_data.recs = (Record *) xmalloc(sizeof(Record) * config_data.num_recs)));
  }
  if (process_recs) {
    process_data.num_recs = process_recs;
    ink_assert((process_data.recs = (Record *) xmalloc(sizeof(Record) * process_data.num_recs)));
  }
  if (node_recs) {
    node_data.num_recs = node_recs;
    ink_assert((node_data.recs = (Record *) xmalloc(sizeof(Record) * node_data.num_recs)));
  }
  if (cluster_recs) {
    cluster_data.num_recs = cluster_recs;
    ink_assert((cluster_data.recs = (Record *) xmalloc(sizeof(Record) * cluster_data.num_recs)));
  }
  if (local_recs) {
    local_data.num_recs = local_recs;
    ink_assert((local_data.recs = (Record *) xmalloc(sizeof(Record) * local_data.num_recs)));
  }
  // add statically defined default records
  for (r = 0; RecordsConfig[r].value_type != INVALID; r++) {
    switch (RecordsConfig[r].type) {
    case CONFIG:
      recs = config_data.recs;
      cur_rec = &cur_config;
      recs[*cur_rec].rtype = CONFIG;
      break;
    case PROCESS:
      recs = process_data.recs;
      cur_rec = &cur_process;
      recs[*cur_rec].rtype = PROCESS;
      break;
    case NODE:
      recs = node_data.recs;
      cur_rec = &cur_node;
      recs[*cur_rec].rtype = NODE;
      break;
    case CLUSTER:
      recs = cluster_data.recs;
      cur_rec = &cur_cluster;
      recs[*cur_rec].rtype = CLUSTER;
      break;
    case LOCAL:
      recs = local_data.recs;
      cur_rec = &cur_loc;
      recs[*cur_rec].rtype = LOCAL;
      break;
    default:
      mgmt_elog(stderr, "[BaseRecords] Invalid record type, record index = %d\n", r);
      return;                   /* lv: incorrect static data */
    }

    recs[*cur_rec].id = *cur_rec;
    recs[*cur_rec].changed = false;
    recs[*cur_rec].read = false;
    recs[*cur_rec].func = NULL;
    recs[*cur_rec].opaque_token = NULL;
    recs[*cur_rec].list = NULL;

    recs[*cur_rec].name = (char*)RecordsConfig[r].name;
    recs[*cur_rec].stype = RecordsConfig[r].value_type;

    switch (recs[*cur_rec].stype) {
    case INK_INT:
      recs[*cur_rec].data.int_data = (MgmtInt) ink_atoll(RecordsConfig[r].value);
      break;
    case INK_LLONG:
      recs[*cur_rec].data.llong_data = (MgmtLLong) ink_atoll(RecordsConfig[r].value);
      break;
    case INK_FLOAT:
      recs[*cur_rec].data.float_data = (MgmtFloat) atof(RecordsConfig[r].value);
      break;
    case INK_STRING:
      {
        char *value;
        if (RecordsConfig[r].value) {
          size_t value_len = strlen(RecordsConfig[r].value) + 1;
          value = (char *) xmalloc(value_len);
          ink_debug_assert(value);
          ink_strncpy(value, RecordsConfig[r].value, value_len);
        } else {
          value = NULL;
        }
        recs[*cur_rec].data.string_data = value;
      }
      break;
    case INK_COUNTER:
      recs[*cur_rec].data.counter_data = (MgmtIntCounter) ink_atoll(RecordsConfig[r].value);
      break;
    default:
      // Handled here:
      // INVALID, INK_STAT_CONST, INK_STAT_FX, MAX_MGMT_TYPE
      break;
    }
    *cur_rec += 1;
  }

}                               /* End BaseRecords::defineRecords */

int
BaseRecords::rereadRecordFile(char *path, char *f, bool dirty)
{
  int r;
  int cur_line = 0;
  char *param, line[1024], fname[1024];
  bool valid = true, found = false;
  FILE *fin;
  textBuffer *buff = NULL;
  InkHashTableEntry *hte;
  InkHashTableIteratorState htis;

  MgmtHashTable *required_records_ht = NULL;

  // swap hash tables
  MgmtHashTable *user_modified_old_ht;
  user_modified_old_ht = new MgmtHashTable("user_modified_configs_ht", true, InkHashTableKeyType_String);
  user_modified_old_ht = (MgmtHashTable *)
    ink_atomic_swap_ptr(&user_modified_configs_ht, user_modified_old_ht);

  ink_assert(path && f);
  if (!(path && f)) {
    mgmt_fatal(stderr, "[BaseRecords::rereadRecordFile] Null path or file\n");
  }
  // Look for a "shadow" records.config file, internal for process(records.config) only
  ink_snprintf(fname, sizeof(fname), "%s%s%s.shadow", path, DIR_SEP, f);
  if (!(fin = mgmt_fopen(fname, "r+"))) {
    ink_snprintf(fname, sizeof(fname), "%s%s%s", path, DIR_SEP, f);
    if (!(fin = mgmt_fopen(fname, "r+"))) {
      mgmt_fatal(stderr, "[BaseRecords::rereadRecordFile] " "Unable to open file '%s', %s\n", fname, strerror(errno));
    }
  } else {
    mgmt_log(stderr, "Using shadow config file\n");
  }

  // Get the file size to alloc an output "checklist" buffer
  if (fseek(fin, 0, SEEK_END) < 0) {
    mgmt_fatal(stderr, "[BaseRecords::rereadRecordFile] Failed seek in conf file: '%s', %s\n", fname, strerror(errno));
  } else {
    long fsize = ftell(fin);
    buff = new textBuffer(fsize);
    rewind(fin);
  }

  // find all of the required user-override records
  required_records_ht = new MgmtHashTable("required_records_ht", false, InkHashTableKeyType_String);
  for (r = 0; RecordsConfig[r].value_type != INVALID; r++) {
    if (RecordsConfig[r].required == RR_REQUIRED) {
      const char *name = RecordsConfig[r].name;
      required_records_ht->mgmt_hash_table_insert(name, (void*)name);
    }
  }

  while (fgets(line, 1024, fin)) {

    cur_line++;

    if (!validate_line(line, cur_line, fname)) {
      continue;
    }

    char *ltrimmed_line;        /* trim whitespace off the left of the line */
    for (ltrimmed_line = line; *ltrimmed_line && isspace(*ltrimmed_line); ltrimmed_line++);

    if ((ltrimmed_line[0] == '#') || (ltrimmed_line[0] == '\0')) {      /* Skip comments/blank lines */
      buff->copyFrom(line, strlen(line));
      line[0] = '\0';
      continue;
    } else {
      char var_name[1024];
      RecordType rtype;
      RecDataT mtype = RECD_INT;        /* Safe since valid will fall out, c. warning fix */

      param = strtok(ltrimmed_line, " ");
      for (int i = 0; param != NULL && valid; i++) {

        switch (i) {
        case 0:                /* RECORD TYPE */
          if (strcmp("CONFIG", param) == 0) {
            rtype = CONFIG;
          } else if (strcmp("PROCESS", param) == 0) {
            rtype = PROCESS;
          } else if (strcmp("NODE", param) == 0) {
            rtype = NODE;
          } else if (strcmp("CLUSTER", param) == 0) {
            rtype = CLUSTER;
          } else if (strcmp("LOCAL", param) == 0) {
            rtype = LOCAL;
          } else {
            valid = false;
          }
          param = strtok(NULL, " ");
          break;
        case 1:                /* NAME */
          {
            ink_strncpy(var_name, param, sizeof(var_name));

            // if this was required, check if off
            required_records_ht->mgmt_hash_table_delete(param);

            // remove from old, add to current
            if (user_modified_old_ht->mgmt_hash_table_isbound(param)) {
              char *name;
              user_modified_old_ht->mgmt_hash_table_lookup(param, (void **) &name);
              user_modified_old_ht->mgmt_hash_table_delete(param);
              xfree(name);
            }
            const size_t name_size = strlen(param) + 1;
            char *name = (char *) xmalloc(name_size);
            ink_debug_assert(name);
            ink_strncpy(name, param, name_size);
            ink_debug_assert(*name);
            user_modified_configs_ht->mgmt_hash_table_insert(name, name);

            // update our textBuffer
            buff->copyFrom(param, strlen(param));
            buff->copyFrom("\n", strlen("\n"));

            param = strtok(NULL, " ");
          }
          break;
        case 2:                /* DATA TYPE */
          if (strcmp("INT", param) == 0) {
            mtype = RECD_INT;
          } else if (strcmp("LLONG", param) == 0) {
            mtype = RECD_LLONG;
          } else if (strcmp("FLOAT", param) == 0) {
            mtype = RECD_FLOAT;
          } else if (strcmp("STRING", param) == 0) {
            mtype = RECD_STRING;
          } else if (strcmp("COUNTER", param) == 0) {
            mtype = RECD_COUNTER;
          } else {
            valid = false;
          }
          param = strtok(NULL, "\n");   /* Everything till end of line is value */
          break;
        case 3:                /* INITIAL VALUE */
          switch (mtype) {
          case RECD_INT:{
              RecInt tmp = 0;
              int rec_err = RecGetRecordInt(var_name, &tmp);
              found = (rec_err == REC_ERR_OKAY);
              if (found) {
                if (tmp != (RecInt) ink_atoll(param)) {
                  RecSetRecordInt(var_name, (RecInt) ink_atoll(param));
                }
              } else {
                // Modularization: Switch mgmt_fatal to mgmt_log so that
                // we don't have problems while we tempoarily run with
                // both BaseRecords and librecords (e.g. there could be a
                // record defined for librecords that does not exist in
                // BaseRecords).
                //mgmt_fatal("Invalid record specified in file '%s': '%s'\n", f, var_name);
                mgmt_log("Invalid record specified in file '%s': '%s'\n", f, var_name);
              }
              break;
            }
          case RECD_LLONG:{
              RecLLong tmp = 0;
              int rec_err = RecGetRecordLLong(var_name, &tmp);
              found = (rec_err == REC_ERR_OKAY);
              if (found) {
                if (tmp != (RecLLong) ink_atoll(param)) {
                  RecSetRecordLLong(var_name, (RecLLong) ink_atoll(param));
                }
              } else {
                // Modularization: Switch mgmt_fatal to mgmt_log so that
                // we don't have problems while we tempoarily run with
                // both BaseRecords and librecords (e.g. there could be a
                // record defined for librecords that does not exist in
                // BaseRecords).
                //mgmt_fatal("Invalid record specified in file '%s': '%s'\n", f, var_name);
                mgmt_log("Invalid record specified in file '%s': '%s'\n", f, var_name);
              }
              break;
            }
          case RECD_FLOAT:{
              RecFloat tmp = 0.0;
              int rec_err = RecGetRecordFloat(var_name, &tmp);
              found = (rec_err == REC_ERR_OKAY);
              if (found) {
                if (tmp != (RecFloat) atof(param)) {
                  RecSetRecordFloat(var_name, (RecFloat) atof(param));
                }
              } else {
                //mgmt_fatal("Invalid record specified in file '%s': '%s'\n", f, var_name);
                mgmt_log("Invalid record specified in file '%s': '%s'\n", f, var_name);
              }
              break;
            }
          case RECD_STRING:{
              // INKqa07904: Trailing blanks break records.config
              int i;
              for (i = strlen(param) - 1; i >= 0; i--)
                if (isspace(param[i]))
                  param[i] = '\0';
                else
                  break;
              // end of INKqa07904
              RecString tmp;
              int rec_err = RecGetRecordString_Xmalloc(var_name, &tmp);
              found = (rec_err == REC_ERR_OKAY);
              if (found) {
                if (tmp &&      /* Orig. not NULL */
                    strcmp(param, "NULL") != 0 &&       /* Nor is entry in file */
                    strcmp(tmp, param) != 0) {  /* And they are different */
                  RecSetRecordString(var_name, param);
                } else if (tmp &&       /* Being set to NULL */
                           strcmp(param, "NULL") == 0) {
                  RecSetRecordString(var_name, NULL);
                } else if (!tmp &&      /* Var is null, entry is not */
                           strcmp(param, "NULL") != 0) {
                  RecSetRecordString(var_name, param);
                }
                if (tmp)
                  xfree(tmp);
              } else {
                //mgmt_fatal("Invalid record specified in file '%s': '%s'\n", f, var_name);
                mgmt_log("Invalid record specified in file '%s': '%s'\n", f, var_name);
              }
              break;
            }
          case RECD_COUNTER:{
              RecCounter tmp = 0;
              int rec_err = RecGetRecordCounter(var_name, &tmp);
              found = (rec_err == REC_ERR_OKAY);
              if (found) {
                if (tmp != (RecCounter) ink_atoll(param)) {
                  RecSetRecordCounter(var_name, (RecCounter) ink_atoll(param));
                }
              } else {
                //mgmt_fatal("Invalid record specified in file '%s': '%s'\n", f, var_name);
                mgmt_log("Invalid record specified in file '%s': '%s'\n", f, var_name);
              }
              break;
            }
          default:
            valid = false;
            break;
          }
          param = NULL;
          break;
        default:
          ink_assert(false);
          valid = false;
          break;
        }
      }
      if (!valid) {
        mgmt_elog(stderr, "Invalid line '%d' in file '%s'\n", cur_line, fname);
        delete required_records_ht;
        delete buff;
        fclose(fin);
        return -1;
      }
    }
  }
  fclose(fin);

  // did we miss anybody?
  hte = required_records_ht->mgmt_hash_table_iterator_first(&htis);
  if (hte) {
    char *name = (char *) required_records_ht->mgmt_hash_table_entry_value(hte);
    mgmt_fatal(stderr, "Required record not specified: %s\n", name);
  }
  delete required_records_ht;

  // cycle through old, and reset any defaults
  for (hte = user_modified_old_ht->mgmt_hash_table_iterator_first(&htis);
       hte != NULL; hte = user_modified_old_ht->mgmt_hash_table_iterator_next(&htis)) {
    int r;
    char *name = (char *) user_modified_old_ht->mgmt_hash_table_entry_value(hte);
    RecordsConfigIndex->mgmt_hash_table_lookup(name, (void **) &r);
    switch (RecordsConfig[r].value_type) {
    case INK_INT:
      setInteger(name, (MgmtInt) ink_atoll(RecordsConfig[r].value), dirty);
      break;
    case INK_LLONG:
      setLLong(name, (MgmtLLong) ink_atoll(RecordsConfig[r].value), dirty);
      break;
    case INK_FLOAT:
      setFloat(name, (MgmtFloat) atof(RecordsConfig[r].value), dirty);
      break;
    case INK_STRING:
      setString(name, (MgmtString) (RecordsConfig[r].value), dirty);
      break;
    default:
      ink_debug_assert(0);
    }
  }
  delete user_modified_old_ht;

  // swap textBuffers
  ink_mutex_acquire(&record_textbuffer_lock);
  textBuffer *tmp_buff = 0;
  record_files->mgmt_hash_table_lookup((InkHashTableKey) f, (void **) &tmp_buff);
  record_files->mgmt_hash_table_insert((InkHashTableKey) f, buff);
  if (tmp_buff)
    delete tmp_buff;
  ink_mutex_release(&record_textbuffer_lock);

  syncPutRecords(CONFIG, NULL);
  syncPutRecords(LOCAL, NULL);

  return 0;
}                               /* End BaseRecords::rereadRecordFile */


bool
BaseRecords::registerUpdateFunc(int id, RecordType type, RecordUpdateFunc func, void *odata)
{

  if (isvalidRecord(id, type) && func) {
    InkHashTableValue hash_value;

    if (record_type_map->mgmt_hash_table_lookup((InkHashTableKey) type, &hash_value) != 0) {
      ink_mutex_acquire(&mutex[type]);
      ((Records *) hash_value)->recs[id].func = func;
      ((Records *) hash_value)->recs[id].opaque_token = odata;
      ink_mutex_release(&mutex[type]);
    }
    return true;
  }
  return false;
}                               /* End BaseRecords::registerUpdateFunc */


bool
BaseRecords::registerChangeFunc(int id, RecordType type, RecordChangeFunc func, void *odata)
{

  if (isvalidRecord(id, type) && func) {
    InkHashTableValue hash_value;

    if (record_type_map->mgmt_hash_table_lookup((InkHashTableKey) type, &hash_value) != 0) {
      CallBackList *list;

      ink_mutex_acquire(&mutex[type]);

      list = ((Records *) hash_value)->recs[id].list;
      if (list) {
        CallBackList *tmp;

        for (tmp = ((Records *) hash_value)->recs[id].list; tmp->next; tmp = tmp->next);
        ink_assert((tmp->next = (CallBackList *) xmalloc(sizeof(CallBackList))));
        tmp->next->func = func;
        tmp->next->opaque_token = odata;
        tmp->next->next = NULL;
      } else {
        ink_assert((((Records *) hash_value)->recs[id].list = (CallBackList *) xmalloc(sizeof(CallBackList))));
        ((Records *) hash_value)->recs[id].list->func = func;
        ((Records *) hash_value)->recs[id].list->opaque_token = odata;
        ((Records *) hash_value)->recs[id].list->next = NULL;
      }
      ink_mutex_release(&mutex[type]);
    }
    return true;
  }
  return false;
}                               /* End BaseRecords::registerChangeFunc */


bool
BaseRecords::unregisterChangeFunc(int id, RecordType type, RecordChangeFunc func, void *odata)
{

  if (isvalidRecord(id, type) && func) {
    InkHashTableValue hash_value;

    if (record_type_map->mgmt_hash_table_lookup((InkHashTableKey) type, &hash_value) != 0) {
      CallBackList *list, *last;

      ink_mutex_acquire(&mutex[type]);
      for (list = last = ((Records *) hash_value)->recs[id].list; list; list = list->next) {
        if (func == list->func && odata == list->opaque_token) {
          if (last) {
            last->next = list->next;
          } else {
            ((Records *) hash_value)->recs[id].list = list->next;
          }
          xfree(list);
          return true;
        }
        last = list;
      }
      ink_mutex_release(&mutex[type]);
    }
  }
  return false;
}                               /* End BaseRecords::unregisterChangeFunc */


Record *
BaseRecords::getRecord(int id, RecordType rtype)
{

  if (isvalidRecord(id, rtype)) {
    InkHashTableValue hash_value;

    if (record_type_map->mgmt_hash_table_lookup((InkHashTableKey) rtype, &hash_value) != 0) {
      return (&((Records *) hash_value)->recs[id]);
    }
    ink_assert(false);          /* Invariant */
    mgmt_fatal(stderr, "[BaseRecords::getRecord] getRecord failed!\n");
  }
  return NULL;
}                               /* End BaseRecords::getRecord */


MgmtIntCounter
BaseRecords::incrementCounter(int id, RecordType type)
{
  Record *rec;

  if ((rec = getRecord(id, type)) && rec->stype == INK_COUNTER) {
    MgmtIntCounter ret;

    ink_mutex_acquire(&mutex[type]);
    ret = ++(rec->data.counter_data);
    rec->changed = true;
    ink_mutex_release(&mutex[type]);

    return (ret);
  }
  return INVALID;
}                               /* End BaseRecords::incrementCounter */


MgmtIntCounter
BaseRecords::setCounter(int id, RecordType type, MgmtIntCounter value)
{
  Record *rec;

  if ((rec = getRecord(id, type)) && rec->stype == INK_COUNTER) {
    MgmtIntCounter ret;

    ink_mutex_acquire(&mutex[type]);
    if (rec->data.counter_data != value) {
      ret = rec->data.counter_data = value;
      rec->changed = true;
      updateCount[type]++;
    } else {
      ret = value;
    }
    ink_mutex_release(&mutex[type]);

    return (ret);
  }
  return INVALID;
}                               /* End BaseRecords::setCounter */

MgmtInt
BaseRecords::setInteger(int id, RecordType type, MgmtInt value, bool dirty)
{
  Record *rec;

  if ((rec = getRecord(id, type)) && rec->stype == INK_INT) {
    MgmtInt ret;

    ink_mutex_acquire(&mutex[type]);
    if (value != rec->data.int_data) {
      ret = rec->data.int_data = value;
      if (dirty)
        rec->changed = true;
      updateCount[type]++;
    } else {
      ret = value;
    }
    ink_mutex_release(&mutex[type]);

    return (ret);
  }
  return INVALID;
}                               /* End BaseRecords::setInteger */


MgmtLLong
BaseRecords::setLLong(int id, RecordType type, MgmtLLong value, bool dirty)
{
  Record *rec;

  if ((rec = getRecord(id, type)) && rec->stype == INK_LLONG) {
    MgmtLLong ret;

    ink_mutex_acquire(&mutex[type]);
    if (value != rec->data.llong_data) {
      ret = rec->data.llong_data = value;
      if (dirty)
        rec->changed = true;
      updateCount[type]++;
    } else {
      ret = value;
    }
    ink_mutex_release(&mutex[type]);

    return (ret);
  }
  return INVALID;
}                               /* End BaseRecords::setLLong */


MgmtFloat
BaseRecords::setFloat(int id, RecordType type, MgmtFloat value, bool dirty)
{
  Record *rec;

  if ((rec = getRecord(id, type)) && rec->stype == INK_FLOAT) {
    MgmtFloat ret;

    ink_mutex_acquire(&mutex[type]);
    if (value != rec->data.float_data) {
      ret = rec->data.float_data = value;
      if (dirty)
        rec->changed = true;
      updateCount[type]++;
    } else {
      ret = value;
    }
    ink_mutex_release(&mutex[type]);

    return (ret);
  }
  return INVALID;
}                               /* End BaseRecords::setFloat */


bool
BaseRecords::setString(int id, RecordType type, MgmtString value, bool dirty)
{
  Record *rec;

  if ((rec = getRecord(id, type)) && rec->stype == INK_STRING) {

    ink_mutex_acquire(&mutex[type]);
    if (rec->data.string_data) {
      if (value && strcmp(value, rec->data.string_data) == 0) {
        ink_mutex_release(&mutex[type]);
        return true;
      }
      xfree(rec->data.string_data);
    } else if (!value && !rec->data.string_data) {
      ink_mutex_release(&mutex[type]);
      return true;
    }

    if (value) {
      int n = strcmp("", value);

      ink_assert(n != 0);
      if (n != 0) {
        size_t string_data_len = strlen(value) + 1;
        ink_assert((rec->data.string_data = (MgmtString) xmalloc(string_data_len)));
        ink_strncpy(rec->data.string_data, value, string_data_len);
      } else {
        rec->data.string_data = NULL;
      }
    } else {
      rec->data.string_data = NULL;
    }
    if (dirty)
      rec->changed = true;
    updateCount[type]++;
    ink_mutex_release(&mutex[type]);

    return (true);
  }
  return false;
}                               /* End BaseRecords::setString */


MgmtIntCounter
BaseRecords::incrementCounter(const char *name)
{
  int id;
  RecordType type;

  if (idofRecord(name, &id, &type)) {
    return incrementCounter(id, type);
  }
  return INVALID;
}                               /* End BaseRecords::incrementCounter */


MgmtIntCounter
BaseRecords::setCounter(const char *name, MgmtIntCounter value)
{
  int id;
  RecordType type;

  if (idofRecord(name, &id, &type)) {
    return setCounter(id, type, value);
  }
  return INVALID;
}                               /* End BaseRecords::setCounter */


MgmtInt
BaseRecords::setInteger(const char *name, MgmtInt value, bool dirty)
{
  int id;
  RecordType type;

  if (idofRecord(name, &id, &type)) {
    return setInteger(id, type, value, dirty);
  }
  return INVALID;
}                               /* End BaseRecords::setInteger */


MgmtLLong
BaseRecords::setLLong(const char *name, MgmtInt value, bool dirty)
{
  int id;
  RecordType type;

  if (idofRecord(name, &id, &type)) {
    return setLLong(id, type, value, dirty);
  }
  return INVALID;
}                               /* End BaseRecords::setLLong */


MgmtFloat
BaseRecords::setFloat(const char *name, MgmtFloat value, bool dirty)
{
  int id;
  RecordType type;

  if (idofRecord(name, &id, &type)) {
    return setFloat(id, type, value, dirty);
  }
  return INVALID;
}                               /* End BaseRecords::setFloat */

bool
BaseRecords::setString(const char *name, MgmtString value, bool dirty)
{
  int id;
  RecordType type;

  if (idofRecord(name, &id, &type)) {
    return setString(id, type, value, dirty);
  }
  return false;
}                               /* End BaseRecords::setString */


MgmtIntCounter
BaseRecords::readCounter(int id, RecordType type, bool * found)
{
  Record *rec;

  if (found)
    *found = false;
  if ((rec = getRecord(id, type)) && rec->stype == INK_COUNTER) {
    MgmtIntCounter ret;

    ink_mutex_acquire(&mutex[type]);
    ret = rec->data.counter_data;
    rec->read = true;
    ink_mutex_release(&mutex[type]);

    if (found)
      *found = true;
    return ret;
  }
  if (!found) {
    // die if the caller isn't checking 'found'
    mgmt_fatal(stderr, "[Config Error] Unable to find record id: %d type: %d\n", id, type);
  }
  return INVALID;
}                               /* End BaseRecords::readCounter */


MgmtInt
BaseRecords::readInteger(int id, RecordType type, bool * found)
{
  Record *rec;

  if (found)
    *found = false;
  if ((rec = getRecord(id, type)) && rec->stype == INK_INT) {
    MgmtInt ret;

    ink_mutex_acquire(&mutex[type]);
    ret = rec->data.int_data;
    rec->read = true;
    ink_mutex_release(&mutex[type]);

    if (found)
      *found = true;
    return ret;
  }
  if (!found) {
    // die if the caller isn't checking 'found'
    mgmt_fatal(stderr, "[Config Error] Unable to find record id: %d type: %d\n", id, type);
  }
  return INVALID;
}                               /* End BaseRecords::readInteger */


MgmtLLong
BaseRecords::readLLong(int id, RecordType type, bool * found)
{
  Record *rec;

  if (found)
    *found = false;
  if ((rec = getRecord(id, type)) && rec->stype == INK_LLONG) {
    MgmtLLong ret;

    ink_mutex_acquire(&mutex[type]);
    ret = rec->data.llong_data;
    rec->read = true;
    ink_mutex_release(&mutex[type]);

    if (found)
      *found = true;
    return ret;
  }
  if (!found) {
    // die if the caller isn't checking 'found'
    mgmt_fatal(stderr, "[Config Error] Unable to find record id: %d type: %d\n", id, type);
  }
  return INVALID;
}                               /* End BaseRecords::readLLong */


MgmtFloat
BaseRecords::readFloat(int id, RecordType type, bool * found)
{
  Record *rec;

  if (found)
    *found = false;
  if ((rec = getRecord(id, type)) && rec->stype == INK_FLOAT) {
    MgmtFloat ret;

    ink_mutex_acquire(&mutex[type]);
    ret = rec->data.float_data;
    rec->read = true;
    ink_mutex_release(&mutex[type]);

    if (found)
      *found = true;
    return ret;
  }
  if (!found) {
    // die if the caller isn't checking 'found'
    mgmt_fatal(stderr, "[Config Error] Unable to find record id: %d type: %d\n", id, type);
  }
  return INVALID;
}                               /* End BaseRecords::readFloat */


MgmtString
BaseRecords::readString(int id, RecordType type, bool * found)
{
  Record *rec;

  if (found)
    *found = false;
  if ((rec = getRecord(id, type)) && rec->stype == INK_STRING) {
    MgmtString ret;

    ink_mutex_acquire(&mutex[type]);
    if (rec->data.string_data) {
      size_t ret_len = strlen(rec->data.string_data) + 1;
      ink_assert((ret = (MgmtString) xmalloc(ret_len)));
      ink_strncpy(ret, rec->data.string_data, ret_len);
    } else {
      ret = NULL;
    }
    rec->read = true;
    ink_mutex_release(&mutex[type]);

    if (found)
      *found = true;
    return ret;
  }
  if (!found) {
    // die if the caller isn't checking 'found'
    mgmt_fatal(stderr, "[Config Error] Unable to find record id: %d type: %d\n", id, type);
  }
  return NULL;
}                               /* End BaseRecords::readString */


MgmtIntCounter
BaseRecords::readCounter(const char *name, bool * found)
{
  int id;
  RecordType type;

  if (found)
    *found = false;
  if (idofRecord(name, &id, &type)) {
    return (readCounter(id, type, found));
  }
  if (!found) {
    // die if the caller isn't checking 'found'
    mgmt_fatal(stderr, "[Config Error] Unable to find record: %s\n", name);
  }
  return INVALID;
}                               /* End BaseRecords::readCounter */


MgmtInt
BaseRecords::readInteger(const char *name, bool * found)
{
  int id;
  RecordType type;

  if (found)
    *found = false;
  if (idofRecord(name, &id, &type)) {
    return (readInteger(id, type, found));
  }
  if (!found) {
    // die if the caller isn't checking 'found'
    mgmt_fatal(stderr, "[Config Error] Unable to find record: %s\n", name);
  }
  return INVALID;
}                               /* End BaseRecords::readIntegerr */


MgmtInt
BaseRecords::readLLong(const char *name, bool * found)
{
  int id;
  RecordType type;

  if (found)
    *found = false;
  if (idofRecord(name, &id, &type)) {
    return (readLLong(id, type, found));
  }
  if (!found) {
    // die if the caller isn't checking 'found'
    mgmt_fatal(stderr, "[Config Error] Unable to find record: %s\n", name);
  }
  return INVALID;
}                               /* End BaseRecords::readLLong */


MgmtFloat
BaseRecords::readFloat(const char *name, bool * found)
{
  int id;
  RecordType type;

  if (found)
    *found = false;
  if (idofRecord(name, &id, &type)) {
    return (readFloat(id, type, found));
  }
  if (!found) {
    // die if the caller isn't checking 'found'
    mgmt_fatal(stderr, "[Config Error] Unable to find record: %s\n", name);
  }
  return INVALID;
}                               /* End BaseRecords::readFloat */


MgmtString
BaseRecords::readString(const char *name, bool * found)
{
  int id;
  RecordType type;

  if (found)
    *found = false;
  if (idofRecord(name, &id, &type)) {
    return (readString(id, type, found));
  }
  if (!found) {
    // die if the caller isn't checking 'found'
    mgmt_fatal(stderr, "[Config Error] Unable to find record: %s\n", name);
  }
  return NULL;
}                               /* End BaseRecords::readString */


/*
 * Special purpose read functions for reading records of a name from a 
 * copy of the BaseRecords records array.
 */
MgmtIntCounter
BaseRecords::readCounter(const char *name, Records * recs, bool * found)
{
  int id;
  RecordType type;

  if (found)
    *found = false;
  if (idofRecord(name, &id, &type) && recs->recs[id].stype == INK_COUNTER) {
    *found = true;
    return (recs->recs[id].data.counter_data);
  }
  if (!found) {
    // die if the caller isn't checking 'found'
    mgmt_fatal(stderr, "[Config Error] Unable to find record: %s\n", name);
  }
  return INVALID;
}                               /* End BaseRecords::readCounter */


MgmtInt
BaseRecords::readInteger(const char *name, Records * recs, bool * found)
{
  int id;
  RecordType type;

  if (found)
    *found = false;
  if (idofRecord(name, &id, &type) && recs->recs[id].stype == INK_INT) {
    *found = true;
    return (recs->recs[id].data.int_data);
  }
  if (!found) {
    // die if the caller isn't checking 'found'
    mgmt_fatal(stderr, "[Config Error] Unable to find record: %s\n", name);
  }
  return INVALID;
}                               /* End BaseRecords::readIntegerr */


MgmtLLong
BaseRecords::readLLong(const char *name, Records * recs, bool * found)
{
  int id;
  RecordType type;

  if (found)
    *found = false;
  if (idofRecord(name, &id, &type) && recs->recs[id].stype == INK_LLONG) {
    *found = true;
    return (recs->recs[id].data.llong_data);
  }
  if (!found) {
    // die if the caller isn't checking 'found'
    mgmt_fatal(stderr, "[Config Error] Unable to find record: %s\n", name);
  }
  return INVALID;
}                               /* End BaseRecords::readLLong */


MgmtFloat
BaseRecords::readFloat(const char *name, Records * recs, bool * found)
{
  int id;
  RecordType type;

  if (found)
    *found = false;
  if (idofRecord(name, &id, &type) && recs->recs[id].stype == INK_FLOAT) {
    *found = true;
    return (recs->recs[id].data.float_data);
  }
  if (!found) {
    // die if the caller isn't checking 'found'
    mgmt_fatal(stderr, "[Config Error] Unable to find record: %s\n", name);
  }
  return INVALID;
}                               /* End BaseRecords::readFloat */


MgmtString
BaseRecords::readString(const char *name, Records * recs, bool * found)
{
  int id;
  RecordType type;

  if (found)
    *found = false;
  if (idofRecord(name, &id, &type) && recs->recs[id].stype == INK_STRING) {
    MgmtString tmp = (recs->recs[id].data.string_data);
    *found = true;
    if (tmp) {
      MgmtString ret;
      size_t ret_len = sizeof(char) * strlen(tmp) + 1;
      ink_assert((ret = (MgmtString) xmalloc(ret_len)));
      ink_strncpy(ret, tmp, ret_len);
      return ret;
    }
  }
  if (!found) {
    // die if the caller isn't checking 'found'
    mgmt_fatal(stderr, "[Config Error] Unable to find record: %s\n", name);
  }
  return NULL;
}                               /* End BaseRecords::readString */


bool
BaseRecords::isvalidRecord(int id, RecordType type)
{
  InkHashTableValue hash_value;

  if (record_type_map->mgmt_hash_table_lookup((InkHashTableKey) type, &hash_value) != 0) {
    if (id >= 0 && id < ((Records *) hash_value)->num_recs) {
      return true;
    }
  } else {
    mgmt_log(stderr, "[BaseRecords::isvalidRecord] Unrecognized record type seen: %d\n", type);
  }
  return false;
}                               /* End BaseRecords::isvalidRecord */


MgmtType
BaseRecords::typeofRecord(int id, RecordType type)
{
  Record *rec;

  if ((rec = getRecord(id, type))) {
    return rec->stype;
  }
  return INVALID;
}                               /* End BaseRecords::typeofRecord */


char *
BaseRecords::nameofRecord(int id, RecordType type)
{
  Record *rec;

  if ((rec = getRecord(id, type))) {
    return rec->name;
  }
  return NULL;
}                               /* End BaseRecords::nameofRecord */


bool
BaseRecords::idofRecord(const char *name, int *id, RecordType * type)
{
  InkHashTableValue hash_value;

  if (record_id_map->mgmt_hash_table_lookup((InkHashTableKey) name, &hash_value) != 0) {
    *id = ((RecordID *) hash_value)->index;
    *type = ((RecordID *) hash_value)->rtype;
    return true;
  }
  return false;
}                               /* End BaseRecords::idofRecord */


bool
BaseRecords::id_typeofRecord(const char *name, int *id, RecordType * type, MgmtType * mgmt_type)
{
  InkHashTableValue hash_value;

  if (record_id_map->mgmt_hash_table_lookup((InkHashTableKey) name, &hash_value) != 0) {
    *id = ((RecordID *) hash_value)->index;
    *type = ((RecordID *) hash_value)->rtype;

    Record *rec;
    if ((rec = getRecord(*id, *type))) {
      *mgmt_type = rec->stype;
    } else
      *mgmt_type = INVALID;

    return true;
  }
  return false;
}                               /* End BaseRecords::id_typeofRecord */

void
BaseRecords::updateRecord(Record * rec)
{

  if (rec && rec->func) {
    ink_mutex_acquire(&mutex[rec->rtype]);
    switch (rec->stype) {
    case INK_COUNTER:{
        MgmtIntCounter tmp = 0;
        (*((RecordUpdateFunc) (rec->func))) (rec->opaque_token, &tmp);
        if (rec->data.counter_data != tmp) {
          rec->data.counter_data = tmp;
          rec->changed = true;
        }
        break;
      }
    case INK_INT:{
        MgmtInt tmp = 0;
        (*((RecordUpdateFunc) (rec->func))) (rec->opaque_token, &tmp);
        if (rec->data.int_data != tmp) {
          rec->data.int_data = tmp;
          rec->changed = true;
        }
        break;
      }
    case INK_LLONG:{
        MgmtLLong tmp = 0;
        (*((RecordUpdateFunc) (rec->func))) (rec->opaque_token, &tmp);
        if (rec->data.llong_data != tmp) {
          rec->data.llong_data = tmp;
          rec->changed = true;
        }
        break;
      }
    case INK_FLOAT:{
        MgmtFloat tmp = 0;
        (*((RecordUpdateFunc) (rec->func))) (rec->opaque_token, &tmp);
        if (rec->data.float_data != tmp) {
          rec->data.float_data = tmp;
          rec->changed = true;
        }
        break;
      }
    case INK_STRING:{
        MgmtString tmp = NULL;
        tmp = (MgmtString) (*((RecordUpdateFunc) (rec->func))) (rec->opaque_token, (tmp));
        if (!rec->data.string_data || strcmp(tmp, rec->data.string_data) != 0) {
          rec->data.string_data = tmp;
          rec->changed = true;
        }
        break;
      }
    default:
      ink_assert(false);
      break;
    }
    ink_mutex_release(&mutex[rec->rtype]);
  }
  return;
}                               /* End BaseRecords::updateRecord */


void
BaseRecords::updateRecords(RecordType type)
{
  Records *the_records;
  InkHashTableValue hash_value;
  UpdateLockFunc f_lock = f_update_lock;

  if (f_lock != NULL) {
    f_lock(UPDATE_LOCK_ACQUIRE);
  }

  if ((record_type_map->mgmt_hash_table_lookup((InkHashTableKey) type, &hash_value) != 0)) {
    the_records = (Records *) hash_value;
    for (int i = 0; i < the_records->num_recs; i++) {
      if (the_records->recs[i].func) {
        updateRecord(&the_records->recs[i]);
      }
    }
  }

  if (f_lock != NULL) {
    f_lock(UPDATE_LOCK_RELEASE);
  }

  return;
}                               /* End BaseRecords::updateRecords */


void
BaseRecords::notifyChangeLists(RecordType type, bool no_reset)
{
  Records *the_records;
  InkHashTableValue hash_value;

  if ((record_type_map->mgmt_hash_table_lookup((InkHashTableKey) type, &hash_value) != 0)) {
    the_records = (Records *) hash_value;

    // FIXME: we'll have deadlock problems if one of our callbacks decides to
    //        call readInteger, setInteger, etc. since each of those calls
    //        also tries to also aquire the mutex...
    ink_mutex_acquire(&mutex[type]);
    for (int i = 0; i < the_records->num_recs; i++) {

      if (the_records->recs[i].list && the_records->recs[i].changed) {

        for (CallBackList * list = the_records->recs[i].list; list; list = list->next) {

          switch (the_records->recs[i].stype) {
          case INK_COUNTER:
            (*((RecordChangeFunc) (list->func)))
              (list->opaque_token, &the_records->recs[i].data.counter_data);
            break;
          case INK_INT:
            (*((RecordChangeFunc) (list->func)))
              (list->opaque_token, &the_records->recs[i].data.int_data);
            break;
          case INK_LLONG:
            (*((RecordChangeFunc) (list->func)))
              (list->opaque_token, &the_records->recs[i].data.llong_data);
            break;
          case INK_FLOAT:
            (*((RecordChangeFunc) (list->func)))
              (list->opaque_token, &the_records->recs[i].data.float_data);
            break;
          case INK_STRING:
            (*((RecordChangeFunc) (list->func)))
              (list->opaque_token, the_records->recs[i].data.string_data);
            break;
          default:
            mgmt_fatal(stderr, "[BaseRecords::notifyChangeLists] Unknown type seen\n");

          }
        }
      }
      if (the_records->recs[i].changed && !no_reset) {
        the_records->recs[i].changed = false;
      }
    }
    ink_mutex_release(&mutex[type]);
  }
  return;
}                               /* End BaseRecords::notifyChangeLists */


bool
BaseRecords::syncPutRecord(Record * rec, char *pref, bool force_flush)
{
  bool ret = false;

  if (rec) {
    int res = 1;
//      char pname[1024];
    char *name;

    if (pref) {
// Removed by dg, since we are not multi-process now, we dont need the prefix
//          snprintf(pname, sizeof(pname), "%s-%s", pref, rec->name); /* Unique record entries in db */
//          name = pname;
      name = rec->name;
    } else {
      name = rec->name;
    }

    ink_mutex_acquire(&mutex[rec->rtype]);
    if (rec->changed || force_flush) {

      switch (rec->stype) {
      case INK_COUNTER:{
          res = record_db->mgmt_put(name, strlen(name), (void *) &rec->data.counter_data,
                                    sizeof(rec->data.counter_data));
          break;
        }
      case INK_INT:{
          res = record_db->mgmt_put(name, strlen(name), (void *) &rec->data.int_data, sizeof(rec->data.int_data));
          break;
        }
      case INK_LLONG:{
          res = record_db->mgmt_put(name, strlen(name), (void *) &rec->data.llong_data, sizeof(rec->data.llong_data));
          break;
        }
      case INK_FLOAT:{
          res = record_db->mgmt_put(name, strlen(name), (void *) &rec->data.float_data, sizeof(rec->data.float_data));
          break;
        }
      case INK_STRING:{
          if (rec->data.string_data) {
            res = record_db->mgmt_put(name, strlen(name), rec->data.string_data, strlen(rec->data.string_data) + 1);
          } else {
            res = record_db->mgmt_put(name, strlen(name), (char *) "NULL", strlen("NULL") + 1);
          }
          break;
        }
      default:
        ink_assert(false);
        break;
      }
      if (!res) {               /* Actually was updated */
        rec->changed = false;
        ret = true;
      } else {
        mgmt_elog(stderr, "[BaseRecords::syncPutRecord] Put failed! for: '%s' er: %d\n", rec->name, res);
      }
    }
    ink_mutex_release(&mutex[rec->rtype]);
  }
  return ret;
}                               /* End BaseRecords::syncPutRecord */


bool
BaseRecords::syncPutRecords(RecordType type, char *pref, bool force_flush)
{
  bool ret = false;
  Records *the_records;
  InkHashTableValue hash_value;

  if (!pref) {
    notifyChangeLists(type, true);
  }
  /* Notify anyone who needs it */
  if ((record_type_map->mgmt_hash_table_lookup((InkHashTableKey) type, &hash_value) != 0)) {
    the_records = (Records *) hash_value;
    if (record_db->mgmt_batch_open()) {
      for (int i = 0; i < the_records->num_recs; i++) {
        bool tmp = syncPutRecord(&the_records->recs[i], pref, force_flush);
        if (tmp) {
          ret = true;
        }
      }
      record_db->mgmt_batch_close();
    }
  } else {
    mgmt_log(stderr, "[BaseRecords::syncPutRecords] Invalid Record Type: %d\n", type);
  }
  return ret;
}                               /* End BaseRecords::syncPutRecords */


bool
BaseRecords::syncGetRecord(Record * rec, char *pref, bool ignore)
{

  if (rec) {
    int l, res;
//      char pname[1024];
    char *name;
    void *value;

    if (pref) {
// Removed by dg, since we are not multi-process we dont need the prefix right now
//          snprintf(pname, sizeof(pname), "%s-%s", pref, rec->name); /* Unique record entries in db */
//          name = pname;
      name = rec->name;
    } else {
      name = rec->name;
    }

    res = record_db->mgmt_get(name, strlen(name), &value, &l);
    if (!res) {
      ink_mutex_acquire(&mutex[rec->rtype]);
      switch (rec->stype) {
      case INK_COUNTER:{
          MgmtIntCounter tmp = *((MgmtIntCounter *) value);
          if (rec->data.counter_data != tmp) {
            rec->data.counter_data = tmp;
            if (!ignore)
              rec->changed = true;
          }
          break;
        }
      case INK_INT:{
          MgmtInt tmp = *((MgmtInt *) value);
          if (rec->data.int_data != tmp) {
            rec->data.int_data = tmp;
            if (!ignore)
              rec->changed = true;
          }
          break;
        }
      case INK_LLONG:{
          MgmtLLong tmp = *((MgmtLLong *) value);
          if (rec->data.llong_data != tmp) {
            rec->data.llong_data = tmp;
            if (!ignore)
              rec->changed = true;
          }
          break;
        }
      case INK_FLOAT:{
          MgmtFloat tmp = *((MgmtFloat *) value);
          if (tmp != rec->data.float_data) {
            rec->data.float_data = tmp;
            if (!ignore)
              rec->changed = true;
          }
          break;
        }
      case INK_STRING:{
          MgmtString tmp = (MgmtString) value;

          if (strcmp(tmp, "NULL") == 0) {
            if (rec->data.string_data) {
              xfree(rec->data.string_data);
              rec->data.string_data = NULL;
              if (!ignore)
                rec->changed = true;
            }
            break;
          }

          if (rec->data.string_data && strcmp(tmp, rec->data.string_data) == 0) {
            break;
          }

          if (rec->data.string_data) {
            xfree(rec->data.string_data);
          }
          size_t string_data_len = strlen((MgmtString) value) + 1;
          ink_assert((rec->data.string_data = (MgmtString) xmalloc(string_data_len)));
          ink_strncpy(rec->data.string_data, ((MgmtString) value), string_data_len);
          if (!ignore)
            rec->changed = true;
          break;
        }
      default:
        ink_assert(false);
        break;
      }
      ink_mutex_release(&mutex[rec->rtype]);
      xfree(value);
    } else {
      return false;
    }
    return true;
  }
  return false;
}                               /* End BaseRecords::syncGetRecord */


void
BaseRecords::syncGetRecords(RecordType type, char *pref, bool ignore)
{
  int sync_failure_count = 0;
  Records *the_records;
  InkHashTableValue hash_value;

  if ((record_type_map->mgmt_hash_table_lookup((InkHashTableKey) type, &hash_value) != 0)) {
    the_records = (Records *) hash_value;
    if (record_db->mgmt_batch_open()) {
      for (int i = 0; i < the_records->num_recs; i++) {
        if (!syncGetRecord(&the_records->recs[i], pref, ignore)) {
          sync_failure_count++;
        }
      }
      record_db->mgmt_batch_close();
    }

    if (sync_failure_count > 0) {
      mgmt_elog(stderr, "[BaseRecords::syncGetRecords] %d records failed to sync, will retry.\n", sync_failure_count);
    }
  } else {
    mgmt_log(stderr, "[BaseRecords::syncGetRecords] Invalid Record Type: %d\n", type);
  }
  return;
}                               /* End BaseRecords::syncGetRecords */


bool
BaseRecords::getExternalRecordValue(Record * rec, char *p)
{

  if (rec) {
    int l, res;
    char pname[1024], *name;
    void *value;

    if (p) {
      ink_snprintf(pname, sizeof(pname), "%s-%s", p, rec->name);
      name = pname;
    } else {
      name = rec->name;
    }

    res = record_db->mgmt_get(name, strlen(name), &value, &l);
    if (!res) {
      switch (rec->stype) {     /* Assume that caller has taken out mutex */
      case INK_COUNTER:
        rec->data.counter_data = *((MgmtIntCounter *) value);
        break;
      case INK_INT:
        rec->data.int_data = *((MgmtInt *) value);
        break;
      case INK_LLONG:
        rec->data.llong_data = *((MgmtLLong *) value);
        break;
      case INK_FLOAT:
        rec->data.float_data = *((MgmtFloat *) value);
        break;
      case INK_STRING:
        {
          if (rec->data.string_data) {
            xfree(rec->data.string_data);
          }
          size_t string_data_len = strlen(((MgmtString) value)) + 1;
          rec->data.string_data = (MgmtString) xmalloc(string_data_len);
          ink_assert(rec->data.string_data != NULL);
          ink_strncpy(rec->data.string_data, ((MgmtString) value), string_data_len);
        }
        break;
      default:
        ink_assert(false);
      }
      return true;
    }
    return false;
  }
  return false;
}                               /* End BaseRecords::getExternalRecordValue */


void
BaseRecords::removeExternalRecords(RecordType type, long p)
{

  char name[1024];
  InkHashTableValue hash_value;

  if (record_type_map->mgmt_hash_table_lookup((InkHashTableKey) type, &hash_value) != 0) {
    Records *recs = (Records *) hash_value;
    if (record_db->mgmt_batch_open()) {
      for (int i = 0; i < recs->num_recs; i++) {
        if (p != -1) {
          ink_snprintf(name, sizeof(name), "%ld-%s", p, recs->recs[i].name);
        } else {
          ink_snprintf(name, sizeof(name), "%ld-%s", pid, recs->recs[i].name);
        }
        record_db->mgmt_remove(name, strlen(name));
      }
      record_db->mgmt_batch_close();
    }
  } else {
    mgmt_log(stderr, "[BaseRecords::removeExternalRecords] Invalid record type seen\n");
  }
  return;
}                               /* End BaseRecords::removeExtRecords */


void
BaseRecords::printRecord(Record rec)
{

  fprintf(stderr, "\n\tID: index == '%d' rtype == '%d'\n", rec.id, rec.rtype);

  switch (rec.rtype) {
  case CONFIG:
    fprintf(stderr, "\tRecord Type: CONFIG\n");
    break;
  case PROCESS:
    fprintf(stderr, "\tRecord Type: PROCESS\n");
    break;
  case NODE:
    fprintf(stderr, "\tRecord Type: NODE\n");
    break;
  case CLUSTER:
    fprintf(stderr, "\tRecord Type: CLUSTER\n");
    break;
  case LOCAL:
    fprintf(stderr, "\tRecord Type: LOCAL\n");
    break;
  case PLUGIN:
    fprintf(stderr, "\tRecord Type: PLUGIN\n");
    break;
  default:
    ink_assert(false);
  }

  fprintf(stderr, "\tName: '%s' ", rec.name);
  switch (rec.stype) {
  case INK_COUNTER:
    fprintf(stderr, "\tType: COUNTER\n");
    ink_fprintf(stderr, "\tValue: '%lld'\n", rec.data.counter_data);
    break;
  case INK_INT:
    fprintf(stderr, "\tType: INT\n");
    ink_fprintf(stderr, "\tValue: '%lld'\n", rec.data.int_data);
    break;
  case INK_LLONG:
    fprintf(stderr, "\tType: LLONG\n");
    ink_fprintf(stderr, "\tValue: '%lld'\n", rec.data.llong_data);
    break;
  case INK_FLOAT:
    fprintf(stderr, "\tType: FLOAT\n");
    fprintf(stderr, "\tValue: '%f'\n", rec.data.float_data);
    break;
  case INK_STRING:
    fprintf(stderr, "\tType: STRING\n");
    if (rec.data.string_data) {
      fprintf(stderr, "\tValue: '%s'\n", rec.data.string_data);
    } else {
      fprintf(stderr, "\tValue: 'NULL'\n");
    }
    break;
  default:
    ink_assert(false);
  }

  if (rec.changed) {
    fprintf(stderr, "\tChanged: true ");
  } else {
    fprintf(stderr, "\tChanged: false ");
  }

  if (rec.func) {
    fprintf(stderr, "\tCB: registered\n");
  } else {
    fprintf(stderr, "\tCB: none\n");
  }

  return;
}                               /* End BaseRecords::printRecord */


void
BaseRecords::printRecords()
{

  InkHashTableEntry *hash_entry;
  InkHashTableIteratorState hash_iterator_state;

  fprintf(stderr, "-------- Begin Records Dump --------\n");
  for (hash_entry = record_type_map->mgmt_hash_table_iterator_first(&hash_iterator_state);
       hash_entry != NULL; hash_entry = record_type_map->mgmt_hash_table_iterator_next(&hash_iterator_state)) {

    Records *recs = (Records *) record_type_map->mgmt_hash_table_entry_value(hash_entry);
    for (int i = 0; i < recs->num_recs; i++) {
      printRecord(recs->recs[i]);
    }
  }
  fprintf(stderr, "\n-------- End Records Dump --------\n");
  return;
}                               /* End BaseRecords::printRecords */


void
BaseRecords::printRecords(RecordType type)
{

  InkHashTableValue hash_value;

  fprintf(stderr, "\n-------- printRecords: %d --------\n", type);
  if (record_type_map->mgmt_hash_table_lookup((InkHashTableKey) type, &hash_value) != 0) {
    Records *recs = (Records *) hash_value;
    for (int i = 0; i < recs->num_recs; i++) {
      printRecord(recs->recs[i]);
    }
  } else {
    mgmt_log(stderr, "[BaseRecords::printRecords] Invalid record type seen\n");
  }
  return;
}                               /* End BaseRecords::printRecords */


textBuffer *
BaseRecords::createRecordsFile(char *fname)
{
  textBuffer *newFile = NULL;
  InkHashTableValue hash_value;
  char *line, *buffer;
  char *last;

  ink_mutex_acquire(&record_textbuffer_lock);
  if (record_files->mgmt_hash_table_lookup((InkHashTableKey) fname, &hash_value) != 0) {
    textBuffer *buff = (textBuffer *) hash_value;
    size_t buffer_len = strlen(buff->bufPtr()) + 1;
    ink_assert((buffer = (char *) xmalloc(sizeof(char) * buffer_len)));
    ink_strncpy(buffer, buff->bufPtr(), buffer_len);
  } else {
    ink_mutex_release(&record_textbuffer_lock);
    return NULL;
  }
  ink_mutex_release(&record_textbuffer_lock);

  newFile = new textBuffer(strlen(buffer) * 2);

  line = ink_strtok_r(buffer, "\n", &last);
  do {
    int id;
    char str_val[1024];
    RecordType type;

    if (idofRecord(line, &id, &type)) {
      MgmtType rtype;

      switch (type) {
      case CONFIG:
        newFile->copyFrom("CONFIG ", strlen("CONFIG "));
        break;
      case PROCESS:
        newFile->copyFrom("PROCESS ", strlen("PROCESS "));
        break;
      case NODE:
        newFile->copyFrom("NODE ", strlen("NODE "));
        break;
      case CLUSTER:
        newFile->copyFrom("CLUSTER ", strlen("CLUSTER "));
        break;
      case LOCAL:
        newFile->copyFrom("LOCAL ", strlen("LOCAL "));
        break;
      default:
        ink_assert(false);
        break;
      }

      newFile->copyFrom(line, strlen(line));
      newFile->copyFrom(" ", 1);

      rtype = typeofRecord(id, type);
      switch (rtype) {
      case INK_COUNTER:{
          MgmtIntCounter tmp = readCounter(id, type);
          if (type == PROCESS)
            tmp = 0;
          newFile->copyFrom("COUNTER ", strlen("COUNTER "));
          ink_snprintf(str_val, sizeof(str_val), "%lld\n", tmp);
          newFile->copyFrom(str_val, strlen(str_val));
          break;
        }
      case INK_INT:{
          MgmtInt tmp = readInteger(id, type);
          if (type == PROCESS)
            tmp = 0;
          newFile->copyFrom("INT ", strlen("INT "));
          ink_snprintf(str_val, sizeof(str_val), "%lld\n", tmp);
          newFile->copyFrom(str_val, strlen(str_val));
          break;
        }
      case INK_LLONG:{
          MgmtLLong tmp = readLLong(id, type);
          if (type == PROCESS)
            tmp = 0;
          newFile->copyFrom("LLONG ", strlen("LLONG "));
          ink_snprintf(str_val, sizeof(str_val), "%lld\n", tmp);
          newFile->copyFrom(str_val, strlen(str_val));
          break;
        }
      case INK_FLOAT:{
          MgmtFloat tmp = readFloat(id, type);
          if (type == PROCESS)
            tmp = 0.0;
          newFile->copyFrom("FLOAT ", strlen("FLOAT "));
          snprintf(str_val, sizeof(str_val), "%.5f\n", tmp);
          newFile->copyFrom(str_val, strlen(str_val));
          break;
        }
      case INK_STRING:{
          MgmtString tmp = readString(id, type);
          newFile->copyFrom("STRING ", strlen("STRING "));
          if (tmp) {
            snprintf(str_val, sizeof(str_val), "%s\n", tmp);
            xfree(tmp);
          } else {
            ink_strncpy(str_val, "NULL\n", sizeof(str_val));
          }
          newFile->copyFrom(str_val, strlen(str_val));
          break;
        }
      default:
        ink_assert(false);
        break;
      }

    } else {
      newFile->copyFrom(line, strlen(line));
      newFile->copyFrom("\n", 1);
    }

  } while ((line = ink_strtok_r(NULL, "\n", &last)));
  if (buffer) {
    xfree(buffer);
  }
  return newFile;
}                               /* End BaseRecords::createRecordsFile */


void
BaseRecords::dumpReadRegisterReport()
{
  InkHashTableEntry *hash_entry;
  InkHashTableIteratorState hash_iterator_state;

  fprintf(stderr, "-------- Begin Report Dump --------\n");
  for (hash_entry = record_type_map->mgmt_hash_table_iterator_first(&hash_iterator_state);
       hash_entry != NULL; hash_entry = record_type_map->mgmt_hash_table_iterator_next(&hash_iterator_state)) {

    Records *recs = (Records *) record_type_map->mgmt_hash_table_entry_value(hash_entry);
    for (int i = 0; i < recs->num_recs; i++) {
      if (!recs->recs[i].read) {
        fprintf(stderr, "Record: '%s'  -- never read\n", recs->recs[i].name);
      }
      if (!recs->recs[i].list) {
        fprintf(stderr, "Record: '%s'  -- no change function registered\n", recs->recs[i].name);
      }
    }
  }
  fprintf(stderr, "\n-------- End Report Dump --------\n");
  return;
}                               /* End BaseRecords::dumpReadRegisterReport */

int
BaseRecords::getUpdateCount(RecordType type)
{
  if (type < MAX_RECORD_TYPE) {
    return updateCount[type];
  } else {
    return -1;
  }
}                               /* End BaseRecords::getUpdateCount */

// void BaseRecords::clearRecords(RecordType type)
//
//   Clears all records of the specified type
//
void
BaseRecords::clearRecords(RecordType type)
{

  Records *toClear;
  Record *current;

  // Find the records structure for the type we
  //  are to clear
  switch (type) {
  case CONFIG:
    toClear = &this->config_data;
    break;
  case PROCESS:
    toClear = &this->process_data;
    break;
  case NODE:
    toClear = &this->node_data;
    break;
  case CLUSTER:
    toClear = &this->cluster_data;
    break;
  case LOCAL:
    toClear = &this->local_data;
    break;
  case PLUGIN:
    toClear = &this->plugin_data;
    break;
  case MAX_RECORD_TYPE:
  default:
    mgmt_log(stderr, "[BaseRecords::clearRecords] Called with unknown record type: %d\n", type);
    return;
  }

  ink_mutex_acquire(&mutex[type]);

  // Loop through each record and clear it
  for (int i = 0; i < toClear->num_recs; i++) {
    current = toClear->recs + i;

    switch (current->stype) {
    case INK_INT:
      current->data.int_data = 0LL;
      break;
    case INK_LLONG:
      current->data.llong_data = 0LL;
      break;
    case INK_COUNTER:
      current->data.counter_data = 0LL;
      break;
    case INK_FLOAT:
      current->data.float_data = 0.0;
      break;
    case INK_STRING:
      xfree(current->data.string_data);
      current->data.string_data = NULL;
      break;
    case INVALID:
    default:
      mgmt_log(stderr, "[BaseRecords::clearRecords] Unknown data type encountered: %d\n", current->stype);
    }
    current->changed = true;
  }

  updateCount[type] += toClear->num_recs;

  ink_mutex_release(&mutex[type]);
}                               /* End BaseRecords::clearRecords */

//  void registerUpdateLockFunc(UpdateLockFunc func);
//
//     Sets the function used to wrap calls to
//       stat updates.  Registration of a lock
//       function allows for a cosistant view across
//       the statistics
//
void
BaseRecords::registerUpdateLockFunc(UpdateLockFunc func)
{

  // Use ink_atomic since memory write on the alpha
  //    are not atomic
  ink_atomic_swap_ptr(&this->f_update_lock, (void *) func);

}                               /* End BaseRecords::registerUpdateLockFunc */

/*
 * Special interface that does not take out the lock(assumes caller has) 
 */
MgmtIntCounter
BaseRecords::rl_readCounter(int id, RecordType type, bool * found)
{
  Record *rec;

  if (found)
    *found = false;
  if ((rec = getRecord(id, type)) && rec->stype == INK_COUNTER) {
    MgmtIntCounter ret;

    ret = rec->data.counter_data;
    rec->read = true;

    if (found)
      *found = true;
    return ret;
  }
  if (!found) {
    // die if the caller isn't checking 'found'
    mgmt_fatal(stderr, "[Config Error] Unable to find record id: %d type: %d\n", id, type);
  }
  return INVALID;
}                               /* End BaseRecords::rl_readCounter */


MgmtInt
BaseRecords::rl_readInteger(int id, RecordType type, bool * found)
{
  Record *rec;

  if (found)
    *found = false;
  if ((rec = getRecord(id, type)) && rec->stype == INK_INT) {
    MgmtInt ret;

    ret = rec->data.int_data;
    rec->read = true;

    if (found)
      *found = true;
    return ret;
  }
  if (!found) {
    // die if the caller isn't checking 'found'
    mgmt_fatal(stderr, "[Config Error] Unable to find record id: %d type: %d\n", id, type);
  }
  return INVALID;
}                               /* End BaseRecords::rl_readInteger */


MgmtLLong
BaseRecords::rl_readLLong(int id, RecordType type, bool * found)
{
  Record *rec;

  if (found)
    *found = false;
  if ((rec = getRecord(id, type)) && rec->stype == INK_LLONG) {
    MgmtLLong ret;

    ret = rec->data.llong_data;
    rec->read = true;

    if (found)
      *found = true;
    return ret;
  }
  if (!found) {
    // die if the caller isn't checking 'found'
    mgmt_fatal(stderr, "[Config Error] Unable to find record id: %d type: %d\n", id, type);
  }
  return INVALID;
}                               /* End BaseRecords::rl_readLLong */


MgmtFloat
BaseRecords::rl_readFloat(int id, RecordType type, bool * found)
{
  Record *rec;

  if (found)
    *found = false;
  if ((rec = getRecord(id, type)) && rec->stype == INK_FLOAT) {
    MgmtFloat ret;

    ret = rec->data.float_data;
    rec->read = true;

    if (found)
      *found = true;
    return ret;
  }
  if (!found) {
    // die if the caller isn't checking 'found'
    mgmt_fatal(stderr, "[Config Error] Unable to find record id: %d type: %d\n", id, type);
  }
  return INVALID;
}                               /* End BaseRecords::rl_readFloat */


MgmtString
BaseRecords::rl_readString(int id, RecordType type, bool * found)
{
  Record *rec;

  if (found)
    *found = false;
  if ((rec = getRecord(id, type)) && rec->stype == INK_STRING) {
    MgmtString ret;

    if (rec->data.string_data) {
      size_t ret_len = strlen(rec->data.string_data) + 1;
      ink_assert((ret = (MgmtString) xmalloc(ret_len)));
      ink_strncpy(ret, rec->data.string_data, ret_len);
    } else {
      ret = NULL;
    }
    rec->read = true;

    if (found)
      *found = true;
    return ret;
  }
  if (!found) {
    // die if the caller isn't checking 'found'
    mgmt_fatal(stderr, "[Config Error] Unable to find record id: %d type: %d\n", id, type);
  }
  return NULL;
}                               /* End BaseRecords::rl_readString */


MgmtIntCounter
BaseRecords::rl_readCounter(const char *name, bool * found)
{
  int id;
  RecordType type;

  if (found)
    *found = false;
  if (idofRecord(name, &id, &type)) {
    return (rl_readCounter(id, type, found));
  }
  if (!found) {
    // die if the caller isn't checking 'found'
    mgmt_fatal(stderr, "[Config Error] Unable to find record: %s\n", name);
  }
  return INVALID;
}                               /* End BaseRecords::rl_readCounter */


MgmtInt
BaseRecords::rl_readInteger(const char *name, bool * found)
{
  int id;
  RecordType type;

  if (found)
    *found = false;
  if (idofRecord(name, &id, &type)) {
    return (rl_readInteger(id, type, found));
  }
  if (!found) {
    // die if the caller isn't checking 'found'
    mgmt_fatal(stderr, "[Config Error] Unable to find record: %s\n", name);
  }
  return INVALID;
}                               /* End BaseRecords::rl_readInteger */


MgmtFloat
BaseRecords::rl_readFloat(const char *name, bool * found)
{
  int id;
  RecordType type;

  if (found)
    *found = false;
  if (idofRecord(name, &id, &type)) {
    return (rl_readFloat(id, type, found));
  }
  if (!found) {
    // die if the caller isn't checking 'found'
    mgmt_fatal(stderr, "[Config Error] Unable to find record: %s\n", name);
  }
  return INVALID;
}                               /* End BaseRecords::rl_readFloat */


MgmtString
BaseRecords::rl_readString(const char *name, bool * found)
{
  int id;
  RecordType type;

  if (found)
    *found = false;
  if (idofRecord(name, &id, &type)) {
    return (rl_readString(id, type, found));
  }
  if (!found) {
    // die if the caller isn't checking 'found'
    mgmt_fatal(stderr, "[Config Error] Unable to find record: %s\n", name);
  }
  return NULL;
}                               /* End BaseRecords::rl_readString */

Record *
BaseRecords::addPluginRecord(const char *name, MgmtType stype)
{
  if (plugin_data.num_recs < MAX_PLUGIN_RECORDS) {

    Record *new_rec = &(plugin_data.recs[plugin_data.num_recs]);
    RecordID *rec_id = (RecordID *) xmalloc(sizeof(RecordID));

    new_rec->id = plugin_data.num_recs;
    new_rec->rtype = PLUGIN;
    new_rec->name = xstrdup(name);
    new_rec->stype = stype;

    new_rec->changed = true;    // force the initial value to be flushed to mgmt_db
    new_rec->func = NULL;
    new_rec->opaque_token = NULL;
    new_rec->list = NULL;
    new_rec->read = false;

    rec_id->rtype = new_rec->rtype;
    rec_id->index = new_rec->id;
    record_id_map->mgmt_hash_table_insert((InkHashTableKey) new_rec->name, rec_id);

    plugin_data.num_recs++;

    return new_rec;
  }

  return NULL;
}

bool
BaseRecords::addPluginCounter(const char *name, MgmtIntCounter value)
{
  Record *new_rec;
  bool retval = false;

  ink_mutex_acquire(&mutex[PLUGIN]);

  if ((new_rec = addPluginRecord(name, INK_COUNTER)) != NULL) {
    new_rec->data.counter_data = value;
    retval = true;
  }

  ink_mutex_release(&mutex[PLUGIN]);

  return retval;
}

bool
BaseRecords::addPluginInteger(const char *name, MgmtInt value)
{
  Record *new_rec;
  bool retval = false;

  ink_mutex_acquire(&mutex[PLUGIN]);

  if ((new_rec = addPluginRecord(name, INK_INT)) != NULL) {
    new_rec->data.int_data = value;
    retval = true;
  }

  ink_mutex_release(&mutex[PLUGIN]);

  return retval;
}

bool
BaseRecords::addPluginLLong(const char *name, MgmtLLong value)
{
  Record *new_rec;
  bool retval = false;

  ink_mutex_acquire(&mutex[PLUGIN]);

  if ((new_rec = addPluginRecord(name, INK_LLONG)) != NULL) {
    new_rec->data.llong_data = value;
    retval = true;
  }

  ink_mutex_release(&mutex[PLUGIN]);

  return retval;
}

bool
BaseRecords::addPluginFloat(const char *name, MgmtFloat value)
{
  Record *new_rec;
  bool retval = false;

  ink_mutex_acquire(&mutex[PLUGIN]);

  if ((new_rec = addPluginRecord(name, INK_FLOAT)) != NULL) {
    new_rec->data.float_data = value;
    retval = true;
  }

  ink_mutex_release(&mutex[PLUGIN]);

  return retval;
}

bool
BaseRecords::addPluginString(const char *name, MgmtString value)
{
  Record *new_rec;
  bool retval = false;

  ink_mutex_acquire(&mutex[PLUGIN]);

  if ((new_rec = addPluginRecord(name, INK_STRING)) != NULL) {
    new_rec->data.string_data = xstrdup(value);
    retval = true;
  }

  ink_mutex_release(&mutex[PLUGIN]);

  return retval;
}
