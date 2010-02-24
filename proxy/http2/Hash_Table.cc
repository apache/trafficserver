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


#include "inktomi++.h"
#include "Hash_Table.h"
#include "HttpSM.h"
#include "HttpTransactCache.h"

/* void HashTable::createHashTable()
** 
** Allocates memory for the Buckets and initializes the mutexes
** 
*/
void
HashTable::createHashTable()
{
  for (int i = 0; i < NUM_BUCKETS; i++) {
    m_HashTable[i] = (Bucket *) xmalloc(sizeof(Bucket));
    m_HashTable[i]->First = NULL;
    ink_rwlock_init(&(m_HashTable[i]->BucketMutex));
  }
  initialized = true;
}


/* HashTable::~HashTable()
** 
** Deallocates memory for the Buckets and destroys the mutexes
** 
*/
HashTable::~HashTable()
{
  if (initialized) {
    for (int i = 0; i < NUM_BUCKETS; i++) {
      ink_rwlock_destroy(&(m_HashTable[i]->BucketMutex));
      xfree(m_HashTable[i]);
    }
  }
}

/* unsigned int HashTable::KeyToIndex(register char * string)
** 
** Creates an index from <string> which is the url to be used in the hash table
** Used before lookup(), insert() and remove()
** 
*/
unsigned int
HashTable::KeyToIndex(register char *string)
    /*register char *string;     String from which to compute hash value. */
{
  register unsigned int result;
  register int c;

  result = 0;
  while (1) {
    c = *string;
    string++;
    if (c == 0) {
      break;
    }
    result += (result << 3) + c;
  }
  return (result & (NUM_BUCKETS - 1));
}

/* RequestNode *HashTable::find_request(int index, char *url)
** 
** Searches Bucket[<index>] for <url> and returns the found RequestNode.
** If match is not found, returns NULL.
** 
*/
RequestNode *
HashTable::find_request(int index, char *url)
{
  const char *p1, *p2;
  RequestNode *iterator;
  for (iterator = m_HashTable[index]->First; iterator != NULL; iterator = iterator->next_request) {
    for (p1 = url, p2 = iterator->url->get_string();; p1++, p2++) {
      if (*p1 != *p2) {
        break;
      }
      if ((*p1) == 0) {
        return iterator;
      }
    }
  }
  return NULL;
}

/* HeaderAlternate *HashTable::lookup(int index, char *url, HTTPHdr *hdr)
** 
** Searches for <url> in Bucket[<index>] and then finds a match amoung the
** header alternates of that <url>. Returns the HeaderAlternate matched. If no
** match is found, returns NULL.
** 
*/
HeaderAlternate *
HashTable::lookup(int index, char *url, HTTPHdr * hdr)
{
  int match_found = 0;
  RequestNode *request = NULL;
  HeaderAlternate *iterator = NULL;

  ink_rwlock_rdlock(&(m_HashTable[index]->BucketMutex));

  if (NULL == (request = find_request(index, url))) {
    ink_rwlock_unlock(&(m_HashTable[index]->BucketMutex));
    return NULL;
  }

  for (iterator = request->Alternates; iterator != NULL; iterator = iterator->next) {
    if (match_Headers(iterator->hdr, hdr)) {
      match_found = 1;
      break;
    }
  }
  ink_rwlock_unlock(&(m_HashTable[index]->BucketMutex));
  return iterator;
}

/* int HashTable::insert(int index, char *url, HeaderAlternate *alternate)
** 
** Inserts <url> and <alternate> if <url> is not in the hash table,
** else, finds <url> in the hash table and adds <alternate> as a header
** alternate to that node. Waits for current readers of bucket to
** complete operation and then tries to acquire write lock on the bucket.
** If after HT_WRITER_MAX_RETRIES readers are still active,
** then returns 0, indicating that the alternate was not inserted to the hash table.
** 
*/
HeaderAlternate *
HashTable::insert(int index, HttpRequestData * url, bool revalidation)
{
  RequestNode *request = NULL;
  HeaderAlternate *iterator = NULL, *alternate = NULL;
  bool match_found = false;

  ink_rwlock_wrlock(&(m_HashTable[index]->BucketMutex));
  request = find_request(index, url->get_string());


  if (request != NULL) {
    for (iterator = request->Alternates; iterator != NULL; iterator = iterator->next) {
      if (match_Headers(iterator->hdr, url->hdr)) {
        match_found = true;
        break;
      }
    }

    if (iterator == NULL && match_found == false) {
      Debug("http_track", "[HashTable::insert] Adding alternate to node %p **\n", request);
      alternate = (HeaderAlternate *) xmalloc(sizeof(HeaderAlternate));
      alternate->hdr = url->hdr;
      alternate->prev = NULL;
      alternate->next = request->Alternates;
      alternate->revalidation_in_progress = revalidation;
      if (revalidation) {
        alternate->revalidation_start_time = ink_get_hrtime_internal();
      }
      request->Alternates->prev = alternate;
      request->Alternates = alternate;
    } else {
      ink_rwlock_unlock(&(m_HashTable[index]->BucketMutex));
      return NULL;
    }
  } else {
    request = (RequestNode *) xmalloc(sizeof(RequestNode));

    Debug("http_track", "[HashTable::insert] Adding a new node %p **\n", request);
    alternate = (HeaderAlternate *) xmalloc(sizeof(HeaderAlternate));
    alternate->hdr = url->hdr;
    alternate->prev = NULL;
    alternate->next = NULL;
    alternate->revalidation_in_progress = revalidation;
    alternate->response_noncacheable = false;
    if (revalidation) {
      alternate->revalidation_start_time = ink_get_hrtime_internal();
    }
    request->Alternates = alternate;
    request->next_request = m_HashTable[index]->First;
    request->prev_request = NULL;
    request->url = url;

    if (m_HashTable[index]->First) {
      m_HashTable[index]->First->prev_request = request;
    }
    m_HashTable[index]->First = request;
  }
  ink_rwlock_unlock(&(m_HashTable[index]->BucketMutex));
  return alternate;
}

/* int HashTable::remove(int index, char *url, HeaderAlternate *alternate)
** 
** Removes <url> and <alternate> if <alternate> is the only alternate of <url>,
** else, removes only <alternate> from that node. Waits for current readers of
** bucket to complete operation and then tries to acquire write lock on the bucket 
** 
*/
int
HashTable::remove(int index, char *url, HeaderAlternate * alternate)
{
  RequestNode *request = NULL;
  HeaderAlternate *iterator = NULL;
  bool match_found = false, only_alternate = false;
  int ret = 0;

  ink_rwlock_wrlock(&(m_HashTable[index]->BucketMutex));
  if (NULL == (request = find_request(index, url))) {
    Debug("http_track", "[HashTable::remove]'%s' not found! **\n", url);
    ink_rwlock_unlock(&(m_HashTable[index]->BucketMutex));
    return 0;
  }

  only_alternate = true;
  for (iterator = request->Alternates; iterator != NULL; iterator = iterator->next) {
    if (iterator == alternate) {
      match_found = true;
      break;
    }
    only_alternate = false;
  }

  if (match_found) {
    ret = 1;
    //Check if we are deleting the first entry in the bucket.
    if (m_HashTable[index]->First == request) {
      //Check if we are deleting the only alternate.
      if (only_alternate) {
        m_HashTable[index]->First = request->next_request;
        if (m_HashTable[index]->First) {
          m_HashTable[index]->First->prev_request = NULL;
        }

        xfree(request->Alternates);
        request->Alternates = NULL;
        request->url = NULL;
        xfree(request);
        request = NULL;
      }
      //We are not deleting the only alternate.
      else {
        if (alternate->prev) {
          alternate->prev->next = alternate->next;
        }
        if (alternate->next) {
          alternate->next->prev = alternate->prev;
        }
        xfree(alternate);
        alternate = NULL;
      }
    }
    //We are not deleting the first entry in the bucket.
    else {
      //Check if we are deleting the only alternate.
      if (only_alternate) {
        request->prev_request->next_request = request->next_request;
        if (request->next_request) {
          request->next_request->prev_request = request->prev_request;
        }
        xfree(request->Alternates);
        request->Alternates = NULL;
        request->url = NULL;
        xfree(request);
        request = NULL;
      }
      //We are not deleting the only alternate.
      else {
        if (alternate->prev) {
          alternate->prev->next = alternate->next;
        }
        if (alternate->next) {
          alternate->next->prev = alternate->prev;
        }
        xfree(alternate);
        alternate = NULL;
      }
    }
  }
  ink_rwlock_unlock(&(m_HashTable[index]->BucketMutex));
  return ret;
}

/* float HashTable::match_Headers( HTTPHdr * client_request, HTTPHdr * existing_request)
** 
** Api to match two HTTPHdr objects and return a 'quality of match'
** The logic has been customized from the functionality used by cache to find match among 
** alternates.
** 
*/
float
HashTable::match_Headers(HTTPHdr * client_request, HTTPHdr * existing_request)
{
  float q[4], Q;
  MIMEField *accept_field = NULL;
  MIMEField *accept_field_origin = NULL;
  MIMEField *content_field = NULL;

  q[1] = (q[2] = (q[3] = -2.0));        /* just to make debug output happy :) */

  // A NULL Accept or a NULL Content-Type field are perfect matches.
  content_field = existing_request->field_find(MIME_FIELD_ACCEPT, MIME_LEN_ACCEPT);

  accept_field = client_request->field_find(MIME_FIELD_ACCEPT, MIME_LEN_ACCEPT);
  q[0] = ((content_field = existing_request->field_find(MIME_FIELD_ACCEPT, MIME_LEN_ACCEPT)) != 0 &&
          (accept_field = client_request->field_find(MIME_FIELD_ACCEPT, MIME_LEN_ACCEPT)) != 0) ?
    HttpTransactCache::calculate_quality_of_accept_match(accept_field, content_field) : 1.0;

  if (q[0] >= 0.0) {
    // content_field lookup is same as above
    content_field = existing_request->field_find(MIME_FIELD_ACCEPT_CHARSET, MIME_LEN_ACCEPT_CHARSET);
    q[1] = (content_field &&
            ((accept_field =
              client_request->field_find(MIME_FIELD_ACCEPT_CHARSET,
                                         MIME_LEN_ACCEPT_CHARSET)) !=
             0)) ? HttpTransactCache::calculate_quality_of_accept_charset_match(accept_field, content_field) : 1.0;

    if (q[1] >= 0.0) {          /////////////////////
      // Accept-Encoding //
      /////////////////////
      accept_field = client_request->field_find(MIME_FIELD_ACCEPT_ENCODING, MIME_LEN_ACCEPT_ENCODING);
      content_field = existing_request->field_find(MIME_FIELD_ACCEPT_ENCODING, MIME_LEN_ACCEPT_ENCODING);
      //accept_field_origin = obj_client_request->field_find(MIME_FIELD_ACCEPT_ENCODING, MIME_LEN_ACCEPT_ENCODING);
      accept_field_origin = NULL;
      q[2] = (accept_field ||
              content_field) ? HttpTransactCache::calculate_quality_of_accept_encoding_match(accept_field,
                                                                                             content_field,
                                                                                             accept_field_origin) : 1.0;
    }

    if (q[2] >= 0.0) {          /////////////////////
      // Accept-Language //
      /////////////////////
      if ((accept_field = client_request->field_find(MIME_FIELD_ACCEPT_LANGUAGE, MIME_LEN_ACCEPT_LANGUAGE)) != 0) {
        content_field = existing_request->field_find(MIME_FIELD_ACCEPT_LANGUAGE, MIME_LEN_ACCEPT_LANGUAGE);
        q[3] = HttpTransactCache::calculate_quality_of_accept_language_match(accept_field, content_field);
      } else
        q[3] = 1.0;
    }
  }
  /////////////////////////////////////////////////////////////
  // final quality is minimum Q, or -1, if some match failed //
  /////////////////////////////////////////////////////////////
  Q = ((q[0] < 0) || (q[1] < 0) || (q[2] < 0) || (q[3] < 0)) ? -1.0 : q[0] * q[1] * q[2] * q[3];
  return (Q);
}

/* HeaderAlternate *HashTable::update_revalidation_start_time(int index, char *url, HTTPHdr *hdr)
** 
** Updates the revalidation_start_time for a particular request url.Acquires the lock for
** updating the same
** 
*/
void
HashTable::update_revalidation_start_time(int index, HeaderAlternate * alternate)
{
  ink_rwlock_wrlock(&(m_HashTable[index]->BucketMutex));
  alternate->revalidation_start_time = ink_get_hrtime_internal();
  ink_rwlock_unlock(&(m_HashTable[index]->BucketMutex));
}

/* HeaderAlternate *HashTable::set_response_noncacheable(int index, char *url, HTTPHdr *hdr)**
** Updates if the response is non cacheable
**
*/

void
HashTable::set_response_noncacheable(int index, HeaderAlternate * alternate)
{
  ink_rwlock_wrlock(&(m_HashTable[index]->BucketMutex));
  alternate->response_noncacheable = true;
  ink_rwlock_unlock(&(m_HashTable[index]->BucketMutex));
}
