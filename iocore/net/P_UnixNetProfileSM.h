/** @file
  A brief file description

  @section license License
 */

/********

  This file implements a NetProfileSM edition for Unix/Linux.

 ********/

#ifndef __P_UNIXNET_PROFILESM_H__
#define __P_UNIXNET_PROFILESM_H__

#include "ts/ink_sock.h"
#include "I_NetVConnection.h"
#include "P_UnixNetState.h"
#include "P_Connection.h"

class NetHandler;

class UnixNetProfileSM : public NetProfileSM
{
public:
  UnixNetProfileSM(ProxyMutex *aMutex = NULL) : NetProfileSM(aMutex) {}
  virtual void free(EThread *t)       = 0;
  virtual int mainEvent(int event, void *data) = 0;
  virtual void handle_read(NetHandler *nh, EThread *lthread);
  virtual void handle_write(NetHandler *nh, EThread *lthread);

  // for READ & WRITE
  virtual int64_t read(void *buf, int64_t len, int &err = DEFAULT) = 0;
  virtual int64_t readv(struct iovec *vector, int count) = 0;
  virtual int64_t write(void *buf, int64_t len, int &err = DEFAULT) = 0;
  virtual int64_t writev(struct iovec *vector, int count)     = 0;
  virtual int64_t raw_read(void *buf, int64_t len)            = 0;
  virtual int64_t raw_readv(struct iovec *vector, int count)  = 0;
  virtual int64_t raw_write(void *buf, int64_t len)           = 0;
  virtual int64_t raw_writev(struct iovec *vector, int count) = 0;
  virtual int64_t read_from_net(int64_t toread, int64_t &rattempted, int64_t &total_read, MIOBufferAccessor &buf)    = 0;
  virtual int64_t load_buffer_and_write(int64_t towrite, MIOBufferAccessor &buf, int64_t &total_written, int &needs) = 0;

  virtual void
  reenable()
  {
    return;
  }

  virtual const char *get_protocol_tag() const = 0;
};

/********
 TcpProfileSM
 One of base NetProfileSM for any TCP based protocol.
 ********/

class TcpProfileSM : public UnixNetProfileSM
{
public:
  TcpProfileSM();
  static TcpProfileSM *allocate(EThread *t);
  static bool
  check_dep(NetProfileSM *current_netprofile_sm)
  {
    // TcpProfileSM is base NetProfileSM.
    // The current NetProfileSM of netVC should not set before attach TcpProfileSM.
    return current_netprofile_sm ? false : true;
  }
  void free(EThread *t);
  int mainEvent(int event, void *data);

  // for READ & WRITE
  int64_t read(void *buf, int64_t len, int &err = DEFAULT);
  int64_t readv(struct iovec *vector, int count);
  int64_t write(void *buf, int64_t len, int &err = DEFAULT);
  int64_t writev(struct iovec *vector, int count);
  int64_t
  raw_read(void *buf, int64_t len)
  {
    return 0;
  }
  int64_t
  raw_readv(struct iovec *vector, int count)
  {
    return 0;
  }
  int64_t
  raw_write(void *buf, int64_t len)
  {
    return 0;
  }
  int64_t
  raw_writev(struct iovec *vector, int count)
  {
    return 0;
  }
  const char *
  get_protocol_tag() const
  {
    return vc->options.get_proto_string();
  }
  int64_t read_from_net(int64_t toread, int64_t &rattempted, int64_t &total_read, MIOBufferAccessor &buf);
  int64_t load_buffer_and_write(int64_t towrite, MIOBufferAccessor &buf, int64_t &total_written, int &needs);

private:
  TcpProfileSM(const TcpProfileSM &);
  TcpProfileSM &operator=(const TcpProfileSM &);
};

extern ClassAllocator<TcpProfileSM> tcpProfileSMAllocator;

/********
 TODO: UdpProfileSM
 ********/
/*
class UdpProfileSM : public ProfileSM
{
public:
  UdpProfileSM();
  static UdpProfileSM *allocate(EThread *t);
  static bool
  check_dep(NetProfileSM *current_netprofile_sm)
  {
    // UdpProfileSM is base NetProfileSM.
    // The current NetProfileSM of netVC should not set before attach UdpProfileSM.
    return current_netprofile_sm ? false : true;
  }
  void free(EThread *t);
  int mainEvent(int event, void *data);
private:
  UdpProfileSM(const UdpProfileSM &);
  UdpProfileSM &operator=(const UdpProfileSM &);
};
extern ClassAllocator<UdpProfileSM> udpProfileSMAllocator;
*/

#endif /* __P_UNIXNET_PROFILESM_H__ */
