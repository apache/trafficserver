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

  ClusterRPC.cc
****************************************************************************/

#include "P_Cluster.h"
/////////////////////////////////////////////////////////////////////////
// All RPC function handlers (xxx_ClusterFunction() ) are invoked from
// ClusterHandler::update_channels_read().
/////////////////////////////////////////////////////////////////////////

void
ping_ClusterFunction(ClusterHandler *ch, void *data, int len)
{
  //
  // Just return the data back
  //
  clusterProcessor.invoke_remote(ch, PING_REPLY_CLUSTER_FUNCTION, data, len);
}

void
ping_reply_ClusterFunction(ClusterHandler *ch, void *data, int len)
{
  //
  // Pass back the data.
  //
  PingMessage *msg = (PingMessage *)data;
  msg->fn(ch, msg->data, (len - msg->sizeof_fixedlen_msg()));
}

void
machine_list_ClusterFunction(ClusterHandler *from, void *data, int len)
{
  (void)from;
  ClusterMessageHeader *mh = (ClusterMessageHeader *)data;
  MachineListMessage *m    = (MachineListMessage *)data;

  if (mh->GetMsgVersion() != MachineListMessage::MACHINE_LIST_MESSAGE_VERSION) { ////////////////////////////////////////////////
    // Convert from old to current message format
    ////////////////////////////////////////////////
    ink_release_assert(!"machine_list_ClusterFunction() bad msg version");
  }
  if (m->NeedByteSwap())
    m->SwapBytes();

  ink_assert(m->n_ip == ((len - m->sizeof_fixedlen_msg()) / sizeof(uint32_t)));

  //
  // The machine list is a vector of ip's stored in network byte order.
  // This list is exchanged whenever a new Cluster Connection is formed.
  //
  ClusterConfiguration *cc = this_cluster()->current_configuration();

  for (unsigned int i = 0; i < m->n_ip; i++) {
    for (int j = 0; j < cc->n_machines; j++) {
      if (cc->machines[j]->ip == m->ip[i])
        goto Lfound;
    }
    // not found, must be a new machine
    {
      int num_connections = this_cluster_machine()->num_connections;
      for (int k = 0; k < num_connections; k++) {
        clusterProcessor.connect(m->ip[i], k);
      }
    }
  Lfound:;
  }
}

void
close_channel_ClusterFunction(ClusterHandler *ch, void *data, int len)
{
  ClusterMessageHeader *mh = (ClusterMessageHeader *)data;
  CloseMessage *m          = (CloseMessage *)data;

  if (mh->GetMsgVersion() != CloseMessage::CLOSE_CHAN_MESSAGE_VERSION) { ////////////////////////////////////////////////
    // Convert from old to current message format
    ////////////////////////////////////////////////
    ink_release_assert(!"close_channel_ClusterFunction() bad msg version");
  }
  if (m->NeedByteSwap())
    m->SwapBytes();

  //
  // Close the remote side of a VC connection (remote node is originator)
  //
  ink_assert(len >= (int)sizeof(CloseMessage));
  if (!ch || !ch->channels)
    return;
  ClusterVConnection *vc = ch->channels[m->channel];
  if (VALID_CHANNEL(vc) && vc->token.sequence_number == m->sequence_number) {
    vc->remote_closed = m->status;
    vc->remote_lerrno = m->lerrno;
    ch->vcs_push(vc, vc->type);
  }
}

void
test_ClusterFunction(ClusterHandler *ch, void *data, int len)
{
  //
  // Note: Only for testing.
  //
  if (ptest_ClusterFunction)
    ptest_ClusterFunction(ch, data, len);
}

CacheVC *
ChannelToCacheWriteVC(ClusterHandler *ch, int channel, uint32_t channel_seqno, ClusterVConnection **cluster_vc)
{
  EThread *thread   = this_ethread();
  ProxyMutex *mutex = thread->mutex;

  ClusterVConnection *cvc = ch->channels[channel];
  if (!VALID_CHANNEL(cvc) || (channel_seqno != cvc->token.sequence_number) || (cvc->read.vio.op != VIO::READ)) {
    CLUSTER_INCREMENT_DYN_STAT(cluster_setdata_no_CLUSTERVC_STAT);
    return NULL;
  }
  // Tunneling from cluster to cache (remote write).
  // Get cache VC pointer.

  OneWayTunnel *owt = (OneWayTunnel *)cvc->read.vio._cont;
  if (!owt) {
    CLUSTER_INCREMENT_DYN_STAT(CLUSTER_SETDATA_NO_TUNNEL_STAT);
    return NULL;
  }
  CacheVC *cache_vc = (CacheVC *)owt->vioTarget->vc_server;
  if (!cache_vc) {
    CLUSTER_INCREMENT_DYN_STAT(CLUSTER_SETDATA_NO_CACHEVC_STAT);
    return NULL;
  }
  *cluster_vc = cvc;
  return cache_vc;
}

void
set_channel_data_ClusterFunction(ClusterHandler *ch, void *tdata, int tlen)
{
  EThread *thread   = this_ethread();
  ProxyMutex *mutex = thread->mutex;
  // We are called on the ET_CLUSTER thread.

  char *data;
  int len;
  int res;

  // Allocate memory for set channel data and pass it to the cache
  IncomingControl *ic = IncomingControl::alloc();
  ic->len             = tlen;
  ic->alloc_data();

  data = ic->data + sizeof(int32_t); // free_remote_data expects d+sizeof(int32_t)
  memcpy(data, tdata, tlen);
  len = tlen;

  ClusterMessageHeader *mh = (ClusterMessageHeader *)data;
  SetChanDataMessage *m    = (SetChanDataMessage *)data;

  if (mh->GetMsgVersion() !=
      SetChanDataMessage::SET_CHANNEL_DATA_MESSAGE_VERSION) { ////////////////////////////////////////////////
    // Convert from old to current message format
    ////////////////////////////////////////////////
    ink_release_assert(!"set_channel_data_ClusterFunction() bad msg version");
  }
  if (m->NeedByteSwap())
    m->SwapBytes();

  ClusterVConnection *cvc;
  CacheVC *cache_vc;

  if (ch) {
    cache_vc = ChannelToCacheWriteVC(ch, m->channel, m->sequence_number, &cvc);
    if (!cache_vc) {
      ic->freeall();
      return;
    }
    // Unmarshal data.
    switch (m->data_type) {
    case CACHE_DATA_HTTP_INFO: {
      char *p = (char *)m + SetChanDataMessage::sizeof_fixedlen_msg();

      IOBufferBlock *block_ref = ic->get_block();
      res                      = HTTPInfo::unmarshal(p, len, block_ref);
      ink_assert(res > 0);

      CacheHTTPInfo h;
      h.get_handle((char *)&m->data[0], len);
      h.set_buffer_reference(block_ref);
      cache_vc->set_http_info(&h);
      ic->freeall();
      break;
    }
    default: {
      ink_release_assert(!"set_channel_data_ClusterFunction bad CacheDataType");
    }
    }                            // End of switch
    ++cvc->n_recv_set_data_msgs; // note received messages

  } else {
    ic->freeall();
    CLUSTER_INCREMENT_DYN_STAT(cluster_setdata_no_CLUSTER_STAT);
  }
}

void
post_setchan_send_ClusterFunction(ClusterHandler *ch, void *data, int /* len ATS_UNUSED */)
{
  EThread *thread   = this_ethread();
  ProxyMutex *mutex = thread->mutex;
  // We are called on the ET_CLUSTER thread.
  // set_data() control message has been queued into cluster transfer message.
  // This allows us to assume that it has been sent.
  // Decrement Cluster VC n_set_data_msgs to allow transmission of
  // initial open_write data after (n_set_data_msgs == 0).

  SetChanDataMessage *m = (SetChanDataMessage *)data;
  ClusterVConnection *cvc;

  if (ch) {
    cvc = ch->channels[m->channel];
    if (VALID_CHANNEL(cvc)) {
      ink_atomic_increment(&cvc->n_set_data_msgs, -1);
    } else {
      CLUSTER_INCREMENT_DYN_STAT(cluster_setdata_no_CLUSTERVC_STAT);
    }
  } else {
    CLUSTER_INCREMENT_DYN_STAT(cluster_setdata_no_CLUSTER_STAT);
  }
}

void
set_channel_pin_ClusterFunction(ClusterHandler *ch, void *data, int /* len ATS_UNUSED */)
{
  // This isn't used. /leif
  // EThread *thread = this_ethread();
  // ProxyMutex *mutex = thread->mutex;

  // We are called on the ET_CLUSTER thread.

  ClusterMessageHeader *mh = (ClusterMessageHeader *)data;
  SetChanPinMessage *m     = (SetChanPinMessage *)data;

  if (mh->GetMsgVersion() != SetChanPinMessage::SET_CHANNEL_PIN_MESSAGE_VERSION) { ////////////////////////////////////////////////
    // Convert from old to current message format
    ////////////////////////////////////////////////
    ink_release_assert(!"set_channel_pin_ClusterFunction() bad msg version");
  }

  if (m->NeedByteSwap())
    m->SwapBytes();

  ClusterVConnection *cvc = NULL; // Just to make GCC happy
  CacheVC *cache_vc;

  if (ch != 0) {
    cache_vc = ChannelToCacheWriteVC(ch, m->channel, m->sequence_number, &cvc);
    if (cache_vc) {
      cache_vc->set_pin_in_cache(m->pin_time);
    }
    // cvc is always set in ChannelToCacheWriteVC, so need to check it
    ++cvc->n_recv_set_data_msgs; // note received messages
  }
}

void
post_setchan_pin_ClusterFunction(ClusterHandler *ch, void *data, int /* len ATS_UNUSED */)
{
  EThread *thread   = this_ethread();
  ProxyMutex *mutex = thread->mutex;
  // We are called on the ET_CLUSTER thread.
  // Control message has been queued into cluster transfer message.
  // This allows us to assume that it has been sent.
  // Decrement Cluster VC n_set_data_msgs to allow transmission of
  // initial open_write data after (n_set_data_msgs == 0).

  SetChanPinMessage *m = (SetChanPinMessage *)data;
  ClusterVConnection *cvc;

  if (ch) {
    cvc = ch->channels[m->channel];
    if (VALID_CHANNEL(cvc)) {
      ink_atomic_increment(&cvc->n_set_data_msgs, -1);
    } else {
      CLUSTER_INCREMENT_DYN_STAT(cluster_setdata_no_CLUSTERVC_STAT);
    }
  } else {
    CLUSTER_INCREMENT_DYN_STAT(cluster_setdata_no_CLUSTER_STAT);
  }
}

void
set_channel_priority_ClusterFunction(ClusterHandler *ch, void *data, int /* len ATS_UNUSED */)
{
  // This isn't used.
  // EThread *thread = this_ethread();
  // ProxyMutex *mutex = thread->mutex;

  // We are called on the ET_CLUSTER thread.

  ClusterMessageHeader *mh  = (ClusterMessageHeader *)data;
  SetChanPriorityMessage *m = (SetChanPriorityMessage *)data;

  if (mh->GetMsgVersion() !=
      SetChanPriorityMessage::SET_CHANNEL_PRIORITY_MESSAGE_VERSION) { ////////////////////////////////////////////////
    // Convert from old to current message format
    ////////////////////////////////////////////////
    ink_release_assert(!"set_channel_priority_ClusterFunction() bad msg version");
  }
  if (m->NeedByteSwap())
    m->SwapBytes();

  ClusterVConnection *cvc = NULL; // Just to make GCC happy
  CacheVC *cache_vc;

  if (ch != 0) {
    cache_vc = ChannelToCacheWriteVC(ch, m->channel, m->sequence_number, &cvc);
    if (cache_vc) {
      cache_vc->set_disk_io_priority(m->disk_priority);
    }
    // cvc is always set in ChannelToCacheWriteVC, so need to check it
    ++cvc->n_recv_set_data_msgs; // note received messages
  }
}

void
post_setchan_priority_ClusterFunction(ClusterHandler *ch, void *data, int /* len ATS_UNUSED */)
{
  EThread *thread   = this_ethread();
  ProxyMutex *mutex = thread->mutex;

  // We are called on the ET_CLUSTER thread.
  // Control message has been queued into cluster transfer message.
  // This allows us to assume that it has been sent.
  // Decrement Cluster VC n_set_data_msgs to allow transmission of
  // initial open_write data after (n_set_data_msgs == 0).

  SetChanPriorityMessage *m = (SetChanPriorityMessage *)data;
  ClusterVConnection *cvc;

  if (ch) {
    cvc = ch->channels[m->channel];
    if (VALID_CHANNEL(cvc)) {
      ink_atomic_increment(&cvc->n_set_data_msgs, -1);
    } else {
      CLUSTER_INCREMENT_DYN_STAT(cluster_setdata_no_CLUSTERVC_STAT);
    }
  } else {
    CLUSTER_INCREMENT_DYN_STAT(cluster_setdata_no_CLUSTER_STAT);
  }
}

// End of ClusterRPC.cc
