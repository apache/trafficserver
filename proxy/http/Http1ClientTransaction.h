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

  Http1ClientTransaction() : super() {}
  // Implement VConnection interface.
  virtual VIO *
  do_io_read(Continuation *c, int64_t nbytes = INT64_MAX, MIOBuffer *buf = 0)
  {
    return parent->do_io_read(c, nbytes, buf);
  }
  virtual VIO *
  do_io_write(Continuation *c = NULL, int64_t nbytes = INT64_MAX, IOBufferReader *buf = 0, bool owner = false)
  {
    return parent->do_io_write(c, nbytes, buf, owner);
  }

  virtual void
  do_io_close(int lerrno = -1)
  {
    parent->do_io_close(lerrno);
    // this->destroy(); Parent owns this data structure.  No need for separate destroy.
  }

  // Don't destroy your elements.  Rely on the Http1ClientSession to clean up the
  // Http1ClientTransaction class as necessary
  virtual void
  destroy()
  {
  }

  // Clean up the transaction elements when the ClientSession shuts down
  void
  cleanup()
  {
    super::destroy();
  }

  virtual void
  do_io_shutdown(ShutdownHowTo_t howto)
  {
    parent->do_io_shutdown(howto);
  }
  virtual void
  reenable(VIO *vio)
  {
    parent->reenable(vio);
  }
  void
  set_reader(IOBufferReader *reader)
  {
    sm_reader = reader;
  }
  void release(IOBufferReader *r);
  virtual bool
  ignore_keep_alive()
  {
    return false;
  }

  virtual bool
  allow_half_open() const
  {
    return true;
  }
  virtual const char *
  get_protocol_string() const
  {
    return "http";
  }

  void set_parent(ProxyClientSession *new_parent);

  virtual uint16_t
  get_outbound_port() const
  {
    return outbound_port;
  }
  virtual IpAddr
  get_outbound_ip4() const
  {
    return outbound_ip4;
  }
  virtual IpAddr
  get_outbound_ip6() const
  {
    return outbound_ip6;
  }
  virtual void
  set_outbound_port(uint16_t new_port)
  {
    outbound_port = new_port;
  }
  virtual void
  set_outbound_ip(const IpAddr &new_addr)
  {
    if (new_addr.isIp4()) {
      outbound_ip4 = new_addr;
    } else if (new_addr.isIp6()) {
      outbound_ip6 = new_addr;
    } else {
      clear_outbound_ip();
    }
  }
  virtual void
  clear_outbound_ip()
  {
    outbound_ip4.invalidate();
    outbound_ip6.invalidate();
  }
  virtual bool
  is_outbound_transparent() const
  {
    return outbound_transparent;
  }
  virtual void
  set_outbound_transparent(bool flag)
  {
    outbound_transparent = flag;
  }

  // Pass on the timeouts to the netvc
  virtual void
  set_active_timeout(ink_hrtime timeout_in)
  {
    if (parent)
      parent->set_active_timeout(timeout_in);
  }
  virtual void
  set_inactivity_timeout(ink_hrtime timeout_in)
  {
    if (parent)
      parent->set_inactivity_timeout(timeout_in);
  }
  virtual void
  cancel_inactivity_timeout()
  {
    if (parent)
      parent->cancel_inactivity_timeout();
  }

protected:
  uint16_t outbound_port;
  IpAddr outbound_ip4;
  IpAddr outbound_ip6;
  bool outbound_transparent;
};

#endif
