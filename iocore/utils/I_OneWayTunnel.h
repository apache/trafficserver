/** @file

  One way tunnel

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

  Part of the utils library which contains classes that use multiple
  components of the IO-Core to implement some useful functionality. The
  classes also serve as good examples of how to use the IO-Core.

 */

#pragma once

#include "I_EventSystem.h"

//////////////////////////////////////////////////////////////////////////////
//
//      OneWayTunnel
//
//////////////////////////////////////////////////////////////////////////////

#define TUNNEL_TILL_DONE INT64_MAX

#define ONE_WAY_TUNNEL_CLOSE_ALL nullptr

typedef void (*Transform_fn)(MIOBufferAccessor &in_buf, MIOBufferAccessor &out_buf);

/**
  A generic state machine that connects two virtual connections. A
  OneWayTunnel is a module that connects two virtual connections, a source
  vc and a target vc, and copies the data between source and target. Once
  the tunnel is started using the init() call, it handles all the events
  from the source and target and optionally calls a continuation back when
  its done. On success it calls back the continuation with VC_EVENT_EOS,
  and with VC_EVENT_ERROR on failure.

  If manipulate_fn is not nullptr, then the tunnel acts as a filter,
  processing all data arriving from the source vc by the manipulate_fn
  function, before sending to the target vc. By default, the manipulate_fn
  is set to nullptr, yielding the identity function. manipulate_fn takes
  a IOBuffer containing the data to be written into the target virtual
  connection which it may manipulate in any manner it sees fit.

*/
struct OneWayTunnel : public Continuation {
  //
  //  Public Interface
  //

  //  Copy nbytes from vcSource to vcTarget.  When done, call
  //  aCont back with either VC_EVENT_EOS (on success) or
  //  VC_EVENT_ERROR (on error)
  //

  // Use these to construct/destruct OneWayTunnel objects

  /**
    Allocates a OneWayTunnel object.

    @return new OneWayTunnel object.

  */
  static OneWayTunnel *OneWayTunnel_alloc();

  /** Deallocates a OneWayTunnel object. */
  static void OneWayTunnel_free(OneWayTunnel *);

  static void SetupTwoWayTunnel(OneWayTunnel *east, OneWayTunnel *west);
  OneWayTunnel();
  ~OneWayTunnel() override;

  // Use One of the following init functions to start the tunnel.
  /**
    This init function sets up the read (calls do_io_read) and the write
    (calls do_io_write).

    @param vcSource source VConnection. A do_io_read should not have
      been called on the vcSource. The tunnel calls do_io_read on this VC.
    @param vcTarget target VConnection. A do_io_write should not have
      been called on the vcTarget. The tunnel calls do_io_write on this VC.
    @param aCont continuation to call back when the tunnel finishes. If
      not specified, the tunnel deallocates itself without calling back
      anybody. Otherwise, its the callee's responsibility to deallocate
      the tunnel with OneWayTunnel_free.
    @param size_estimate size of the MIOBuffer to create for
      reading/writing to/from the VC's.
    @param aMutex lock that this tunnel will run under. If aCont is
      specified, the Continuation's lock is used instead of aMutex.
    @param nbytes number of bytes to transfer.
    @param asingle_buffer whether the same buffer should be used to read
      from vcSource and write to vcTarget. This should be set to true in
      most cases, unless the data needs be transformed.
    @param aclose_source if true, the tunnel closes vcSource at the
      end. If aCont is not specified, this should be set to true.
    @param aclose_target if true, the tunnel closes vcTarget at the
      end. If aCont is not specified, this should be set to true.
    @param manipulate_fn if specified, the tunnel calls this function
      with the input and the output buffer, whenever it gets new data
      in the input buffer. This function can transform the data in the
      input buffer
    @param water_mark watermark for the MIOBuffer used for reading.

  */
  void init(VConnection *vcSource, VConnection *vcTarget, Continuation *aCont = nullptr, int size_estimate = 0, // 0 = best guess
            ProxyMutex *aMutex = nullptr, int64_t nbytes = TUNNEL_TILL_DONE, bool asingle_buffer = true, bool aclose_source = true,
            bool aclose_target = true, Transform_fn manipulate_fn = nullptr, int water_mark = 0);

  /**
    This init function sets up only the write side. It assumes that the
    read VConnection has already been setup.

    @param vcSource source VConnection. Prior to calling this
      init function, a do_io_read should have been called on this
      VConnection. The tunnel uses the same MIOBuffer and frees
      that buffer when the transfer is done (either successful or
      unsuccessful).
    @param vcTarget target VConnection. A do_io_write should not have
      been called on the vcTarget. The tunnel calls do_io_write on
      this VC.
    @param aCont The Continuation to call back when the tunnel
      finishes. If not specified, the tunnel deallocates itself without
      calling back anybody.
    @param SourceVio VIO of the vcSource.
    @param reader IOBufferReader that reads from the vcSource. This
      reader is provided to vcTarget.
    @param aclose_source if true, the tunnel closes vcSource at the
      end. If aCont is not specified, this should be set to true.
    @param aclose_target if true, the tunnel closes vcTarget at the
      end. If aCont is not specified, this should be set to true.
  */
  void init(VConnection *vcSource, VConnection *vcTarget, Continuation *aCont, VIO *SourceVio, IOBufferReader *reader,
            bool aclose_source = true, bool aclose_target = true);

  /**
    Use this init function if both the read and the write sides have
    already been setup. The tunnel assumes that the read VC and the
    write VC are using the same buffer and frees that buffer
    when the transfer is done (either successful or unsuccessful)
    @param aCont The Continuation to call back when the tunnel finishes. If
    not specified, the tunnel deallocates itself without calling back
    anybody.

    @param SourceVio read VIO of the Source VC.
    @param TargetVio write VIO of the Target VC.
    @param aclose_source if true, the tunnel closes vcSource at the
      end. If aCont is not specified, this should be set to true.
    @param aclose_target if true, the tunnel closes vcTarget at the
      end. If aCont is not specified, this should be set to true.

    */
  void init(Continuation *aCont, VIO *SourceVio, VIO *TargetVio, bool aclose_source = true, bool aclose_target = true);

  //
  // Private
  //
  OneWayTunnel(Continuation *aCont, Transform_fn manipulate_fn = nullptr, bool aclose_source = false, bool aclose_target = false);

  int startEvent(int event, void *data);

  virtual void transform(MIOBufferAccessor &in_buf, MIOBufferAccessor &out_buf);

  /** Result is -1 for any error. */
  void close_source_vio(int result);

  virtual void close_target_vio(int result, VIO *vio = ONE_WAY_TUNNEL_CLOSE_ALL);

  void connection_closed(int result);

  virtual void reenable_all();

  bool last_connection();

  VIO *vioSource             = nullptr;
  VIO *vioTarget             = nullptr;
  Continuation *cont         = nullptr;
  Transform_fn manipulate_fn = nullptr;
  int n_connections          = 0;
  int lerrno                 = 0;

  bool single_buffer    = false;
  bool close_source     = false;
  bool close_target     = false;
  bool tunnel_till_done = false;

  /** Non-nullptr when this is one side of a two way tunnel. */
  OneWayTunnel *tunnel_peer = nullptr;
  bool free_vcs             = true;

  // noncopyable
  OneWayTunnel(const OneWayTunnel &) = delete;
  OneWayTunnel &operator=(const OneWayTunnel &) = delete;
};
