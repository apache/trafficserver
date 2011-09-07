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

#include "StringHash.h"
#include "Error.h"


// ===============================================================================
//                              StringHash
// ===============================================================================

StringHash::StringHash(int _hash_size, bool _ignore_case)
{
  ignore_case = _ignore_case;
  hash_mask = 0;
  hash_mask_size = 0;
  max_hit_level = 0;

  if ((hash_size = _hash_size) < STRINGHASH_MIN_TBL_SIZE)
    _hash_size = (hash_size = STRINGHASH_MIN_TBL_SIZE);
  if (_hash_size > STRINGHASH_MAX_TBL_SIZE)
    _hash_size = (hash_size = STRINGHASH_MAX_TBL_SIZE);
  while (_hash_size) {
    _hash_size >>= 1;
    hash_mask <<= 1;
    hash_mask |= 1;
    hash_mask_size++;
  }
  hash_mask >>= 1;
  hash_mask_size--;

  if (((hash_size + hash_mask) & ~hash_mask) != hash_size) {
    hash_size = (hash_size & ~hash_mask) << 1;
    hash_mask = (hash_mask << 1) + 1;
    hash_mask_size++;
  }

  hash = (StringHashEntry **)ats_malloc(hash_size * sizeof(StringHashEntry *));
  memset(hash, 0, hash_size * sizeof(StringHashEntry *));
  //printf("StringHash::StringHash  hash = 0x%0X, mask = 0x%0lX, hash_mask_size = %d\n",hash_size,(long)hash_mask,hash_mask_size);
}

StringHash::~StringHash()
{
  StringHashEntry *he;
  if (likely(hash)) {
    for (int i = 0; i < hash_size; i++) {
      while ((he = hash[i]) != 0) {
        hash[i] = he->next;
        delete he;
      }
    }
    hash = (StringHashEntry **) xfree_null(hash);
  }
}

unsigned long
StringHash::csum_calc(void *_buf, int size)
{
  char *buf;
  register unsigned short csum = 0;

  if (likely((buf = (char *) _buf) != 0 && size > 0)) {
    register int c;
    int oddf = size & 1;
    int oddf2 = size & 2;
    size >>= 2;

    while (size--) {
      csum <<= ((c = *buf++) & 1);
      csum += c;
      csum <<= ((c = *buf++) & 1);
      csum += c;
      csum <<= ((c = *buf++) & 1);
      csum += c;
      csum <<= ((c = *buf++) & 1);
      csum += c;
    }
    if (oddf2) {
      csum <<= ((c = *buf++) & 1);
      csum += c;
      csum <<= ((c = *buf++) & 1);
      csum += c;
    }
    if (oddf) {
      csum <<= ((c = *buf) & 1);
      csum += c;
    }
  }
  return (unsigned long) csum;
}

StringHashEntry *
StringHash::find_or_add(void *_ptr, const char *_str, int _strsize)
{
  StringHashEntry *he, **hep;
  int htid;
  unsigned long hid;
  char tbuf[1024 * 2], *tbufp, *tbuf_alloc = NULL;

  if (!_str) {
    _str = (const char *) "";
    _strsize = 0;
  }
  if (_strsize < 0)
    _strsize = strlen(_str);

  if (ignore_case) {
    if (unlikely(_strsize >= (int) sizeof(tbuf))) {
      tbufp = (tbuf_alloc = (char *)ats_malloc(_strsize + 1));
      tbufp = &tbuf[0];
      _strsize = (int) (sizeof(tbuf) - 1);
    } else
      tbufp = &tbuf[0];
    for (htid = 0; htid < _strsize; htid++) {
      if ((tbufp[htid] = _str[htid]) >= 'A' && tbufp[htid] <= 'Z') {
        tbufp[htid] = (tbufp[htid] + ('a' - 'A'));
      }
    }
    tbufp[htid] = 0;
    _str = (const char *) tbufp;
  }

  htid = (int) ((hid = csum_calc((void *) _str, _strsize)) & hash_mask);

  for (he = hash[htid]; he; he = he->next) {
    if (he->hashid == hid && he->strsize == _strsize && (!_strsize || !memcmp(he->str, _str, _strsize))) {      //printf("StringHash::find_or_add - find it! In table id %d - \"%s\" - hid 0x%0lx\n",htid,he->str,hid);
      break;
    }
    //printf("StringHash::find_or_add - search - We have hash table hit for hash table id %d - \"%s\" - not matched\n",htid,he->str);
  }
  if (!he && _ptr) {
    he = NEW(new StringHashEntry());
    if (likely(he->setstr(_str, _strsize))) {
      int _max_hit_level = 0;
      he->hashid = hid;
      he->hash_table_index = htid;
      //printf("StringHash::find_or_add - start lookup for insert new hash id 0x%0lx, hash table id %d\n",hid,htid);
      for (hep = &hash[htid]; *hep; hep = &((*hep)->next)) {    //printf("StringHash::find_or_add - add - We have hash table hit for hash table id %d - \"%s\"\n",htid,(*hep)->str);
        _max_hit_level++;
      }
      (*hep = he)->ptr = _ptr;
      if (_max_hit_level > max_hit_level)
        max_hit_level = _max_hit_level;
    } else {
      delete he;
      he = NULL;
    }
  }
  if (unlikely(tbuf_alloc))
    xfree(tbuf_alloc);
  return he;
}

// ===============================================================================
//                            StringHashEntry
// ===============================================================================
StringHashEntry & StringHashEntry::clean()
{
  if (str)
    str = (char *) xfree_null(str);
  hash_table_index = (strsize = 0);
  hashid = 0;
  return *this;
}

const char *
StringHashEntry::setstr(const char *_str, int _strsize)
{
  clean();
  if (!_str) {
    _str = (const char *) "";
    _strsize = 0;
  }
  if ((strsize = _strsize) < 0)
    strsize = strlen(_str);
  str = (char *)ats_malloc(strsize + 1);
  if (strsize)
    memcpy(str, _str, strsize);
  str[strsize] = 0;

  return str;
}
