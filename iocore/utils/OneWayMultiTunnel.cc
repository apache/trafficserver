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

   OneWayMultiTunnel.h
 ****************************************************************************/

#include "P_EventSystem.h"
#include "I_OneWayMultiTunnel.h"

// #define TEST

//////////////////////////////////////////////////////////////////////////////
//
//      OneWayMultiTunnel::OneWayMultiTunnel()
//
//////////////////////////////////////////////////////////////////////////////

ClassAllocator<OneWayMultiTunnel> OneWayMultiTunnelAllocator("OneWayMultiTunnelAllocator");

OneWayMultiTunnel::OneWayMultiTunnel() : OneWayTunnel()
{
  ink_zero(vioTargets);
}

OneWayMultiTunnel *
OneWayMultiTunnel::OneWayMultiTunnel_alloc()
{
  return OneWayMultiTunnelAllocator.alloc();
}

void
OneWayMultiTunnel::OneWayMultiTunnel_free(OneWayMultiTunnel *pOWT)
{
  pOWT->mutex = nullptr;
  OneWayMultiTunnelAllocator.free(pOWT);
}

void
OneWayMultiTunnel::init(VConnection *vcSource, VConnection **vcTargets, int n_vcTargets, Continuation *aCont, int size_estimate,
                        int64_t nbytes, bool asingle_buffer, /* = true */
                        bool aclose_source,                  /* = false */
                        bool aclose_targets,                 /* = false */
                        Transform_fn aManipulate_fn, int water_mark)
{
  mutex                            = aCont ? aCont->mutex : make_ptr(new_ProxyMutex());
  cont                             = aCont;
  manipulate_fn                    = aManipulate_fn;
  close_source                     = aclose_source;
  close_target                     = aclose_targets;
  source_read_previously_completed = false;

  SET_HANDLER(&OneWayMultiTunnel::startEvent);

  n_connections = n_vioTargets + 1;

  int64_t size_index = 0;
  if (size_estimate) {
    size_index = buffer_size_to_index(size_estimate, default_large_iobuffer_size);
  } else {
    size_index = default_large_iobuffer_size;
  }

  tunnel_till_done = (nbytes == TUNNEL_TILL_DONE);

  MIOBuffer *buf1 = new_MIOBuffer(size_index);
  MIOBuffer *buf2 = nullptr;

  single_buffer = asingle_buffer;

  if (single_buffer) {
    buf2 = buf1;
  } else {
    buf2 = new_MIOBuffer(size_index);
  }
  topOutBuffer.writer_for(buf2);

  buf1->water_mark = water_mark;

  vioSource = vcSource->do_io_read(this, nbytes, buf1);

  ink_assert(n_vcTargets <= ONE_WAY_MULTI_TUNNEL_LIMIT);
  for (int i = 0; i < n_vcTargets; i++) {
    vioTargets[i] = vc_do_io_write(vcTargets[i], this, INT64_MAX, buf2, 0);
  }

  return;
}

void
OneWayMultiTunnel::init(Continuation *aCont, VIO *SourceVio, VIO **TargetVios, int n_TargetVios, bool aclose_source,
                        bool aclose_targets)
{
  mutex         = aCont ? aCont->mutex : make_ptr(new_ProxyMutex());
  cont          = aCont;
  single_buffer = true;
  manipulate_fn = nullptr;
  n_connections = n_TargetVios + 1;
  ;
  close_source = aclose_source;
  close_target = aclose_targets;
  // The read on the source vio may have already been completed, yet
  // we still need to write data into the target buffers.  Note this
  // fact as we'll not get a VC_EVENT_READ_COMPLETE callback later.
  source_read_previously_completed = (SourceVio->ntodo() == 0);
  tunnel_till_done                 = true;
  n_vioTargets                     = n_TargetVios;
  topOutBuffer.writer_for(SourceVio->buffer.writer());

  // do_io_read() read already posted on vcSource.
  // do_io_write() already posted on vcTargets
  SET_HANDLER(&OneWayMultiTunnel::startEvent);

  SourceVio->set_continuation(this);
  vioSource = SourceVio;

  for (int i = 0; i < n_vioTargets; i++) {
    vioTargets[i] = TargetVios[i];
    vioTargets[i]->set_continuation(this);
  }
}

//////////////////////////////////////////////////////////////////////////////
//
//      int OneWayMultiTunnel::startEvent()
//
//////////////////////////////////////////////////////////////////////////////

int
OneWayMultiTunnel::startEvent(int event, void *data)
{
  VIO *vio   = (VIO *)data;
  int ret    = VC_EVENT_DONE;
  int result = 0;

#ifdef TEST
  const char *event_origin = (vio == vioSource ? "source" : "target"), *event_name = get_vc_event_name(event);
  printf("OneWayMultiTunnel::startEvent --- %s received from %s VC\n", event_name, event_origin);
#endif

  // handle the event
  //
  switch (event) {
  case VC_EVENT_READ_READY: { // SunCC uses old scoping rules
    transform(vioSource->buffer, topOutBuffer);
    for (int i = 0; i < n_vioTargets; i++) {
      if (vioTargets[i]) {
        vioTargets[i]->reenable();
      }
    }
    ret = VC_EVENT_CONT;
    break;
  }

  case VC_EVENT_WRITE_READY:
    if (vioSource) {
      vioSource->reenable();
    }
    ret = VC_EVENT_CONT;
    break;

  case VC_EVENT_EOS:
    if (!tunnel_till_done && vio->ntodo()) {
      goto Lerror;
    }
    if (vio == vioSource) {
      transform(vioSource->buffer, topOutBuffer);
      goto Lread_complete;
    } else {
      goto Lwrite_complete;
    }
    // fallthrough

  Lread_complete:
  case VC_EVENT_READ_COMPLETE: { // SunCC uses old scoping rules
    // set write nbytes to the current buffer size
    //
    for (int i = 0; i < n_vioTargets; i++) {
      if (vioTargets[i]) {
        vioTargets[i]->nbytes = vioTargets[i]->ndone + vioTargets[i]->buffer.reader()->read_avail();
        vioTargets[i]->reenable();
      }
    }
    close_source_vio(0);
    ret = VC_EVENT_DONE;
    break;
  }

  Lwrite_complete:
  case VC_EVENT_WRITE_COMPLETE:
    close_target_vio(0, (VIO *)data);
    if ((n_connections == 0) || (n_connections == 1 && source_read_previously_completed)) {
      goto Ldone;
    } else if (vioSource) {
      vioSource->reenable();
    }
    break;

  Lerror:
  case VC_EVENT_ERROR:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
    result = -1;
  Ldone:
    close_source_vio(result);
    close_target_vio(result);
    connection_closed(result);
    break;

  default:
    ret = VC_EVENT_CONT;
    break;
  }
#ifdef TEST
  printf("    (OneWayMultiTunnel returning value: %s)\n", (ret == VC_EVENT_DONE ? "VC_EVENT_DONE" : "VC_EVENT_CONT"));
#endif
  return (ret);
}

void
OneWayMultiTunnel::close_target_vio(int result, VIO *vio)
{
  for (int i = 0; i < n_vioTargets; i++) {
    VIO *v = vioTargets[i];
    if (v && (!vio || v == vio)) {
      if (last_connection() || !single_buffer) {
        free_MIOBuffer(v->buffer.writer());
      }
      if (close_target) {
        v->vc_server->do_io_close();
      }
      vioTargets[i] = nullptr;
      n_connections--;
    }
  }
}

void
OneWayMultiTunnel::reenable_all()
{
  for (int i = 0; i < n_vioTargets; i++) {
    if (vioTargets[i]) {
      vioTargets[i]->reenable();
    }
  }
  if (vioSource) {
    vioSource->reenable();
  }
}
