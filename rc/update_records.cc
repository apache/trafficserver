/** @file

  Updates a 'records.config' file

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

  @section details Details

  The program takes three arguments
    -# List of config name-value pairs to set, e.g. "proxy.config.setting 10"
    -# The default records.config file/template
    -# The filename to write the new/upgraded records.config to

  The program compares the files with the internally defined records
  in RecordsConfig.cc and determine which records should be written
  into the new records.config file.  For example, deprecated records
  specified as input should not be migrated to the new config file.
  This is useful for doing upgrades.

  Recall that in Tomcat moving forward, records.config no longer contains
  a comprehensive set of all configs in the system.  Also, all statistics
  have been removed from records.config.  lm.config has been deprecated,
  and the remaining config variables in lm.config have been merged into
  records.config.

 */

#include "ink_config.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#include "SimpleTokenizer.h"
#include "RecordsConfig.h"

//-------------------------------------------------------------------------
// structs
//-------------------------------------------------------------------------

struct ConfigEntry
{
  const char *type;
  const char *name;
  const char *value_type;
  const char *value;
  ConfigEntry *next;
};

struct RecordRenameMapElement
{
  const char *old_name;
  const char *new_name;
};

//-------------------------------------------------------------------------
// globals
//-------------------------------------------------------------------------

RecordRenameMapElement RecordRenameMap[] = {

  // 3.x to 4.x variable renamings
  {"proxy.config.log2.separate_rni_logs", "proxy.config.log2.separate_mixt_logs"},

  // 4.x to 5.x variable renamings
  {"proxy.config.cluster.type", "proxy.local.cluster.type"},

  // place future variables below ^_^

  {NULL, NULL}                  // array terminator
};

// blacklist these records (upgrade only!)
const char *RecordBlackList[] = {
  "proxy.config.socks.socks_version",
  NULL                          // array terminator
};

char *a_buf = 0;
char *b_buf = 0;
char *b_ht_buf = 0;

InkHashTable *b_ht = 0;
InkHashTable *rec_ht = 0;
InkHashTable *rename_ht = 0;
InkHashTable *blacklist_ht = 0;
InkHashTable *modify_ht = 0;
ConfigEntry *modify_list_head = 0;
ConfigEntry *modify_list_tail = 0;

const char *null_str = "NULL";
const char *config_str = "CONFIG";
const char *local_str = "LOCAL";
const char *plugin_str = "PLUGIN";
const char *int_str = "INT";
const char *llong_str = "LLONG";
const char *string_str = "STRING";
const char *float_str = "FLOAT";
const char *counter_str = "COUNTER";

#define ADMIN_PASSWD_LEN 23
const char *admin_passwd_rec_name = "proxy.config.admin.admin_password";
char admin_passwd[ADMIN_PASSWD_LEN + 1];

bool upgrade = false;

//-------------------------------------------------------------------------
// import_file
//-------------------------------------------------------------------------

char *
import_file(char *fname)
{

  int fd, fsize;
  char *fbuf;
  struct stat stat_buf;

#ifndef _WIN32
  if ((fd =::open(fname, O_RDWR)) < 0) {
#else
  if ((fd =::open(fname, O_RDWR | O_BINARY)) < 0) {
#endif
    printf("[Error] could not open '%s'\n", fname);
    return 0;
  }
  ::fstat(fd, &stat_buf);
  fsize = stat_buf.st_size;
  fbuf = (char *) malloc(fsize + 1);
  if (::read(fd, fbuf, fsize) != fsize) {
    free(fbuf);
    printf("[Error] could not read '%s'\n", fname);
    return 0;
  }
  fbuf[fsize] = '\0';
  ::close(fd);

  return fbuf;

}

//-------------------------------------------------------------------------
// utils
//-------------------------------------------------------------------------

bool
is_string_whitespace(char *p)
{
  while (*p != '\0') {
    if ((*p != ' ') && (*p != '\t'))
      return false;
    p++;
  }
  return true;
}

char *
terminate_end_of_line(char *q)
{
  while ((*q != '\r') && (*q != '\n') && (*q != '\0'))
    q++;
  if (*q == '\r') {
    *q = '\0';
    q++;
  }
  if (*q == '\n') {
    *q = '\0';
    q++;
  }
  return q;
}

char *
clear_leading_whitespace(char *p)
{
  while ((*p == ' ') || (*p == '\t'))
    p++;
  return p;
}

void
clear_trailing_whitespace(char *p)
{
  if (*p == '\0')
    return;
  while (*p != '\0')
    p++;
  p--;
  while ((*p == ' ') || (*p == '\t')) {
    *p = '\0';
    p--;
  }
}


//-------------------------------------------------------------------------
// generate_b_ht_from_b_ht_buf
//-------------------------------------------------------------------------

int
generate_b_ht_from_b_ht_buf()
{

  char *p, *q, *p_copy;
  ConfigEntry *ce;
  SimpleTokenizer st(' ', SimpleTokenizer::OVERWRITE_INPUT_STRING);

  p = q = b_ht_buf;
  while (true) {
    if (*q == '\0')
      break;
    p = q;
    q = terminate_end_of_line(q);
    if (is_string_whitespace(p)) {
      continue;
    } else {
      p = clear_leading_whitespace(p);
      if (*p == '#')
        continue;
      clear_trailing_whitespace(p);
    }
    // 'p' points to start of this line, 'q' points to start of next
    if ((ce = (ConfigEntry *) malloc(sizeof(ConfigEntry))) == 0) {
      printf("[Error] Could not allocate memory\n");
      return -1;
    }
    if ((p_copy = strdup(p)) == 0) {
      printf("[Error] Could not allocate memory\n");
      if (ce)
        free(ce);
      return -1;
    }
    st.setString(p);
    ce->type = st.getNext();
    ce->name = st.getNext();
    ce->value_type = st.getNext();
    ce->value = st.peekAtRestOfString();
    ce->next = 0;
    if (*(ce->value) == '\0') {
      // coverity[noescape]
      printf("[Error] Could not parse; possible corruption in " "default records.config '%s'\n", p_copy);
      if (p_copy)
        free(p_copy);
      if (ce)
        free(ce);
      return -1;
    }
    while (*(ce->value) == ' ')
      (ce->value)++;
    ink_hash_table_insert(b_ht, ce->name, ce);
    free(p_copy);
  }
  return 0;

}

//-------------------------------------------------------------------------
// gererate_rec_ht_from_RecordsConfig
//-------------------------------------------------------------------------

int
generate_rec_ht_from_RecordsConfig()
{

  ConfigEntry *ce = 0;
  RecordElement *re = RecordsConfig;

  while (re->value_type != INVALID) {
    if ((ce = (ConfigEntry *) malloc(sizeof(ConfigEntry))) == 0) {
      printf("[Error] Could not allocate memory\n");
      return -1;
    }
    switch (re->type) {
    case CONFIG:
      ce->type = config_str;
      break;
    case LOCAL:
      ce->type = local_str;
      break;
    case PLUGIN:
      ce->type = plugin_str;
      break;
    default:
      // statistic record, ignore it...
      free(ce);
      re++;
      continue;
    }
    ce->name = re->name;
    switch (re->value_type) {
    case INK_INT:
      ce->value_type = int_str;
      break;
    case INK_LLONG:
      ce->value_type = llong_str;
      break;
    case INK_FLOAT:
      ce->value_type = float_str;
      break;
    case INK_STRING:
      ce->value_type = string_str;
      break;
    case INK_COUNTER:
      ce->value_type = counter_str;
      break;
    default:                   /* nop; make gcc happy */ ;
    };                          /* switch (re->value_type) */
    ce->value = (re->value == NULL) ? null_str : re->value;
    ce->next = 0;
    ink_hash_table_insert(rec_ht, ce->name, ce);
    re++;
  }
  return 0;

}

//-------------------------------------------------------------------------
// generate_rename_ht_from_RecordRenameMap
//-------------------------------------------------------------------------

int
generate_rename_ht_from_RecordRenameMap()
{

  RecordRenameMapElement *rrme = RecordRenameMap;

  while (rrme->old_name != NULL) {
    ink_hash_table_insert(rename_ht, rrme->old_name, (void*)rrme->new_name);
    rrme++;
  }
  return 0;

}

//-------------------------------------------------------------------------
// generate_blacklist_ht_from_RecordBlackList
//-------------------------------------------------------------------------

int
generate_blacklist_ht_from_RecordBlackList()
{

  const char **p = RecordBlackList;

  while (*p != NULL) {
    ink_hash_table_insert(blacklist_ht, *p, NULL);
    p++;
  }
  return 0;

}

//-------------------------------------------------------------------------
// find_config_updates
//-------------------------------------------------------------------------

int
find_config_updates()
{

  char *p, *q, *p_copy;
  char *a_name, *a_value;
  ConfigEntry *b_ce, *rec_ce, *ce;
  SimpleTokenizer st(' ', SimpleTokenizer::OVERWRITE_INPUT_STRING);
  bool add_to_modify_ht, add_to_modify_list;

  // walk thru a_buf and populate modify_ht and modify_list
  p = q = a_buf;
  while (true) {
    if (*q == '\0')
      break;
    p = q;
    q = terminate_end_of_line(q);
    if (is_string_whitespace(p)) {
      continue;
    } else {
      p = clear_leading_whitespace(p);
      if (*p == '#')
        continue;
      clear_trailing_whitespace(p);
    }
    // 'p' points to start of this line, 'q' points to start of next
    if ((p_copy = strdup(p)) == 0) {
      printf("[Error] Could not allocate memory\n");
      return -1;
    }

    st.setString(p);
    a_name = st.getNext();
    a_value = st.peekAtRestOfString();
    if (*a_value == '\0') {
      printf("[Warning] Could not parse; possible corruption in " "previous records.config '%s'\n", p_copy);
      free(p_copy);
      continue;
    }
    while (*a_value == ' ')
      a_value++;

    add_to_modify_ht = add_to_modify_list = false;

    // is the 'a' record one of our renamed records?
    if (ink_hash_table_isbound(rename_ht, a_name)) {
      char *a_name_new;
      ink_hash_table_lookup(rename_ht, a_name, (void **) &a_name_new);
      // rename 'a' to the new name
      //printf("[Note] Mapping from '%s' to '%s'\n", a_name, a_name_new);
      a_name = a_name_new;
    }
    // make sure 'a' isn't blacklisted (upgrade only!)
    if (!upgrade || (upgrade && !ink_hash_table_isbound(blacklist_ht, a_name))) {
      // is the 'a' record defined in RecordsConfig.cc?
      if (ink_hash_table_lookup(rec_ht, a_name, (void **) &rec_ce)) {
        // is the 'a' record defined in the 'b' config file?
        if (ink_hash_table_lookup(b_ht, a_name, (void **) &b_ce)) {
          // are the 'a' record-value and the 'b' record-value different?
          if (strcmp(b_ce->value, a_value) != 0) {
            // add this record to our modify_ht
            add_to_modify_ht = true;
          }
        } else {                // the 'a' record is defined in RecordsConfig.cc only
          // are the 'a' record-value and the RecordsConfig.cc-value different?
          if (strcmp(rec_ce->value, a_value) != 0) {
            // add this record to our modify_list
            add_to_modify_list = true;
          }
        }
      } else {
        // who cares?  drop the old record since it's no longer defined in
        // the current system.
      }
    }

    if (add_to_modify_ht || add_to_modify_list) {
      if ((ce = (ConfigEntry *) malloc(sizeof(ConfigEntry))) == 0) {
        printf("[Error] Could not allocate memory\n");
        if (p_copy)
          free(p_copy);
        return -1;
      }
      ce->type = rec_ce->type;
      ce->name = a_name;
      ce->value_type = rec_ce->value_type;
      ce->value = a_value;
      ce->next = 0;
      // admin password check
      if ((strcmp(ce->name, admin_passwd_rec_name) == 0) && !upgrade) {
        INK_DIGEST_CTX context;
        char md5[16];
        char md5_str[33];
        ink_code_incr_md5_init(&context);
        ink_code_incr_md5_update(&context, ce->value, strlen(ce->value));
        ink_code_incr_md5_final(md5, &context);
        ink_code_md5_stringify(md5_str, sizeof(md5_str), md5);
        strncpy(admin_passwd, md5_str, ADMIN_PASSWD_LEN);
        ce->value = admin_passwd;
      }
      // do the updates
      if (add_to_modify_ht) {
        ink_hash_table_insert(modify_ht, ce->name, ce);
      }
      if (add_to_modify_list) {
        if (modify_list_head == 0) {
          modify_list_head = modify_list_tail = ce;
        } else {
          modify_list_tail->next = ce;
          modify_list_tail = ce;
        }
      }
    }

    free(p_copy);

  }

  return 0;

}

//-------------------------------------------------------------------------
// generate_new_config
//-------------------------------------------------------------------------

int
generate_new_config(char *fname)
{

  FILE *fp;
  char *p, *q, *t, *p_copy;
  char *b_type, *b_name, *b_value_type;
  const char *b_value;
  SimpleTokenizer st(' ', SimpleTokenizer::OVERWRITE_INPUT_STRING);
  ConfigEntry *ce;

  if ((fp = fopen(fname, "w")) == NULL) {
    printf("[Error] could not open '%s'\n", fname);
    return -1;
  }
  // walk through 'b' and modify records if necessary when
  // writing to 'c'
  p = q = b_buf;
  while (true) {
    if (*q == '\0')
      break;
    p = q;
    q = terminate_end_of_line(q);
    if (is_string_whitespace(p)) {
      fprintf(fp, "%s\n", p);
      continue;
    } else {
      t = clear_leading_whitespace(p);
      if (*t == '#') {
        fprintf(fp, "%s\n", p);
        continue;
      }
      p = t;
      clear_trailing_whitespace(p);
    }
    // 'p' points to start of this line, 'q' points to start of next
    if ((p_copy = strdup(p)) == 0) {
      printf("[Error] Could not allocate memory\n");
      // coverity[leaked_storage]
      return -1;
    }
    st.setString(p);
    b_type = st.getNext();
    b_name = st.getNext();
    b_value_type = st.getNext();
    b_value = st.peekAtRestOfString();
    if (*b_value == '\0') {
      printf("[Error] Could not parse; possible corruption in " "default records.config '%s'\n", p_copy);
      if (p_copy)
        free(p_copy);
      return -1;
    }
    while (*b_value == ' ')
      b_value++;
    if (ink_hash_table_lookup(modify_ht, b_name, (void **) &ce)) {
      b_value = ce->value;
    }
    fprintf(fp, "%s %s %s %s\n", b_type, b_name, b_value_type, b_value);
    free(p_copy);
  }

  // append additional records to 'c'
  ce = modify_list_head;
  while (ce) {
    fprintf(fp, "%s %s %s %s\n", ce->type, ce->name, ce->value_type, ce->value);
    ce = ce->next;
  }

  fclose(fp);

  return 0;
}

/**
  Main function.

  @param argc number of arguments
  @param argv array of argument strings

 */
int
main(int argc, char *argv[])
{

  int ret = 0;
  char *upgrade_env = NULL;

  if (argc != 4) {
    printf("[Error] Inavlid number of arguments passed " "to 'update_records'\n");
    goto lerror;
  }
  // are we in upgrade mode?
  upgrade_env = getenv("Upgrade");
  upgrade = (upgrade_env && (strcmp(upgrade_env, "true") == 0));

  if ((a_buf = import_file(argv[1])) == 0)
    goto lerror;
  if ((b_buf = import_file(argv[2])) == 0)
    goto lerror;
  if ((b_ht_buf = strdup(b_buf)) == 0) {
    printf("[Error] Could not allocate memory\n");
    goto lerror;
  }
  // create hash tables
  b_ht = ink_hash_table_create(InkHashTableKeyType_String);
  rec_ht = ink_hash_table_create(InkHashTableKeyType_String);
  rename_ht = ink_hash_table_create(InkHashTableKeyType_String);
  blacklist_ht = ink_hash_table_create(InkHashTableKeyType_String);
  modify_ht = ink_hash_table_create(InkHashTableKeyType_String);
  if (!(b_ht && rec_ht && modify_ht)) {
    printf("[Error] Could not create hash tables\n");
    goto lerror;
  }
  modify_list_head = 0;
  modify_list_tail = 0;

  // compute!
  if (generate_b_ht_from_b_ht_buf() < 0)
    goto lerror;
  if (generate_rec_ht_from_RecordsConfig() < 0)
    goto lerror;
  if (generate_rename_ht_from_RecordRenameMap() < 0)
    goto lerror;
  if (generate_blacklist_ht_from_RecordBlackList() < 0)
    goto lerror;
  if (find_config_updates() < 0)
    goto lerror;
  if (generate_new_config(argv[3]) < 0)
    goto lerror;

  // success!
  goto ldone;

lerror:
  ret = -1;

ldone:

  // clean-up memory
  if (b_ht)
    ink_hash_table_destroy_and_free_values(b_ht);
  if (rec_ht)
    ink_hash_table_destroy_and_free_values(rec_ht);
  if (rename_ht)
    ink_hash_table_destroy(rename_ht);
  if (blacklist_ht)
    ink_hash_table_destroy(blacklist_ht);
  if (modify_ht)
    ink_hash_table_destroy_and_free_values(modify_ht);
  while (modify_list_head) {
    ConfigEntry *ce = modify_list_head;
    modify_list_head = modify_list_head->next;
    free(ce);
  }
  if (a_buf)
    free(a_buf);
  if (b_buf)
    free(b_buf);
  if (b_ht_buf)
    free(b_ht_buf);

  return ret;

}

#ifdef _WIN32

int WINAPI
WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
  return main(__argc, __argv);
}

#endif // _WIN32
