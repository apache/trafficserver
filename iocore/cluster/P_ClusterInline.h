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

/****************************************************************************

  ClusterInline.h
****************************************************************************/

#ifndef __P_CLUSTERINLINE_H__
#define __P_CLUSTERINLINE_H__
#include "P_ClusterCacheInternal.h"
#include "P_CacheInternal.h"
#include "P_ClusterHandler.h"

inline Action *
Cluster_lookup(Continuation *cont, const CacheKey *key, CacheFragType frag_type, const char *hostname, int host_len)
{
  // Try to send remote, if not possible, handle locally
  Action *retAct;
  ClusterMachine *m = cluster_machine_at_depth(cache_hash(*key));
  if (m && !clusterProcessor.disable_remote_cluster_ops(m)) {
    CacheContinuation *cc = CacheContinuation::cacheContAllocator_alloc();
    cc->action            = cont;
    cc->mutex             = cont->mutex;
    retAct                = CacheContinuation::do_remote_lookup(cont, key, cc, frag_type, hostname, host_len);
    if (retAct) {
      return retAct;
    } else {
      // not remote, do local lookup
      CacheContinuation::cacheContAllocator_free(cc);
      return (Action *)NULL;
    }
  } else {
    Action a;
    a = cont;
    return CacheContinuation::callback_failure(&a, CACHE_EVENT_LOOKUP_FAILED, 0);
  }
  return (Action *)NULL;
}

inline Action *
Cluster_read(ClusterMachine *owner_machine, int opcode, Continuation *cont, MIOBuffer *buf, CacheHTTPHdr *request,
             CacheLookupHttpConfig *params, const CacheKey *key, time_t pin_in_cache, CacheFragType frag_type, const char *hostname,
             int hostlen)
{
  (void)params;
  if (clusterProcessor.disable_remote_cluster_ops(owner_machine)) {
    Action a;
    a = cont;
    return CacheContinuation::callback_failure(&a, CACHE_EVENT_OPEN_READ_FAILED, 0);
  }
  int vers = CacheOpMsg_long::protoToVersion(owner_machine->msg_proto_major);
  int flen;
  int len = 0;
  int res = 0;
  char *msg;
  char *data;

  if (vers == CacheOpMsg_long::CACHE_OP_LONG_MESSAGE_VERSION) {
    if ((opcode == CACHE_OPEN_READ_LONG) || (opcode == CACHE_OPEN_READ_BUFFER_LONG)) {
      ink_assert(hostname);
      ink_assert(hostlen);

      // Determine length of data to Marshal
      flen = op_to_sizeof_fixedlen_msg(opcode);

      len += request->m_heap->marshal_length();
      len += params->marshal_length();
      len += hostlen;

      if ((flen + len) > DEFAULT_MAX_BUFFER_SIZE) // Bound marshalled data
        goto err_exit;

      // Perform data Marshal operation
      msg  = (char *)ALLOCA_DOUBLE(flen + len);
      data = msg + flen;

      int cur_len = len;

      res = request->m_heap->marshal(data, cur_len);
      if (res < 0) {
        goto err_exit;
      }
      data += res;
      cur_len -= res;
      if ((res = params->marshal(data, cur_len)) < 0)
        goto err_exit;
      data += res;
      memcpy(data, hostname, hostlen);

      CacheOpArgs_General readArgs;
      readArgs.url_md5      = key;
      readArgs.pin_in_cache = pin_in_cache;
      readArgs.frag_type    = frag_type;
      return CacheContinuation::do_op(cont, owner_machine, (void *)&readArgs, opcode, (char *)msg, (flen + len), -1, buf);
    } else {
      // Build message if we have host data.

      if (hostlen) {
        // Determine length of data to Marshal
        flen = op_to_sizeof_fixedlen_msg(opcode);
        len  = hostlen;

        if ((flen + len) > DEFAULT_MAX_BUFFER_SIZE) // Bound marshalled data
          goto err_exit;

        msg  = (char *)ALLOCA_DOUBLE(flen + len);
        data = msg + flen;
        memcpy(data, hostname, hostlen);

      } else {
        msg  = 0;
        flen = 0;
        len  = 0;
      }
      CacheOpArgs_General readArgs;
      readArgs.url_md5   = key;
      readArgs.frag_type = frag_type;
      return CacheContinuation::do_op(cont, owner_machine, (void *)&readArgs, opcode, (char *)msg, (flen + len), -1, buf);
    }

  } else {
    //////////////////////////////////////////////////////////////
    // Create the specified down rev version of this message
    //////////////////////////////////////////////////////////////
    ink_release_assert(!"CacheOpMsg_long [read] bad msg version");
  }
err_exit:
  Action a;
  a = cont;
  return CacheContinuation::callback_failure(&a, CACHE_EVENT_OPEN_READ_FAILED, 0);
}

inline Action *
Cluster_write(Continuation *cont, int expected_size, MIOBuffer *buf, ClusterMachine *m, const CacheKey *url_md5, CacheFragType ft,
              int options, time_t pin_in_cache, int opcode, CacheHTTPHdr *request, CacheHTTPInfo *old_info, const char *hostname,
              int hostlen)
{
  (void)request;
  if (clusterProcessor.disable_remote_cluster_ops(m)) {
    Action a;
    a = cont;
    return CacheContinuation::callback_failure(&a, CACHE_EVENT_OPEN_WRITE_FAILED, 0);
  }
  char *msg                 = 0;
  char *data                = 0;
  int allow_multiple_writes = 0;
  int len                   = 0;
  int flen                  = 0;
  int vers                  = CacheOpMsg_long::protoToVersion(m->msg_proto_major);

  switch (opcode) {
  case CACHE_OPEN_WRITE: {
    // Build message if we have host data
    if (hostlen) {
      // Determine length of data to Marshal
      flen = op_to_sizeof_fixedlen_msg(CACHE_OPEN_WRITE);
      len  = hostlen;

      if ((flen + len) > DEFAULT_MAX_BUFFER_SIZE) // Bound marshalled data
        goto err_exit;

      msg  = (char *)ALLOCA_DOUBLE(flen + len);
      data = msg + flen;

      memcpy(data, hostname, hostlen);
    }
    break;
  }
  case CACHE_OPEN_WRITE_LONG: {
    ink_assert(hostname);
    ink_assert(hostlen);

    // Determine length of data to Marshal
    flen = op_to_sizeof_fixedlen_msg(CACHE_OPEN_WRITE_LONG);
    len  = 0;

    if (old_info == (CacheHTTPInfo *)CACHE_ALLOW_MULTIPLE_WRITES) {
      old_info              = 0;
      allow_multiple_writes = 1;
    }
    if (old_info) {
      len += old_info->marshal_length();
    }
    len += hostlen;

    if ((flen + len) > DEFAULT_MAX_BUFFER_SIZE) // Bound marshalled data
      goto err_exit;

    // Perform data Marshal operation
    msg  = (char *)ALLOCA_DOUBLE(flen + len);
    data = msg + flen;

    if (old_info) {
      int res = old_info->marshal(data, len);

      if (res < 0) {
        goto err_exit;
      }
      data += res;
    }
    memcpy(data, hostname, hostlen);
    break;
  }
  default: {
    ink_release_assert(!"open_write_internal invalid opcode.");
  } // End of case
  } // End of switch

  if (vers == CacheOpMsg_long::CACHE_OP_LONG_MESSAGE_VERSION) {
    // Do remote open_write()
    CacheOpArgs_General writeArgs;
    writeArgs.url_md5      = url_md5;
    writeArgs.pin_in_cache = pin_in_cache;
    writeArgs.frag_type    = ft;
    writeArgs.cfl_flags |= (options & CACHE_WRITE_OPT_OVERWRITE ? CFL_OVERWRITE_ON_WRITE : 0);
    writeArgs.cfl_flags |= (old_info ? CFL_LOPENWRITE_HAVE_OLDINFO : 0);
    writeArgs.cfl_flags |= (allow_multiple_writes ? CFL_ALLOW_MULTIPLE_WRITES : 0);

    return CacheContinuation::do_op(cont, m, (void *)&writeArgs, opcode, msg, flen + len, expected_size, buf);
  } else {
    //////////////////////////////////////////////////////////////
    // Create the specified down rev version of this message
    //////////////////////////////////////////////////////////////
    ink_release_assert(!"CacheOpMsg_long [write] bad msg version");
    return (Action *)0;
  }

err_exit:
  Action a;
  a = cont;
  return CacheContinuation::callback_failure(&a, CACHE_EVENT_OPEN_WRITE_FAILED, 0);
}

inline Action *
Cluster_link(ClusterMachine *m, Continuation *cont, CacheKey *from, CacheKey *to, CacheFragType type, char *hostname, int host_len)
{
  if (clusterProcessor.disable_remote_cluster_ops(m)) {
    Action a;
    a = cont;
    return CacheContinuation::callback_failure(&a, CACHE_EVENT_LINK_FAILED, 0);
  }

  int vers = CacheOpMsg_short_2::protoToVersion(m->msg_proto_major);
  if (vers == CacheOpMsg_short_2::CACHE_OP_SHORT_2_MESSAGE_VERSION) {
    // Do remote link

    // Allocate memory for message header
    int flen = op_to_sizeof_fixedlen_msg(CACHE_LINK);
    int len  = host_len;

    if ((flen + len) > DEFAULT_MAX_BUFFER_SIZE) // Bound marshalled data
      goto err_exit;

    char *msg = (char *)ALLOCA_DOUBLE(flen + len);
    memcpy((msg + flen), hostname, host_len);

    // Setup args for remote link
    CacheOpArgs_Link linkArgs;
    linkArgs.from      = from;
    linkArgs.to        = to;
    linkArgs.frag_type = type;
    return CacheContinuation::do_op(cont, m, (void *)&linkArgs, CACHE_LINK, msg, (flen + len));
  } else {
    //////////////////////////////////////////////////////////////
    // Create the specified down rev version of this message
    //////////////////////////////////////////////////////////////
    ink_release_assert(!"CacheOpMsg_short_2 [CACHE_LINK] bad msg version");
    return 0;
  }

err_exit:
  Action a;
  a = cont;
  return CacheContinuation::callback_failure(&a, CACHE_EVENT_LINK_FAILED, 0);
}

inline Action *
Cluster_deref(ClusterMachine *m, Continuation *cont, CacheKey *key, CacheFragType type, char *hostname, int host_len)
{
  if (clusterProcessor.disable_remote_cluster_ops(m)) {
    Action a;
    a = cont;
    return CacheContinuation::callback_failure(&a, CACHE_EVENT_DEREF_FAILED, 0);
  }

  int vers = CacheOpMsg_short::protoToVersion(m->msg_proto_major);
  if (vers == CacheOpMsg_short::CACHE_OP_SHORT_MESSAGE_VERSION) {
    // Do remote deref

    // Allocate memory for message header
    int flen = op_to_sizeof_fixedlen_msg(CACHE_DEREF);
    int len  = host_len;

    if ((flen + len) > DEFAULT_MAX_BUFFER_SIZE) // Bound marshalled data
      goto err_exit;

    char *msg = (char *)ALLOCA_DOUBLE(flen + len);
    memcpy((msg + flen), hostname, host_len);

    // Setup args for remote deref
    CacheOpArgs_Deref drefArgs;
    drefArgs.md5       = key;
    drefArgs.frag_type = type;
    return CacheContinuation::do_op(cont, m, (void *)&drefArgs, CACHE_DEREF, msg, (flen + len));
  } else {
    //////////////////////////////////////////////////////////////
    // Create the specified down rev version of this message
    //////////////////////////////////////////////////////////////
    ink_release_assert(!"CacheOpMsg_short [CACHE_DEREF] bad msg version");
    return 0;
  }

err_exit:
  Action a;
  a = cont;
  return CacheContinuation::callback_failure(&a, CACHE_EVENT_DEREF_FAILED, 0);
}

inline Action *
Cluster_remove(ClusterMachine *m, Continuation *cont, const CacheKey *key, CacheFragType frag_type, const char *hostname,
               int host_len)
{
  if (clusterProcessor.disable_remote_cluster_ops(m)) {
    Action a;
    a = cont;
    return CacheContinuation::callback_failure(&a, CACHE_EVENT_REMOVE_FAILED, 0);
  }

  int vers = CacheOpMsg_short::protoToVersion(m->msg_proto_major);
  if (vers == CacheOpMsg_short::CACHE_OP_SHORT_MESSAGE_VERSION) {
    // Do remote update

    // Allocate memory for message header
    int flen = op_to_sizeof_fixedlen_msg(CACHE_REMOVE);
    int len  = host_len;

    if ((flen + len) > DEFAULT_MAX_BUFFER_SIZE) // Bound marshalled data
      goto err_exit;

    char *msg = (char *)ALLOCA_DOUBLE(flen + len);
    memcpy((msg + flen), hostname, host_len);

    // Setup args for remote update
    CacheOpArgs_General updateArgs;
    ink_zero(updateArgs);
    updateArgs.url_md5   = key;
    updateArgs.frag_type = frag_type;
    return CacheContinuation::do_op(cont, m, (void *)&updateArgs, CACHE_REMOVE, msg, (flen + len));
  } else {
    //////////////////////////////////////////////////////////////
    // Create the specified down rev version of this message
    //////////////////////////////////////////////////////////////
    ink_release_assert(!"CacheOpMsg_short [CACHE_REMOVE] bad msg version");
    return (Action *)0;
  }

err_exit:
  Action a;
  a = cont;
  return CacheContinuation::callback_failure(&a, CACHE_EVENT_REMOVE_FAILED, 0);
}

#endif /* __CLUSTERINLINE_H__ */
