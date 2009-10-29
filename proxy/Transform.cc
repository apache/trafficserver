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

  @section thoughts Transform thoughts

    - Must be able to handle a chain of transformations.
    - Any transformation in the chain may fail.
      Failure options:
        - abort the client (if transformed data already sent)
        - serve the client the untransformed document
        - remove the failing transformation from the chain and attempt the transformation again (difficult to do)
        - never send untransformed document to client if client would not understand it (e.g. a set top box)
    - Must be able to change response header fields up until the point that TRANSFORM_READ_READY is sent to the user.

  @section usage Transform usage

    -# transformProcessor.open (cont, hooks); - returns "tvc", a TransformVConnection if 'hooks != NULL'
    -# tvc->do_io_write (cont, nbytes, buffer1);
    -# cont->handleEvent (TRANSFORM_READ_READY, NULL);
    -# tvc->do_io_read (cont, nbytes, buffer2);
    -# tvc->do_io_close ();

  @section visualization Transform visualization

  @verbatim
         +----+     +----+     +----+     +----+
    -IB->| T1 |-B1->| T2 |-B2->| T3 |-B3->| T4 |-OB->
         +----+     +----+     +----+     +----+
  @endverbatim

  Data flows into the first transform in the form of the buffer
  passed to TransformVConnection::do_io_write (IB). Data flows
  out of the last transform in the form of the buffer passed to
  TransformVConnection::do_io_read (OB). Between each transformation is
  another buffer (B1, B2 and B3).

  A transformation is a Continuation. The continuation is called with the
  event TRANSFORM_IO_WRITE to initialize the write and TRANSFORM_IO_READ
  to initialize the read.

*/

#ifndef INK_NO_TRANSFORM

#include "Config.h"
#include "P_Net.h"
#include "MimeTable.h"
#include "TransformInternal.h"
#include "HttpMessageBody.h"
#include "HdrUtils.h"
#include "Log.h"


#define ART                   1
#define AGIF                  2


TransformProcessor transformProcessor;


/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
TransformProcessor::start()
{
#ifdef PREFETCH
  prefetchProcessor.start();
#endif
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

VConnection *
TransformProcessor::open(Continuation * cont, APIHook * hooks)
{
  if (hooks) {
    return NEW(new TransformVConnection(cont, hooks));
  } else {
    return NULL;
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

INKVConnInternal *
TransformProcessor::null_transform(ProxyMutex * mutex)
{
  return NEW(new NullTransform(mutex));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

INKVConnInternal *
TransformProcessor::ftp_list_transform(ProxyMutex * mutex,
                                       HTTPHdr * req, IOBufferReader * ftp_message, const char *currentdir)
{
  return NEW(new FtpListTransform(mutex, req, ftp_message, currentdir));
}


/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

INKVConnInternal *
TransformProcessor::range_transform(ProxyMutex * mut,
                                    MIMEField * range_field, HTTPInfo * cache_obj, HTTPHdr * transform_resp, bool & b)
{
  RangeTransform *range_transform = NEW(new RangeTransform(mut, range_field, cache_obj, transform_resp));

  b = range_transform->is_range_unsatisfiable();

  if (b || range_transform->is_this_range_not_handled()) {
    delete range_transform;
    return NULL;
  }

  return range_transform;
}


/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

TransformTerminus::TransformTerminus(TransformVConnection * tvc)
:VConnection(tvc->mutex),
m_tvc(tvc), m_read_vio(), m_write_vio(), m_event_count(0), m_deletable(0), m_closed(0), m_called_user(0)
{
  SET_HANDLER(&TransformTerminus::handle_event);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

static inline void
dump_buffer(IOBufferReader * reader)
{
  IOBufferBlock *b = reader->get_current_block();
  int offset = reader->start_offset;
  char *s, *e;
  int avail;
  int err;

  while (b) {
    avail = b->read_avail();
    avail -= offset;
    if (avail <= 0) {
      offset = -avail;
    } else {
      s = b->start() + offset;
      e = b->end();

      ink_assert(avail == (e - s));

      while (s != e) {
        do {
          err = write(STDOUT_FILENO, s, e - s);
        } while ((err < 0) && (errno == EAGAIN));

        ink_assert(err >= 0);

        s += err;
      }
    }

    b = b->next;
  }
}


#define RETRY() \
    if (ink_atomic_increment ((int*) &m_event_count, 1) < 0) { \
        ink_assert (!"not reached"); \
    } \
    eventProcessor.schedule_in (this, HRTIME_MSECONDS (10), ET_NET); \
    return 0;


int
TransformTerminus::handle_event(int event, void *edata)
{
  int val;

  m_deletable = ((m_closed != 0) && (m_tvc->m_closed != 0));

  val = ink_atomic_increment((int *) &m_event_count, -1);

  Debug("transform", "[TransformTerminus::handle_event] event_count %d", m_event_count);

  if (val <= 0) {
    ink_assert(!"not reached");
  }

  m_deletable = m_deletable && (val == 1);

  if (m_closed != 0 && m_tvc->m_closed != 0) {
    if (m_deletable) {
      Debug("transform", "TransformVConnection destroy [0x%lx]", (long) m_tvc);
      delete m_tvc;
      return 0;
    }
  } else if (m_write_vio.op == VIO::WRITE) {
    if (m_read_vio.op == VIO::NONE) {
      if (!m_called_user) {
        Debug("transform", "TransformVConnection calling user: %d %d [0x%lx] [0x%lx]",
              m_event_count, event, (long) m_tvc, (long) m_tvc->m_cont);

        m_called_user = 1;
        m_tvc->m_cont->handleEvent(TRANSFORM_READ_READY, (void *) m_write_vio.nbytes);
      }
    } else {
      int towrite;

      MUTEX_TRY_LOCK(trylock1, m_write_vio.mutex, this_ethread());
      if (!trylock1) {
        RETRY();
      }

      MUTEX_TRY_LOCK(trylock2, m_read_vio.mutex, this_ethread());
      if (!trylock2) {
        RETRY();
      }

      if (m_closed != 0) {
        return 0;
      }

      if (m_write_vio.op == VIO::NONE) {
        return 0;
      }

      towrite = m_write_vio.ntodo();
      if (towrite > 0) {
        if (towrite > m_write_vio.get_reader()->read_avail()) {
          towrite = m_write_vio.get_reader()->read_avail();
        }
        if (towrite > m_read_vio.ntodo()) {
          towrite = m_read_vio.ntodo();
        }

        if (towrite > 0) {
          if (is_debug_tag_set("transform_data")) {
            printf("transform data start: %d", towrite);
            dump_buffer(m_write_vio.get_reader());
            printf("\ntransform data end\n");
          }

          m_read_vio.get_writer()->write(m_write_vio.get_reader(), towrite);
          m_read_vio.ndone += towrite;

          m_write_vio.get_reader()->consume(towrite);
          m_write_vio.ndone += towrite;
        }
      }

      if (m_write_vio.ntodo() > 0) {
        if (towrite > 0) {
          m_write_vio._cont->handleEvent(VC_EVENT_WRITE_READY, &m_write_vio);
        }
      } else {
        m_write_vio._cont->handleEvent(VC_EVENT_WRITE_COMPLETE, &m_write_vio);
      }

      // We could have closed on the write callback 
      if (m_closed != 0 && m_tvc->m_closed != 0) {
        return 0;
      }

      if (m_read_vio.ntodo() > 0) {
        if (m_write_vio.ntodo() <= 0) {
          m_read_vio._cont->handleEvent(VC_EVENT_EOS, &m_read_vio);
        } else if (towrite > 0) {
          m_read_vio._cont->handleEvent(VC_EVENT_READ_READY, &m_read_vio);
        }
      } else {
        m_read_vio._cont->handleEvent(VC_EVENT_READ_COMPLETE, &m_read_vio);
      }
    }
  } else {
    MUTEX_TRY_LOCK(trylock2, m_read_vio.mutex, this_ethread());
    if (!trylock2) {
      RETRY();
    }

    if (m_closed != 0) {
      // The terminus was closed, but the enclosing transform
      // vconnection wasn't. If the terminus was aborted then we
      // call the read_vio cont back with VC_EVENT_ERROR. If it
      // was closed normally then we call it back with
      // VC_EVENT_EOS. If a read operation hasn't been initiated
      // yet and we haven't called the user back then we call
      // the user back instead of the read_vio cont (which won't
      // exist).
      if (m_tvc->m_closed == 0) {
        if (m_closed == INK_VC_CLOSE_ABORT) {
          if (m_read_vio.op == VIO::NONE) {
            if (!m_called_user) {
              m_called_user = 1;
              m_tvc->m_cont->handleEvent(VC_EVENT_ERROR, NULL);
            }
          } else {
            m_read_vio._cont->handleEvent(VC_EVENT_ERROR, &m_read_vio);
          }
        } else {
          if (m_read_vio.op == VIO::NONE) {
            if (!m_called_user) {
              m_called_user = 1;
              m_tvc->m_cont->handleEvent(VC_EVENT_EOS, NULL);
            }
          } else {
            m_read_vio._cont->handleEvent(VC_EVENT_EOS, &m_read_vio);
          }
        }
      }
      return 0;
    }
  }

  return 0;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

VIO *
TransformTerminus::do_io_read(Continuation * c, int nbytes, MIOBuffer * buf)
{
  m_read_vio.buffer.writer_for(buf);
  m_read_vio.op = VIO::READ;
  m_read_vio.set_continuation(c);
  m_read_vio.nbytes = nbytes;
  m_read_vio.data = 0;
  m_read_vio.ndone = 0;
  m_read_vio.vc_server = this;

  if (ink_atomic_increment((int *) &m_event_count, 1) < 0) {
    ink_assert(!"not reached");
  }
  Debug("transform", "[TransformTerminus::do_io_read] event_count %d", m_event_count);

  eventProcessor.schedule_imm(this, ET_NET);

  return &m_read_vio;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

VIO *
TransformTerminus::do_io_write(Continuation * c, int nbytes, IOBufferReader * buf, bool owner)
{
  // In the process of eliminating 'owner' mode so asserting against it
  ink_assert(!owner);
  m_write_vio.buffer.reader_for(buf);
  m_write_vio.op = VIO::WRITE;
  m_write_vio.set_continuation(c);
  m_write_vio.nbytes = nbytes;
  m_write_vio.data = 0;
  m_write_vio.ndone = 0;
  m_write_vio.vc_server = this;

  if (ink_atomic_increment((int *) &m_event_count, 1) < 0) {
    ink_assert(!"not reached");
  }
  Debug("transform", "[TransformTerminus::do_io_write] event_count %d", m_event_count);

  eventProcessor.schedule_imm(this, ET_NET);

  return &m_write_vio;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
TransformTerminus::do_io_close(int error)
{
  if (ink_atomic_increment((int *) &m_event_count, 1) < 0) {
    ink_assert(!"not reached");
  }

  INK_WRITE_MEMORY_BARRIER;

  if (error != -1) {
    lerrno = error;
    m_closed = INK_VC_CLOSE_ABORT;
  } else {
    m_closed = INK_VC_CLOSE_NORMAL;
  }

  m_read_vio.op = VIO::NONE;
  m_read_vio.buffer.clear();

  m_write_vio.op = VIO::NONE;
  m_write_vio.buffer.clear();

  eventProcessor.schedule_imm(this, ET_NET);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
TransformTerminus::do_io_shutdown(ShutdownHowTo_t howto)
{
  if ((howto == IO_SHUTDOWN_READ) || (howto == IO_SHUTDOWN_READWRITE)) {
    m_read_vio.op = VIO::NONE;
    m_read_vio.buffer.clear();
  }

  if ((howto == IO_SHUTDOWN_WRITE) || (howto == IO_SHUTDOWN_READWRITE)) {
    m_write_vio.op = VIO::NONE;
    m_write_vio.buffer.clear();
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
TransformTerminus::reenable(VIO * vio)
{
  ink_assert((vio == &m_read_vio) || (vio == &m_write_vio));

  if (m_event_count == 0) {

    if (ink_atomic_increment((int *) &m_event_count, 1) < 0) {
      ink_assert(!"not reached");
    }
    Debug("transform", "[TransformTerminus::reenable] event_count %d", m_event_count);
    eventProcessor.schedule_imm(this, ET_NET);
  } else {
    Debug("transform", "[TransformTerminus::reenable] skipping due to " "pending events");
  }
}


/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

TransformVConnection::TransformVConnection(Continuation * cont, APIHook * hooks)
:VConnection(cont->mutex), m_cont(cont), m_terminus(this), m_closed(0)
{
  INKVConnInternal *xform;

  SET_HANDLER(&TransformVConnection::handle_event);

  ink_assert(hooks != NULL);

  m_transform = hooks->m_cont;
  while (hooks->m_link.next) {
    xform = (INKVConnInternal *) hooks->m_cont;
    hooks = hooks->m_link.next;
    xform->do_io_transform(hooks->m_cont);
  }
  xform = (INKVConnInternal *) hooks->m_cont;
  xform->do_io_transform(&m_terminus);

  Debug("transform", "TransformVConnection create [0x%lx]", (long) this);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

TransformVConnection::~TransformVConnection()
{
  // Clear the continuations in terminus VConnections so that
  //  mutex's get released (INKqa05596)
  m_terminus.m_read_vio.set_continuation(NULL);
  m_terminus.m_write_vio.set_continuation(NULL);
  m_terminus.mutex = NULL;
  this->mutex = NULL;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
TransformVConnection::handle_event(int event, void *edata)
{
  ink_assert(!"not reached");
  return 0;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

VIO *
TransformVConnection::do_io_read(Continuation * c, int nbytes, MIOBuffer * buf)
{
  Debug("transform", "TransformVConnection do_io_read: 0x%lx [0x%lx]", (long) c, (long) this);

  return m_terminus.do_io_read(c, nbytes, buf);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

VIO *
TransformVConnection::do_io_write(Continuation * c, int nbytes, IOBufferReader * buf, bool owner)
{
  Debug("transform", "TransformVConnection do_io_write: 0x%lx [0x%lx]", (long) c, (long) this);

  return m_transform->do_io_write(c, nbytes, buf);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
TransformVConnection::do_io_close(int error)
{
  Debug("transform", "TransformVConnection do_io_close: %d [0x%lx]", error, (long) this);

  if (error != -1) {
    m_closed = INK_VC_CLOSE_ABORT;
  } else {
    m_closed = INK_VC_CLOSE_NORMAL;
  }

  m_transform->do_io_close(error);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
TransformVConnection::do_io_shutdown(ShutdownHowTo_t howto)
{
  ink_assert(howto == IO_SHUTDOWN_WRITE);

  Debug("transform", "TransformVConnection do_io_shutdown: %d [0x%lx]", howto, (long) this);

  m_transform->do_io_shutdown(howto);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
TransformVConnection::reenable(VIO * vio)
{
  ink_assert(!"not reached");
}


/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

TransformControl::TransformControl()
:Continuation(new_ProxyMutex()), m_hooks(), m_tvc(NULL), m_read_buf(NULL), m_write_buf(NULL)
{
  SET_HANDLER(&TransformControl::handle_event);

  m_hooks.append(transformProcessor.null_transform(new_ProxyMutex()));
//     m_hooks.append (transformProcessor.null_transform (new_ProxyMutex ()));
//     m_hooks.append (transformProcessor.null_transform (new_ProxyMutex ()));
//     m_hooks.append (transformProcessor.null_transform (new_ProxyMutex ()));
//     m_hooks.append (transformProcessor.null_transform (new_ProxyMutex ()));
//     m_hooks.append (transformProcessor.null_transform (new_ProxyMutex ()));
//     m_hooks.append (transformProcessor.null_transform (new_ProxyMutex ()));
//     m_hooks.append (transformProcessor.null_transform (new_ProxyMutex ()));
//     m_hooks.append (transformProcessor.null_transform (new_ProxyMutex ()));
//     m_hooks.append (transformProcessor.null_transform (new_ProxyMutex ()));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
TransformControl::handle_event(int event, void *edata)
{
  switch (event) {
  case EVENT_IMMEDIATE:{
      char *s, *e;

      ink_assert(m_tvc == NULL);
      if (http_global_hooks && http_global_hooks->get(INK_HTTP_RESPONSE_TRANSFORM_HOOK)) {
        m_tvc = transformProcessor.open(this, http_global_hooks->get(INK_HTTP_RESPONSE_TRANSFORM_HOOK));
      } else {
        m_tvc = transformProcessor.open(this, m_hooks.get());
      }
      ink_assert(m_tvc != NULL);

      m_write_buf = new_MIOBuffer();
      s = m_write_buf->end();
      e = m_write_buf->buf_end();

      memset(s, 'a', e - s);
      m_write_buf->fill(e - s);

      m_tvc->do_io_write(this, 4 * 1024, m_write_buf->alloc_reader());
      break;
    }

  case TRANSFORM_READ_READY:{
      MIOBuffer *buf = new_empty_MIOBuffer();
      m_read_buf = buf->alloc_reader();
      m_tvc->do_io_read(this, INT_MAX, buf);
      break;
    }

  case VC_EVENT_READ_COMPLETE:
  case VC_EVENT_EOS:
    m_tvc->do_io_close();

    free_MIOBuffer(m_read_buf->mbuf);
    m_read_buf = NULL;

    free_MIOBuffer(m_write_buf);
    m_write_buf = NULL;
    break;

  case VC_EVENT_WRITE_COMPLETE:
    break;

  default:
    ink_assert(!"not reached");
    break;
  }

  return 0;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

NullTransform::NullTransform(ProxyMutex * _mutex)
:INKVConnInternal(NULL, _mutex), m_output_buf(NULL), m_output_reader(NULL), m_output_vio(NULL)
{
  SET_HANDLER(&NullTransform::handle_event);

  Debug("transform", "NullTransform create [0x%lx]", (long) this);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

NullTransform::~NullTransform()
{
  if (m_output_buf) {
    free_MIOBuffer(m_output_buf);
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
NullTransform::handle_event(int event, void *edata)
{
  handle_event_count(event);

  Debug("transform", "[NullTransform::handle_event] event count %d", m_event_count);

  if (m_closed) {
    if (m_deletable) {
      Debug("transform", "NullTransform destroy: %d [0x%lx]", m_output_vio ? m_output_vio->ndone : 0, (long) this);
      delete this;
    }
  } else {
    switch (event) {
    case VC_EVENT_ERROR:
      m_write_vio._cont->handleEvent(VC_EVENT_ERROR, &m_write_vio);
      break;
    case VC_EVENT_WRITE_COMPLETE:
      ink_assert(m_output_vio == (VIO *) edata);

      // The write to the output vconnection completed. This
      // could only be the case if the data being fed into us
      // has also completed.
      ink_assert(m_write_vio.ntodo() == 0);

      m_output_vc->do_io_shutdown(IO_SHUTDOWN_WRITE);
      break;
    case VC_EVENT_WRITE_READY:
    default:{
        int towrite;
        int avail;

        ink_assert(m_output_vc != NULL);

        if (!m_output_vio) {
          m_output_buf = new_empty_MIOBuffer();
          m_output_reader = m_output_buf->alloc_reader();
          m_output_vio = m_output_vc->do_io_write(this, m_write_vio.nbytes, m_output_reader);
        }

        MUTEX_TRY_LOCK(trylock, m_write_vio.mutex, this_ethread());
        if (!trylock) {
          retry(10);
          return 0;
        }

        if (m_closed) {
          return 0;
        }

        if (m_write_vio.op == VIO::NONE) {
          m_output_vio->nbytes = m_write_vio.ndone;
          m_output_vio->reenable();
          return 0;
        }

        towrite = m_write_vio.ntodo();
        if (towrite > 0) {
          avail = m_write_vio.get_reader()->read_avail();
          if (towrite > avail) {
            towrite = avail;
          }

          if (towrite > 0) {
            Debug("transform", "[NullTransform::handle_event] " "writing %d bytes to output", towrite);
            m_output_buf->write(m_write_vio.get_reader(), towrite);

            m_write_vio.get_reader()->consume(towrite);
            m_write_vio.ndone += towrite;
          }
        }

        if (m_write_vio.ntodo() > 0) {
          if (towrite > 0) {
            m_output_vio->reenable();
            m_write_vio._cont->handleEvent(VC_EVENT_WRITE_READY, &m_write_vio);
          }
        } else {
          m_output_vio->nbytes = m_write_vio.ndone;
          m_output_vio->reenable();
          m_write_vio._cont->handleEvent(VC_EVENT_WRITE_COMPLETE, &m_write_vio);
        }

        break;
      }
    }
  }

  return 0;
}


/*-------------------------------------------------------------------------
  Transforms incoming text formatted as an ftp directory listing into
  HTML. This transformation cannot actually be a plugin since a) it
  uses a non-standard method for getting prepended to the list of
  transformations, b) it is given a non-standard buffer containing a
  message to be inserted in the outgoing HTML and c) it uses the
  MimeTable for guessing the mime type given a filename in order to
  assign icons in a reasonable fashion. Reason c) could be eliminated
  by adding a mime guessing routine to the plugin, but reasons a) and
  b) are somewhat hard to overcome.
  -------------------------------------------------------------------------*/

FtpListTransform::FtpListTransform(ProxyMutex * _mutex,
                                   HTTPHdr * req, IOBufferReader * ftp_message, const char *currentdir)
:INKVConnInternal(NULL, _mutex),
m_req(req),
m_ftp_message(ftp_message),
m_output_reader(NULL),
m_output_buf(NULL),
m_output_vio(NULL),
m_url(NULL),
m_url_length(0),
m_done(0),
m_header_needed(1), m_trailer_needed(1), m_icon_needed(0), m_current_dir(currentdir), parent_directory_added_flag(0)
{
  const char *val;
  char *old_path = 0;
  int old_path_len = 0;
  int val_len;

  SET_HANDLER(&FtpListTransform::handle_event);

  mime_scanner_init(&m_scanner);

  // this is the place we hack the url to add in absolute path
  // INKqa 05662 & INKqa 06110
  val = m_req->url_get()->path_get(&val_len);
  if (val && val_len > 0) {
    old_path = xstrndup(val, val_len);
    old_path_len = val_len;
  }


  if (currentdir) {
    int currentdir_len = strlen(currentdir);
    if (currentdir_len > 1) {
      m_req->url_get()->path_set(currentdir + 1, currentdir_len - 1);
    }
  }

  m_url = m_req->url_get()->string_get(NULL, &m_url_length);

  if (m_current_dir == NULL) {
    if (old_path) {
      m_current_dir = (const char *) old_path;
      old_path = NULL;
    } else {
      char *temp = (char *) xmalloc(2);
      *temp = '/';
      *(temp + 1) = '\0';
      m_current_dir = (const char *) temp;
    }
  }
  // INKQA05662 & INKqa06110
  if (old_path) {
    m_req->url_get()->path_set(old_path, old_path_len);
    xfree(old_path);
  } else
    m_req->url_get()->path_set("", 0);

  val = req->value_get(MIME_FIELD_USER_AGENT, MIME_LEN_USER_AGENT, &val_len);

  // if the user agent field starts with "Mozilla/" we know
  // this is either a netscape or msie user agent
  const char *val_new;
  if (val && ptr_len_ncmp(val, val_len, "Mozilla/", 8) == 0) {
    // search for the word "compatible" in the remaining string.
    val += 8;
    val_len -= 8;
    for (;;) {
      val_new = ptr_len_pbrk(val, val_len, "Cc");

      if (val_new) {
        val_len -= val_new - val;
        ink_assert(val_len > 0);
        val = val_new;
      } else {
        // We're out of string
        break;
      }

      // Check for the word "compatible" now that we've
      //   something starting with a 'c'
      if (ptr_len_ncasecmp(val, val_len, "compatible", 10) == 0) {
        break;
      }

      val += 1;
      val_len--;
    }

    // if we didn't find the word "compatible" then this is
    // a netscape user agent.
    if (!val_new) {
      // user agent is Netscape
      m_icon_needed = 1;
    } else {
      // if the string "MSIE" exists in the rest of the
      // user agent field we know we are dealing with an
      // MSIE user agent. otherwise the user agent is unknown.
      val += 10;
      val_len -= 10;
      for (;;) {
        val_new = ptr_len_pbrk(val, val_len, "Mm");

        if (val_new) {
          val_len -= val_new - val;
          ink_assert(val_len > 0);
          val = val_new;
        } else {
          // We're out of string
          break;
        }

        // Check for the word "msie" now that we've
        //   something starting with a 'm'
        if (ptr_len_ncasecmp(val, val_len, "msie", 4) == 0) {
          break;
        }
        val += 1;
        val_len -= 1;
      }

      if (val_new) {
        // user agent is MS internet explorer
      }
    }
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

FtpListTransform::~FtpListTransform()
{
  if (m_output_buf) {
    free_MIOBuffer(m_output_buf);
  }

  if (m_ftp_message) {
    MIOBuffer *mbuf = m_ftp_message->mbuf;
    m_ftp_message->dealloc();

    // when we start up, Ftp give us
    // control of both the reader AND the buffer.  We
    // need to free the buffer.
    free_MIOBuffer(mbuf);
  }

  if (m_url) {
    xfree(m_url);
  }

  mime_scanner_clear(&m_scanner);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
FtpListTransform::handle_event(int event, void *edata)
{
  handle_event_count(event);

  if (m_closed) {
    if (m_deletable) {
      Debug("transform", "FtpListTransform destroy: %d", m_output_vio ? m_output_vio->ndone : 0);
      delete this;
    }
  } else {
    switch (event) {
    case VC_EVENT_ERROR:
      m_write_vio._cont->handleEvent(VC_EVENT_ERROR, &m_write_vio);
      break;
    case VC_EVENT_WRITE_COMPLETE:
      ink_assert(m_output_vio == (VIO *) edata);

      // The write to the output vconnection completed. This
      // could only be the case if the data being fed into us
      // has also completed.
      ink_assert(m_write_vio.ntodo() == 0);

      m_output_vc->do_io_shutdown(IO_SHUTDOWN_WRITE);
      break;
    case VC_EVENT_WRITE_READY:
    default:
      ink_assert(m_output_vc != NULL);

      if (!m_output_vio) {
        m_output_buf = new_empty_MIOBuffer();
        m_output_reader = m_output_buf->alloc_reader();
        m_output_vio = m_output_vc->do_io_write(this, INT_MAX, m_output_reader);
        add_header();
      }

      MUTEX_TRY_LOCK(trylock, m_write_vio.mutex, this_ethread());
      if (!trylock) {
        retry(10);
        return 0;
      }

      if (m_closed) {
        return 0;
      }

      if (m_write_vio.op == VIO::NONE) {
        add_trailer();
        m_output_vio->nbytes = m_done;
        m_output_vio->reenable();
        return 0;
      }

      add_entries();
      break;
    }
  }

  return 0;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
FtpListTransform::add_header()
{
  static char l1[] = "<!-- HTML listing generated by Inktomi Traffic Server -->\n";
  static char l2s[] = "<!-- ";
  static char l2e[] = " -->\n";

  static char l3[] = "<HTML>\n<HEAD>\n<TITLE>\nFtp Directory\n";
  static char l4[] = "\n</TITLE>\n</HEAD>\n<BODY>\n";
  static char l5[] = "<HR><PRE>\n";
  static char l6[] = "</PRE><HR>\n";

  static char h1[] = "<H2>\nCurrent Directory ";
  static char h2[] = "\n</H2>\n";
  static char h3[] = "<PRE>\n";

  int avail;

  if (m_header_needed) {
    char buf[33];
    int l;

    m_header_needed = 0;

    m_done = m_output_buf->write(l1, sizeof(l1) - 1);

    m_done += m_output_buf->write(l2s, sizeof(l2s) - 1);
    l = mime_format_date(buf, time(0));
    m_done += m_output_buf->write(buf, l - 1);
    m_done += m_output_buf->write(l2e, sizeof(l2e) - 1);

    m_done += m_output_buf->write(l3, sizeof(l3) - 1);
    m_done += m_output_buf->write(m_url, m_url_length);
    m_done += m_output_buf->write(l4, sizeof(l4) - 1);

    m_done += m_output_buf->write(h1, sizeof(h1) - 1);
    if (m_current_dir != NULL)
      m_done += m_output_buf->write(m_current_dir, strlen(m_current_dir));
    m_done += m_output_buf->write(h2, sizeof(h2) - 1);

    if (m_ftp_message && (avail = m_ftp_message->read_avail()) > 0) {
      m_done += m_output_buf->write(l5, sizeof(l5) - 1);
      m_done += m_output_buf->write(m_ftp_message, avail);
      m_done += m_output_buf->write(l6, sizeof(l6) - 1);
    }

    m_done += m_output_buf->write(h3, sizeof(h3) - 1);
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
FtpListTransform::add_trailer()
{
  static char l1[] = "</PRE>\n</BODY>\n</HTML>\n";

  if (m_trailer_needed) {
    m_trailer_needed = 0;
    m_done += m_output_buf->write(l1, sizeof(l1) - 1);
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
FtpListTransform::add_date(struct tm *tp)
{
  static const char *months[] = { "Jan ", "Feb ", "Mar ", "Apr ", "May ", "Jun ",
    "Jul ", "Aug ", "Sep ", "Oct ", "Nov ", "Dec "
  };
  static const char *digitstrs[] = {
    "00", "01", "02", "03", "04", "05", "06", "07", "08", "09",
    "10", "11", "12", "13", "14", "15", "16", "17", "18", "19",
    "20", "21", "22", "23", "24", "25", "26", "27", "28", "29",
    "30", "31", "32", "33", "34", "35", "36", "37", "38", "39",
    "40", "41", "42", "43", "44", "45", "46", "47", "48", "49",
    "50", "51", "52", "53", "54", "55", "56", "57", "58", "59",
    "60", "61", "62", "63", "64", "65", "66", "67", "68", "69",
    "70", "71", "72", "73", "74", "75", "76", "77", "78", "79",
    "80", "81", "82", "83", "84", "85", "86", "87", "88", "89",
    "90", "91", "92", "93", "94", "95", "96", "97", "98", "99",
  };

  m_done += m_output_buf->write(months[tp->tm_mon], 4);

  int day = tp->tm_mday;
  if ((day<0) || (day> 31)) {
    day = 0;
  }
  m_done += m_output_buf->write(digitstrs[day], 2);
  m_done += m_output_buf->write(" ", 1);

  if (tp->tm_year > 0) {
    int year = tp->tm_year;
    if (year < 200) {
      if (year < 100) {
        m_done += m_output_buf->write(digitstrs[19], 2);
      } else {
        m_done += m_output_buf->write(digitstrs[20], 2);
        year -= 100;
      }
      m_done += m_output_buf->write(digitstrs[year], 2);
    } else {
      // this should not happen, just to get rid of coverity complain.
      m_done += m_output_buf->write("????", 4);
    }
    m_done += m_output_buf->write("   ", 3);
  } else {
    int hour = tp->tm_hour;
    int min = tp->tm_min;

    if ((hour<0) || (hour>= 24)) {
      hour = 0;
    }
    if ((min<0) || (min>= 60)) {
      min = 0;
    }

    m_done += m_output_buf->write(digitstrs[hour], 2);
    m_done += m_output_buf->write(":", 1);
    m_done += m_output_buf->write(digitstrs[min], 2);
    m_done += m_output_buf->write("  ", 2);
  }
}

/*-------------------------------------------------------------------------
  <size> <month> <day> <year> <name ...>
  <size> <month> <day> <hh:mm> <name ...> 
  -------------------------------------------------------------------------*/
void
FtpListTransform::add_entry(const char *s, const char *e)
{
  const char *cur = s;
  const char *end = e;

  while ((cur != end) && ParseRules::is_ws(*cur)) {
    cur += 1;
  }
  if (cur == end) {
    FtpListTransform::send_error_message(s, e, cur);
    return;
  }

  if (ParseRules::is_digit(*cur)) {
    // listing is starting with a number
    // assume that its ms.
    parse_ms_listing(s, e, cur);
    return;
  }
  // otherwise assume its a unix listing
  parse_unix_listing(s, e, cur);
}

void
FtpListTransform::parse_ms_listing(const char *s, const char *e, const char *cur)
{
  const char *end = e;
  const char *te;
  const char *ne;
  const char *ns;
  const char *nm;
  int type = -1;
  struct tm tp;
  int size = -1;


  tp.tm_year = 0;
  tp.tm_mon = 0;
  tp.tm_mday = 0;
  tp.tm_hour = 0;
  tp.tm_min = 0;
  tp.tm_sec = 0;

  // parse month
  te = cur;
  while ((te != end) && (*te != '-')) {
    te += 1;
  }
  if (te == end) {
    FtpListTransform::send_error_message(s, e, cur);
    return;
  }
  if (!mime_parse_integer(cur, te, &tp.tm_mon)) {
    FtpListTransform::send_error_message(s, e, cur);
    return;
  }


  // parse day
  cur = te + 1;
  te = cur;
  while ((te != end) && (*te != '-')) {
    te += 1;
  }
  if (te == end) {
    FtpListTransform::send_error_message(s, e, cur);
    return;
  }
  if (!mime_parse_integer(cur, te, &tp.tm_mday)) {
    FtpListTransform::send_error_message(s, e, cur);
    return;
  }
  // need to -1 to get the correct month
  // to adjust to the months table in add_date function
  tp.tm_mon--;

  // parse year
  cur = te + 1;
  te = cur;
  while ((te != end) && !ParseRules::is_ws(*te)) {
    te += 1;
  }
  if (te == end) {
    FtpListTransform::send_error_message(s, e, cur);
    return;
  }
  if (!mime_parse_integer(cur, te, &tp.tm_year)) {
    FtpListTransform::send_error_message(s, e, cur);
    return;
  }
  // skip the whitespaces
  cur = te + 1;
  while ((cur != end) && ParseRules::is_ws(*cur)) {
    cur += 1;
  }
  if (cur == end) {
    FtpListTransform::send_error_message(s, e, cur);
    return;
  }
  // parse time - first the hour
  te = cur;
  while ((te != end) && (*te != ':') && !ParseRules::is_ws(*te)) {
    te += 1;
  }
  if (te == end) {
    FtpListTransform::send_error_message(s, e, cur);
    return;
  }
  if (!mime_parse_integer(cur, te, &tp.tm_hour)) {
    FtpListTransform::send_error_message(s, e, cur);
    return;
  }
  // now the minutes
  cur = te + 1;
  te = cur;

  // should keep AM/PM
  while ((te != end) && ParseRules::is_digit(*te)) {
    te += 1;
  }
  if (te == end) {
    FtpListTransform::send_error_message(s, e, cur);
    return;
  }
  if (!mime_parse_integer(cur, te, &tp.tm_min)) {
    FtpListTransform::send_error_message(s, e, cur);
    return;
  }
  // am or pm?
  // now te should be 'A' or 'P'
  cur = te;

  if (ParseRules::is_alpha(*cur)) {
    if (*cur == 'P')
      if (tp.tm_hour < 12)
        tp.tm_hour += 12;
    cur += 2;
  }
  // skip the whitespaces
  while ((cur != end) && ParseRules::is_ws(*cur)) {
    cur += 1;
  }
  if (cur == end) {
    FtpListTransform::send_error_message(s, e, cur);
    return;
  }
  // ok, now the puppy could be a directory,
  // or it could be the size of the file.
  if (ParseRules::is_digit(*cur)) {
    // looks like a size to me, amigo
    if (!mime_parse_integer(cur, end, &size)) {
      FtpListTransform::send_error_message(s, e, cur);
      return;
    }
    // regular file
    type = S_IFREG;
  } else {
    if (*cur != '<') {
      FtpListTransform::send_error_message(s, e, cur);
      return;
    }
    cur += 1;
    while ((cur != end) && ParseRules::is_alpha(*cur)) {
      cur += 1;
    }
    if (cur == end) {
      FtpListTransform::send_error_message(s, e, cur);
      return;
    }
    if (*cur != '>') {
      FtpListTransform::send_error_message(s, e, cur);
      return;
    }
    cur += 1;
    // directory
    type = S_IFDIR;
  }

  // skip whitespaces
  while ((cur != end) && ParseRules::is_ws(*cur)) {
    cur += 1;
  }
  if (cur == end) {
    FtpListTransform::send_error_message(s, e, cur);
    return;
  }
  //parse name
  ns = cur;
  ne = ns;
  nm = ns;

  // for MS FTP server, directory and file names could have 
  // white spaces in it
  if (!ParseRules::is_ws(*ne)) {
    while ((ne != end) && (*ne != '\r') && (*ne != '\n')) {
      if (!ParseRules::is_ws(*ne)) {
        // nm to keep track of the last non-ws char
        // using nm in case there are redundant trailing ws
        nm = ne;
      }

      ne += 1;
    }
  }
  // done
  if (ns == ne) {
    return;
  }
  // ignoring trailing white spaces
  ne = nm + 1;
  // . or ..
  if ((ns[0] == '.') && ((&ns[1] == ne) || ((ns[1] == '.') && (&ns[2] == ne)))) {
    return;
  }

  write_html_link(s, e, cur, type, ne, ns, &tp, size);
}

void
FtpListTransform::parse_unix_listing(const char *s, const char *e, const char *cur)
{
  const char *end = e;

  const char *ss = NULL;
  const char *ns = NULL;
  const char *ne = NULL;
  const char *nm = NULL;
  const char *ts = NULL;
  const char *te = NULL;
  int type = -1;
  int size = -1;
  struct tm tp;


  // parse file type
  switch (*cur) {
  case 'd':
    type = S_IFDIR;
    break;
  case 'l':
    type = S_IFLNK;
    break;
  case 'b':
  case 'c':
  case 'p':
  case '-':
  case '|':
    type = S_IFREG;
    break;
  default:
    FtpListTransform::send_error_message(s, e, cur);
    return;
  }

  tp.tm_year = 0;
  tp.tm_mon = 0;
  tp.tm_mday = 0;
  tp.tm_hour = 0;
  tp.tm_min = 0;
  tp.tm_sec = 0;

  // parse month
  for (;;) {
    while ((cur != end) && !ParseRules::is_alpha(*cur)) {
      cur += 1;
    }
    if (cur == end) {
      FtpListTransform::send_error_message(s, e, cur);
      return;
    }

    if (mime_parse_month(cur, end, &tp.tm_mon)) {
      break;
    }

    while ((cur != end) && !ParseRules::is_ws(*cur)) {
      cur += 1;
    }
    if (cur == end) {
      FtpListTransform::send_error_message(s, e, cur);
      return;
    }
  }

  // parse size
  ss = cur;
  while ((ss != s) && !ParseRules::is_digit(*ss)) {
    ss -= 1;
  }
  if (ss == s) {
    FtpListTransform::send_error_message(s, e, cur);
    return;
  }
  while ((ss != s) && ParseRules::is_digit(*ss)) {
    ss -= 1;
  }
  ss += 1;
  if (!mime_parse_integer(ss, cur, &size)) {
    FtpListTransform::send_error_message(s, e, cur);
    return;
  }
  // parse day
  if (!mime_parse_mday(cur, end, &tp.tm_mday)) {
    FtpListTransform::send_error_message(s, e, cur);
    return;
  }

  while ((cur != end) && ParseRules::is_ws(*cur)) {
    cur += 1;
  }
  if (cur == end) {
    FtpListTransform::send_error_message(s, e, cur);
    return;
  }
  // parse time
  ts = cur;
  te = ts;

  while ((te != end) && (*te != ':') && !ParseRules::is_ws(*te)) {
    te += 1;
  }

  if ((te == end) || (*te != ':')) {
    // parse year
    if (!mime_parse_year(ts, te, &tp.tm_year)) {
      FtpListTransform::send_error_message(s, e, cur);
      return;
    }
  } else {
    // parse hours and minutes
    if (!mime_parse_integer(ts, te, &tp.tm_hour)) {
      FtpListTransform::send_error_message(s, e, cur);
      return;
    }
    te += 1;
    if (!mime_parse_integer(te, end, &tp.tm_min)) {
      FtpListTransform::send_error_message(s, e, cur);
      return;
    }
  }

  cur = te;

  // parse name
  while ((cur != end) && ParseRules::is_ws(*cur)) {
    cur += 1;
  }
  if (cur == end) {
    FtpListTransform::send_error_message(s, e, cur);
    return;
  }
  // same thing for Unix based Ftp server!
  ns = cur;
  nm = ne = ns;

  // ftp.microsoft.com sends back Unix format containing MS file names!
  if (!ParseRules::is_ws(*ne)) {
    while ((ne != end) && (*ne != '\r') && (*ne != '\n')) {
      if (!ParseRules::is_ws(*ne)) {
        // nm to keep track of the last non-ws char
        // using nm in case there are redundant trailing ws
        nm = ne;
      }

      ne += 1;
    }
  }
  // done
  if (ns == ne) {
    return;
  }
  //ignoring trailing white spaces
  ne = nm + 1;
  // . or ..
  if ((ns[0] == '.') && ((&ns[1] == ne) || ((ns[1] == '.') && (&ns[2] == ne)))) {
    return;
  }

  write_html_link(s, e, cur, type, ne, ns, &tp, size);
}

void
FtpListTransform::write_html_link(const char *s,
                                  const char *e,
                                  const char *cur, int type, const char *ne, const char *ns, struct tm *tp, int size)
{
  static char icon_fmt[] = "<IMG ALIGN=absbottom BORDER=0 SRC=\"internal-gopher-%s\" ALT=\"[%s] \">";
  static char directory[] = "Directory";
  static char symbolic_link[] = "Symbolic link";
  static char href_start[] = "<A HREF=\"";
  static char href_end[] = "\">";
  static const int entry_type_length = sizeof(symbolic_link) - 1;
  static const int size_length = 12;
  static const int date_length = 14;
  static const int name_length = 80 - (entry_type_length + size_length + date_length + 1);
  char tmp_buf[256];
  const char *ne_real, *ne_real_forward;        // handle symbolic link

  // write the html link
  m_done += m_output_buf->write(href_start, sizeof(href_start) - 1);
  m_done += m_output_buf->write(m_url, m_url_length);
  if (m_url[m_url_length - 1] != '/') {
    m_done += m_output_buf->write("/", 1);
  }
  ne_real = ns;
  for (ne_real = ns, ne_real_forward = ns + 1; ne_real != ne; ne_real++, ne_real_forward++) {
    if (ne_real_forward == ne) {
      ne_real = ne;
      break;
    }
    // symbolic link
    if (*ne_real == '-' && *ne_real_forward == '>') {
      // look for the last non white space char
      ne_real--;
      for (; ne_real != ns; ne_real--) {
        if (*ne_real != ' ') {
          ne_real++;
          break;
        }
      }
      break;
    }
  }
  m_done += m_output_buf->write(ns, ne_real - ns);
  if (S_ISDIR(type) && (ne[-1] != '/')) {
    m_done += m_output_buf->write("/", 1);
  }
  m_done += m_output_buf->write(href_end, sizeof(href_end) - 1);

  if (m_icon_needed) {
    const char *icon;
    const char *alt;

    if (S_ISDIR(type) || S_ISLNK(type)) {
      icon = "menu";
      alt = "DIR";
    } else {
      if ((ne - ns) > (int) (sizeof(tmp_buf) - 1)) {
        icon = "unknown";
      } else {
        MimeTableEntry *entry;
        memcpy(tmp_buf, ns, ne - ns);
        tmp_buf[ne - ns] = '\0';
        entry = mimeTable.get_entry_path(tmp_buf);
        icon = entry ? entry->icon : "unknown";
      }
      alt = "FILE";
    }

    snprintf(tmp_buf, sizeof(tmp_buf), icon_fmt, icon, alt);
    m_done += m_output_buf->write(tmp_buf, strlen(tmp_buf));
  }
  m_done += m_output_buf->write(ns, ne - ns);
  m_done += m_output_buf->write("</A>  ", 6);

  // fill space between the name and the size
  if ((ne - ns) < (name_length - 2)) {
    memset(tmp_buf, ' ', name_length - 2 - (ne - ns));
    m_done += m_output_buf->write(tmp_buf, name_length - 2 - (ne - ns));
  } else {
    memset(tmp_buf, ' ', 2);
    m_done += m_output_buf->write(tmp_buf, 2);
  }

  // write the size
  if (S_ISREG(type)) {
    if ((size >= 0) && (size < 1024)) {
      snprintf(tmp_buf, sizeof(tmp_buf), "%4d bytes  ", size);
    } else if (size < (1024 * 1024)) {
      snprintf(tmp_buf, sizeof(tmp_buf), "%4d KB     ", (int) (size / 1024));
    } else if (size < (1024 * 1024 * 1024)) {
      snprintf(tmp_buf, sizeof(tmp_buf), "%4d MB     ", (int) (size / (1024 * 1024)));
    } else {
      snprintf(tmp_buf, sizeof(tmp_buf), "%4d GB     ", (int) (size / (1024 * 1024 * 1024)));
    }
    m_done += m_output_buf->write(tmp_buf, strlen(tmp_buf));
  } else {
    memset(tmp_buf, ' ', size_length);
    m_done += m_output_buf->write(tmp_buf, size_length);
  }

  // write the date
  add_date(tp);

  // write the entry type
  if (S_ISDIR(type)) {
    m_done += m_output_buf->write(directory, sizeof(directory) - 1);
  } else if (S_ISLNK(type)) {
    m_done += m_output_buf->write(symbolic_link, sizeof(symbolic_link) - 1);
  }

  m_done += m_output_buf->write("\n", 1);
  return;
}

void
FtpListTransform::send_error_message(const char *s, const char *e, const char *cur)
{
  const char *end = e;

  cur = s;
  while ((cur != end) && !ParseRules::is_ws(*cur)) {
    cur += 1;
  }

  // discard the "total" message at the beginning
  if (((cur - s) != 5) || (strncasecmp(s, "total", 5) != 0))
    m_done += m_output_buf->write((char *) s, e - s);
}

// this function specializes in adding parent directory
// Now we add the ftp URL as the link, instead of ".."
void
FtpListTransform::add_parent_directory()
{
  static char icon_fmt[] = "<IMG ALIGN=absbottom BORDER=0 SRC=\"internal-gopher-%s\" ALT=\"[%s] \">";
  static char href_start[] = "<A HREF=\"";
  static char href_end[] = "\">";
  char tmp_buf[256];
  int parent_len = m_url_length - 1;
  int i;

  m_done += m_output_buf->write("\n", 1);
  m_done += m_output_buf->write(href_start, sizeof(href_start) - 1);
  // logic on deciding parent directory should be here:
  // char tmp_buf[256];
  // path = m_hacked_url.path_get();
  if (!m_current_dir || (strlen(m_current_dir) == 1 && m_current_dir[0] == '/')) {
    m_done += m_output_buf->write(m_url, m_url_length);
  } else {
    // skip the last char in m_url, going backwards
    // to find the last '/' in m_url
    for (i = m_url_length - 2; i >= 0 && m_url[i] != '/'; i--)
      parent_len--;
    if (parent_len > 0)
      m_done += m_output_buf->write(m_url, parent_len);
  }
  m_done += m_output_buf->write(href_end, sizeof(href_end) - 1);
  if (m_icon_needed) {
    snprintf(tmp_buf, sizeof(tmp_buf), icon_fmt, "menu", "DIR");
    m_done += m_output_buf->write(tmp_buf, strlen(tmp_buf));
  }
  m_done += m_output_buf->write("Parent Directory</A>\n", 21);
}


/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
FtpListTransform::add_entries()
{
  IOBufferReader *reader = m_write_vio.get_reader();
  IOBufferBlock *blk = reader->block;
  int offset = reader->start_offset;
  const char *s, *e;
  const char *ls, *le;
  int ntodo;
  int avail;
  int done;
  int err;
  bool output_share;

  done = m_done;

  if (!parent_directory_added_flag) {
    add_parent_directory();
    parent_directory_added_flag = 1;
  }

  while (((ntodo = m_write_vio.ntodo()) > 0) && blk) {
    avail = blk->read_avail();
    avail -= offset;
    if (avail <= 0) {
      offset = -avail;
      blk = blk->next;
    } else {
      if (avail > ntodo) {
        avail = ntodo;
      }

      s = blk->_start + offset;
      e = s + avail;

      err = mime_scanner_get(&m_scanner, &s, e, &ls, &le, &output_share, (avail == ntodo), MIME_SCANNER_TYPE_LINE);

      m_write_vio.ndone += (s - (blk->_start + offset));
      reader->consume(s - (blk->_start + offset));

      if (err == PARSE_OK) {
        add_entry(ls, le);
      }

      blk = reader->block;
      offset = reader->start_offset;
    }
  }

  if (ntodo > 0) {
    if (done < m_done) {
      m_output_vio->reenable();
    }
    m_write_vio._cont->handleEvent(VC_EVENT_WRITE_READY, &m_write_vio);
  } else {
    add_trailer();
    m_output_vio->nbytes = m_done;
    m_output_vio->reenable();
    m_write_vio._cont->handleEvent(VC_EVENT_WRITE_COMPLETE, &m_write_vio);
  }
}



/*-------------------------------------------------------------------------
  Reasons the JG transform cannot currently be a plugin:
    a) Uses the config system
       - Easily avoided by using the plugin.config file to pass the config
         values as parameters to the plugin initialization routine.
    b) Uses the stat system
       - FIXME: should probably solve this.
  -------------------------------------------------------------------------*/

/* the JG transform is now a plugin. All the JG code,
   config variables and stats are removed from Transform.cc */


/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
TransformTest::run()
{
  if (is_action_tag_set("transform_test")) {
    eventProcessor.schedule_imm(NEW(new TransformControl()), ET_NET);
  }
}



///////////////////////////////////////////////////////////////////
/// RangeTransform implementation
/// handling Range requests from clients
///////////////////////////////////////////////////////////////////

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline static int
num_chars_for_int(int i)
{
  int k;

  if (i < 0)
    return 0;

  k = 1;

  while ((i /= 10) != 0)
    k++;

  return k;
}


/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

RangeTransform::RangeTransform(ProxyMutex * mut, MIMEField * range_field,
                               HTTPInfo * cache_obj, HTTPHdr * transform_resp)
:INKVConnInternal(NULL, mut),
m_output_buf(NULL),
m_output_reader(NULL),
m_range_field(range_field),
m_transform_resp(transform_resp),
m_output_vio(NULL),
m_unsatisfiable_range(true),
m_not_handle_range(false),
m_num_range_fields(0),
m_current_range(0), m_content_type(NULL), m_content_type_len(0), m_ranges(NULL), m_output_cl(0), m_done(0)
{
  SET_HANDLER(&RangeTransform::handle_event);

  m_content_type = cache_obj->
    response_get()->value_get(MIME_FIELD_CONTENT_TYPE, MIME_LEN_CONTENT_TYPE, &m_content_type_len);

  m_content_length = cache_obj->object_size_get();
  m_num_chars_for_cl = num_chars_for_int(m_content_length);

  parse_range_and_compare();
  calculate_output_cl();

  Debug("transform_range", "RangeTransform creation finishes");
}


/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

RangeTransform::~RangeTransform()
{
  if (m_ranges)
    delete[]m_ranges;
  if (m_output_buf)
    free_MIOBuffer(m_output_buf);
}


/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
RangeTransform::parse_range_and_compare()
{
  // note: unsatisfiable_range is initialized to true in constructor
  int prev_good_range, i;
  const char *value;
  int value_len;
  HdrCsvIter csv;
  const char *s, *e;

  if (m_content_length <= 0)
    return;

  ink_assert(m_range_field != NULL);

  m_num_range_fields = 0;
  value = csv.get_first(m_range_field, &value_len);

  while (value) {
    m_num_range_fields++;
    value = csv.get_next(&value_len);
  }

  if (m_num_range_fields <= 0)
    return;

  m_ranges = NEW(new RangeRecord[m_num_range_fields]);

  value = csv.get_first(m_range_field, &value_len);

  i = 0;
  prev_good_range = -1;
  // Currently HTTP/1.1 only defines bytes Range
  if (ptr_len_ncmp(value, value_len, "bytes=", 6) == 0) {
    while (value) {
      // If delimiter '-' is missing
      if (!(e = (const char *) memchr(value, '-', value_len))) {
        value = csv.get_next(&value_len);
        i++;
        continue;
      }

      s = value;
      mime_parse_integer(s, e, &m_ranges[i]._start);

      e++;
      s = e;
      e = value + value_len;
      mime_parse_integer(s, e, &m_ranges[i]._end);

      // check and change if necessary whether this is a right entry
      // the last _end bytes are required
      if (m_ranges[i]._start == -1 && m_ranges[i]._end > 0) {
        if (m_ranges[i]._end > m_content_length)
          m_ranges[i]._end = m_content_length;

        m_ranges[i]._start = m_content_length - m_ranges[i]._end;
        m_ranges[i]._end = m_content_length - 1;
      }
      // open start
      else if (m_ranges[i]._start >= 0 && m_ranges[i]._end == -1) {
        if (m_ranges[i]._start >= m_content_length)
          m_ranges[i]._start = -1;
        else
          m_ranges[i]._end = m_content_length - 1;
      }
      // "normal" Range - could be wrong if _end<_start
      else if (m_ranges[i]._start >= 0 && m_ranges[i]._end >= 0) {
        if (m_ranges[i]._start > m_ranges[i]._end || m_ranges[i]._start >= m_content_length)
          m_ranges[i]._start = m_ranges[i]._end = -1;
        else if (m_ranges[i]._end >= m_content_length)
          m_ranges[i]._end = m_content_length - 1;
      }

      else
        m_ranges[i]._start = m_ranges[i]._end = -1;

      // this is a good Range entry
      if (m_ranges[i]._start != -1) {
        if (m_unsatisfiable_range) {
          m_unsatisfiable_range = false;
          // initialize m_current_range to the first good Range
          m_current_range = i;
        }
        // currently we don't handle out-of-order Range entry
        else if (prev_good_range >= 0 && m_ranges[i]._start <= m_ranges[prev_good_range]._end) {
          m_not_handle_range = true;
          break;
        }

        prev_good_range = i;
      }

      value = csv.get_next(&value_len);
      i++;
    }
  }
}


/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
RangeTransform::handle_event(int event, void *edata)
{
  handle_event_count(event);

  if (m_closed) {
    if (m_deletable) {
      Debug("transform_range", "RangeTransform destroy: %d", m_output_vio ? m_output_vio->ndone : 0);
      delete this;
    }
  } else {
    switch (event) {
    case VC_EVENT_ERROR:
      m_write_vio._cont->handleEvent(VC_EVENT_ERROR, &m_write_vio);
      break;
    case VC_EVENT_WRITE_COMPLETE:
      ink_assert(m_output_vio == (VIO *) edata);
      m_output_vc->do_io_shutdown(IO_SHUTDOWN_WRITE);
      break;
    case VC_EVENT_WRITE_READY:
    default:
      ink_assert(m_output_vc != NULL);

      if (!m_output_vio) {
        m_output_buf = new_empty_MIOBuffer();
        m_output_reader = m_output_buf->alloc_reader();
        m_output_vio = m_output_vc->do_io_write(this, m_output_cl, m_output_reader);

        change_response_header();

        if (m_num_range_fields > 1) {
          add_boundary(false);
          add_sub_header(m_current_range);
        }
      }

      MUTEX_TRY_LOCK(trylock, m_write_vio.mutex, this_ethread());
      if (!trylock) {
        retry(10);
        return 0;
      }

      if (m_closed) {
        return 0;
      }

      if (m_write_vio.op == VIO::NONE) {
        m_output_vio->nbytes = m_done;
        m_output_vio->reenable();
        return 0;
      }

      transform_to_range();
      break;
    }
  }

  return 0;
}


/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
RangeTransform::transform_to_range()
{
  IOBufferReader *reader = m_write_vio.get_reader();
  int done, toskip, tosend, avail;
  const int *end, *start;
  int prev_end = 0;
  int *done_byte;

  done = m_done;

  end = &m_ranges[m_current_range]._end;
  done_byte = &m_ranges[m_current_range]._done_byte;
  start = &m_ranges[m_current_range]._start;
  avail = reader->read_avail();

  while (true) {
    if (*done_byte < (*start - 1)) {
      toskip = *start - *done_byte - 1;

      if (toskip > avail)
        toskip = avail;

      if (toskip > 0) {
        reader->consume(toskip);
        *done_byte += toskip;
        avail = reader->read_avail();
      }
    }

    if (avail > 0) {
      tosend = *end - *done_byte;

      if (tosend > avail)
        tosend = avail;

      m_output_buf->write(reader, tosend);
      reader->consume(tosend);

      m_done += tosend;
      *done_byte += tosend;
    }

    if (*done_byte == *end)
      prev_end = *end;

    // move to next Range if done one
    // ignore bad Range: _done_byte -1, _end -1
    while (*done_byte == *end) {
      m_current_range++;

      if (m_current_range == m_num_range_fields) {
        if (m_num_range_fields > 1) {
          m_done += m_output_buf->write("\r\n", 2);
          add_boundary(true);
        }

        Debug("transform_range", "total bytes of Range response body is %d", m_done);
        m_output_vio->nbytes = m_done;
        m_output_vio->reenable();

        // if we are detaching before processing all the 
        //   input data, send VC_EVENT_EOS to let the upstream know
        //   that it can rely on us consuming any more data
        int cb_event = (m_write_vio.ntodo() > 0) ? VC_EVENT_EOS : VC_EVENT_WRITE_COMPLETE;
        m_write_vio._cont->handleEvent(cb_event, &m_write_vio);
        return;
      }

      end = &m_ranges[m_current_range]._end;
      done_byte = &m_ranges[m_current_range]._done_byte;
      start = &m_ranges[m_current_range]._start;

      // if this is a good Range
      if (*end != -1) {
        m_done += m_output_buf->write("\r\n", 2);
        add_boundary(false);
        add_sub_header(m_current_range);

        // keep this part for future support of out-of-order Range
        // if this is NOT a sequential Range compared to the previous one -
        // start of current Range is larger than the end of last Range, do
        // not need to go back to the start of the IOBuffereReader. 
        // Otherwise, reset the IOBufferReader. 
        //if ( *start > prev_end ) 
        *done_byte = prev_end;
        //else
        //  reader->reset();

        break;
      }
    }

    // When we need to read and there is nothing available 
    avail = reader->read_avail();
    if (avail == 0)
      break;
  }

  m_output_vio->reenable();
  m_write_vio._cont->handleEvent(VC_EVENT_WRITE_READY, &m_write_vio);
}


/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

/*
 * these two need be changed at the same time
 */

static char bound[] = "RANGE_SEPARATOR";
static char range_type[] = "multipart/byteranges; boundary=RANGE_SEPARATOR";
static char cont_type[] = "Content-type: ";
static char cont_range[] = "Content-range: bytes ";
static int sub_header_size = sizeof(cont_type) - 1 + 2 + sizeof(cont_range) - 1 + 4;
static int boundary_size = 2 + sizeof(bound) - 1 + 2;

void
RangeTransform::add_boundary(bool end)
{
  m_done += m_output_buf->write("--", 2);
  m_done += m_output_buf->write(bound, sizeof(bound) - 1);

  if (end)
    m_done += m_output_buf->write("--", 2);

  m_done += m_output_buf->write("\r\n", 2);
}


/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

#define RANGE_NUMBERS_LENGTH 60

void
RangeTransform::add_sub_header(int index)
{
  // this should be large enough to hold three integers!
  char numbers[RANGE_NUMBERS_LENGTH];
  int len;

  m_done += m_output_buf->write(cont_type, sizeof(cont_type) - 1);
  if (m_content_type)
    m_done += m_output_buf->write(m_content_type, m_content_type_len);
  m_done += m_output_buf->write("\r\n", 2);
  m_done += m_output_buf->write(cont_range, sizeof(cont_range) - 1);

  snprintf(numbers, sizeof(numbers), "%d-%d/%d", m_ranges[index]._start, m_ranges[index]._end, m_content_length);
  len = strlen(numbers);
  if (len < RANGE_NUMBERS_LENGTH)
    m_done += m_output_buf->write(numbers, len);
  m_done += m_output_buf->write("\r\n\r\n", 4);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

/*
 * this function changes the response header to reflect this is 
 * a Range response. 
 */

void
RangeTransform::change_response_header()
{
  MIMEField *field;
  char *reason_phrase;
  HTTPStatus status_code;

  ink_assert(m_transform_resp->field_find(MIME_FIELD_CONTENT_RANGE, MIME_LEN_CONTENT_RANGE) == NULL);

  status_code = HTTP_STATUS_PARTIAL_CONTENT;
  m_transform_resp->status_set(status_code);
  reason_phrase = (char *) (HttpMessageBody::StatusCodeName(status_code));
  m_transform_resp->reason_set(reason_phrase, strlen(reason_phrase));

  // set the right Content-Type for multiple entry Range
  if (m_num_range_fields > 1) {
    field = m_transform_resp->field_find(MIME_FIELD_CONTENT_TYPE, MIME_LEN_CONTENT_TYPE);

    if (field != NULL)
      m_transform_resp->field_delete(MIME_FIELD_CONTENT_TYPE, MIME_LEN_CONTENT_TYPE);


    field = m_transform_resp->field_create(MIME_FIELD_CONTENT_TYPE, MIME_LEN_CONTENT_TYPE);
    field->value_append(m_transform_resp->m_heap, m_transform_resp->m_mime, range_type, sizeof(range_type) - 1);

    m_transform_resp->field_attach(field);
  }

  else {
    char numbers[RANGE_NUMBERS_LENGTH];

    field = m_transform_resp->field_create(MIME_FIELD_CONTENT_RANGE, MIME_LEN_CONTENT_RANGE);
    snprintf(numbers, sizeof(numbers), "bytes %d-%d/%d", m_ranges[0]._start, m_ranges[0]._end, m_content_length);
    field->value_append(m_transform_resp->m_heap, m_transform_resp->m_mime, numbers, strlen(numbers));
    m_transform_resp->field_attach(field);
  }
}

#undef RANGE_NUMBERS_LENGTH


/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
RangeTransform::calculate_output_cl()
{
  int i;

  if (m_unsatisfiable_range)
    return;

  if (m_num_range_fields == 1)
    m_output_cl = m_ranges[0]._end - m_ranges[0]._start + 1;

  else {
    for (i = 0; i < m_num_range_fields; i++) {
      if (m_ranges[i]._start >= 0) {
        m_output_cl += boundary_size;
        m_output_cl += sub_header_size + m_content_type_len;
        m_output_cl += num_chars_for_int(m_ranges[i]._start)
          + num_chars_for_int(m_ranges[i]._end) + m_num_chars_for_cl + 2;
        m_output_cl += m_ranges[i]._end - m_ranges[i]._start + 1;
        m_output_cl += 2;
      }
    }

    m_output_cl += boundary_size + 2;
  }

  Debug("transform_range", "Pre-calculated Content-Length for Range response is %d", m_output_cl);
}

#endif //INK_NO_TRANSFORM

