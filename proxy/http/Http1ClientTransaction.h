/** @file

  Http1ClientTransaction.h - The Transaction class for Http1*

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

#ifndef __HTTP_CLIENT_TRANSACTION_H_
#define __HTTP_CLIENT_TRANSACTION_H_

#include "../ProxyClientTransaction.h"

class Continuation;

class Http1ClientTransaction : public ProxyClientTransaction
{
public:
  typedef ProxyClientTransaction super;

  Http1ClientTransaction() : super(), outbound_port(0), outbound_transparent(false) {}
  // Implement VConnection interface.
  VIO *
  do_io_read(Continuation *c, int64_t nbytes = INT64_MAX, MIOBuffer *buf = 0) override
  {
    return parent->do_io_read(c, nbytes, buf);
  }
  VIO *
  do_io_write(Continuation *c = NULL, int64_t nbytes = INT64_MAX, IOBufferReader *buf = 0, bool owner = false) override
  {
    return parent->do_io_write(c, nbytes, buf, owner);
  }

  void
  do_io_close(int lerrno = -1) override
  {
    parent->do_io_close(lerrno);
    // this->destroy(); Parent owns this data structure.  No need for separate destroy.
  }

  // Don't destroy your elements.  Rely on the Http1ClientSession to clean up the
  // Http1ClientTransaction class as necessary.  The super::destroy() clears the
  // mutex, which Http1ClientSession owns.
  void
  destroy() override
  {
    current_reader = nullptr;
  }

  void
  do_io_shutdown(ShutdownHowTo_t howto) override
  {
    parent->do_io_shutdown(howto);
  }

  void
  reenable(VIO *vio) override
  {
    parent->reenable(vio);
  }

  void
  set_reader(IOBufferReader *reader)
  {
    sm_reader = reader;
  }

  void release(IOBufferReader *r) override;

  bool
  ignore_keep_alive() override
  {
    return false;
  }

  bool allow_half_open() const override;

  void set_parent(ProxyClientSession *new_parent) override;

  uint16_t
  get_outbound_port() const override
  {
    return outbound_port;
  }

  IpAddr
  get_outbound_ip4() const override
  {
    return outbound_ip4;
  }

  IpAddr
  get_outbound_ip6() const override
  {
    return outbound_ip6;
  }

  void
  set_outbound_port(uint16_t new_port) override
  {
    outbound_port = new_port;
  }

  void
  set_outbound_ip(const IpAddr &new_addr) override
  {
    if (new_addr.isIp4()) {
      outbound_ip4 = new_addr;
    } else if (new_addr.isIp6()) {
      outbound_ip6 = new_addr;
    } else {
      clear_outbound_ip();
    }
  }
  void
  clear_outbound_ip()
  {
    outbound_ip4.invalidate();
    outbound_ip6.invalidate();
  }
  bool
  is_outbound_transparent() const override
  {
    return outbound_transparent;
  }
  void
  set_outbound_transparent(bool flag) override
  {
    outbound_transparent = flag;
  }

  // Pass on the timeouts to the netvc
  void
  set_active_timeout(ink_hrtime timeout_in) override
  {
    if (parent)
      parent->set_active_timeout(timeout_in);
  }
  void
  set_inactivity_timeout(ink_hrtime timeout_in) override
  {
    if (parent)
      parent->set_inactivity_timeout(timeout_in);
  }
  void
  cancel_inactivity_timeout() override
  {
    if (parent)
      parent->cancel_inactivity_timeout();
  }
  void transaction_done() override;

  int
  get_transaction_id() const override
  {
    // For HTTP/1 there is only one on-going transaction at a time per session/connection.  Therefore, the transaction count can be
    // presumed not to increase during the lifetime of a transaction, thus this function will return a consistent unique transaction
    // identifier.
    //
    return get_transact_count();
  }

protected:
  uint16_t outbound_port;
  IpAddr outbound_ip4;
  IpAddr outbound_ip6;
  bool outbound_transparent;
};

#endif
