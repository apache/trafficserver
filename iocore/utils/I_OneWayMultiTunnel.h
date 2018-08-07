/** @file

  One way multi tunnel

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

#include "I_OneWayTunnel.h"

/** Maximum number which can be tunnelled too */
#define ONE_WAY_MULTI_TUNNEL_LIMIT 4

/**
  A generic state machine that connects a source virtual connection to
  multiple target virtual connections. A OneWayMultiTunnel is similar to
  the OneWayTunnel module. However, instead of connection one source
  to one target, it connects multiple virtual connections - a source
  vc and multiple target vcs, and copies the data from the source
  to the target vcs. The maximum number of Target VCs is limited by
  ONE_WAY_MULTI_TUNNEL_LIMIT.

  If manipulate_fn is not nullptr, then the tunnel acts as a filter,
  processing all data arriving from the source vc by the manipulate_fn
  function, before sending to the target vcs. By default, the
  manipulate_fn is set to nullptr, yielding the identity function.

  @see OneWayTunnel

*/
struct OneWayMultiTunnel : public OneWayTunnel {
  //
  // Public Interface
  //

  // Use these to construct/destruct OneWayMultiTunnel objects

  /**
    Allocates a OneWayMultiTunnel object.

    @return new OneWayTunnel object.

  */
  static OneWayMultiTunnel *OneWayMultiTunnel_alloc();

  /**
    Deallocates a OneWayTunnel object.

  */
  static void OneWayMultiTunnel_free(OneWayMultiTunnel *);

  OneWayMultiTunnel();

  // Use One of the following init functions to start the tunnel.

  /**
    This init function sets up the read (calls do_io_read) and the write
    (calls do_io_write).

    @param vcSource source VConnection. A do_io_read should not have
      been called on the vcSource. The tunnel calls do_io_read on this VC.
    @param vcTargets array of Target VConnections. A do_io_write should
      not have been called on any of the vcTargets. The tunnel calls
      do_io_write on these VCs.
    @param n_vcTargets size of vcTargets.
    @param aCont continuation to call back when the tunnel finishes. If
      not specified, the tunnel deallocates itself without calling
      back anybody.
    @param size_estimate size of the MIOBuffer to create for reading/
      writing to/from the VC's.
    @param nbytes number of bytes to transfer.
    @param asingle_buffer whether the same buffer should be used to read
      from vcSource and write to vcTarget. This should be set to true
      in most cases, unless the data needs be transformed.
    @param aclose_source if true, the tunnel closes vcSource at the
      end. If aCont is not specified, this should be set to true.
    @param aclose_target if true, the tunnel closes vcTarget at the end.
      If aCont is not specified, this should be set to true.
    @param manipulate_fn if specified, the tunnel calls this function
      with the input and the output buffer, whenever it gets new data
      in the input buffer. This function can transform the data in the
      input buffer.
    @param water_mark for the MIOBuffer used for reading.

  */
  void init(VConnection *vcSource, VConnection **vcTargets, int n_vcTargets, Continuation *aCont = nullptr,
            int size_estimate = 0, // 0 == best guess
            int64_t nbytes = TUNNEL_TILL_DONE, bool asingle_buffer = true, bool aclose_source = true, bool aclose_target = true,
            Transform_fn manipulate_fn = nullptr, int water_mark = 0);

  /**
    Use this init function if both the read and the write sides have
    already been setup. The tunnel assumes that the read VC and the
    write VCs are using the same buffer and frees that buffer when the
    transfer is done (either successful or unsuccessful).

    @param aCont continuation to call back when the tunnel finishes. If
      not specified, the tunnel deallocates itself without calling back
      anybody.
    @param SourceVio read VIO of the Source VC.
    @param TargetVios array of write VIOs of the Target VCs.
    @param n_vioTargets size of TargetVios array.
    @param aclose_source if true, the tunnel closes vcSource at the
      end. If aCont is not specified, this should be set to true.
    @param aclose_target ff true, the tunnel closes vcTarget at the
      end. If aCont is not specified, this should be set to true.

  */
  void init(Continuation *aCont, VIO *SourceVio, VIO **TargetVios, int n_vioTargets, bool aclose_source = true,
            bool aclose_target = true);

  //
  // Private
  //
  int startEvent(int event, void *data);

  void reenable_all() override;
  void close_target_vio(int result, VIO *vio = nullptr) override;

  int n_vioTargets                      = 0;
  bool source_read_previously_completed = false;
  MIOBufferAccessor topOutBuffer;
  VIO *vioTargets[ONE_WAY_MULTI_TUNNEL_LIMIT];
};

extern ClassAllocator<OneWayMultiTunnel> OneWayMultiTunnelAllocator;
