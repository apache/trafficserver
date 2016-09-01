/** @file
 A brief file description

 @section license License

 */

/********

  The NetProfileSM is designed to setup a plugable mechanism for NetVConnection
  to handle different type of I/O operation.

 ********/

#ifndef __I_NET_PROFILESM_H__
#define __I_NET_PROFILESM_H__

#include "ts/ink_sock.h"
#include "I_NetVConnection.h"

class NetVConnection;

#define IOCORE_EVENTS_READ (IOCORE_EVENTS_START + 1)
#define IOCORE_EVENTS_WRITE (IOCORE_EVENTS_START + 2)

typedef enum {
  PROFILE_SM_UNDEFINED,
  PROFILE_SM_TCP,
  PROFILE_SM_UDP,
  PROFILE_SM_SSL,
  PROFILE_SM_SOCKS,
} NetProfileSMType_t;

static int DEFAULT = 0;

class NetProfileSM : public Continuation
{
public:
  NetProfileSM(ProxyMutex *aMutex = NULL)
    : Continuation(aMutex), low_profileSM(NULL), vc(NULL), netTrace(false), globally_allocated(false)
  {
  }
  virtual void free(EThread *t) = 0;
  void
  clear()
  {
    low_profileSM = NULL;
    vc            = NULL;
    type          = PROFILE_SM_UNDEFINED;
    mutex         = NULL;
    netTrace      = false;
  }
  virtual int mainEvent(int event, void *data) = 0;

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

  // for netTrace
  bool getTrace() const;

  void
  setTrace(bool trace)
  {
    netTrace = trace;
  }

  virtual void
  set_mutex(ProxyMutex *aMutex)
  {
    mutex = aMutex;
  }

  virtual NetProfileSMType_t
  get_type() const
  {
    return type;
  }

  virtual void
  reenable()
  {
    return;
  }

  virtual const char *get_protocol_tag() const = 0;

  NetProfileSM *low_profileSM;
  NetVConnection *vc;
  NetProfileSMType_t type;
  bool netTrace;
  bool globally_allocated;

private:
  NetProfileSM(const NetProfileSM &);
  NetProfileSM &operator=(const NetProfileSM &);
};

#endif /* __I_NET_PROFILESM_H__ */
