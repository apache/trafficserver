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

/***************************************************************************
 LogConfigCollation.cc

 This is a *huge* hack, in an attempt to isolate some of the collation
 related code away from liblogging.a. Please fix this!

***************************************************************************/
#include "libts.h"
#include "ink_platform.h"

#include "LogFormatType.h"
#include "LogField.h"
#include "LogFilter.h"
#include "LogFormat.h"
#include "LogFile.h"
#include "LogBuffer.h"
#include "LogHost.h"
#include "LogObject.h"
#include "LogConfig.h"
#include "LogUtils.h"
#include "Log.h"
#include "SimpleTokenizer.h"

#if defined(IOCORE_LOG_COLLATION)
#include "LogCollationAccept.h"
#endif


void
LogConfig::create_pre_defined_objects_with_filter(const PreDefinedFormatInfoList & pre_def_info_list, size_t num_filters,
                                                  LogFilter ** filter, const char *filt_name, bool force_extension)
{
  PreDefinedFormatInfo *pdi;
  for (pdi = pre_def_info_list.head; pdi != NULL; pdi = (pdi->link).next) {

    char *obj_fname;
    char obj_filt_fname[PATH_MAX];
    if (filt_name) {
      ink_string_concatenate_strings_n(obj_filt_fname, PATH_MAX, pdi->filename, "-", filt_name, NULL);
      obj_fname = obj_filt_fname;
    } else {
      obj_fname = pdi->filename;
    }

    if (force_extension) {
      ink_string_append(obj_filt_fname,
                        (char *) (pdi->is_ascii ?
                                  ASCII_LOG_OBJECT_FILENAME_EXTENSION :
                                  BINARY_LOG_OBJECT_FILENAME_EXTENSION), PATH_MAX);
    }
    // create object with filters
    //
    LogObject *obj;
    obj = NEW(new LogObject(pdi->format, logfile_dir, obj_fname,
                            pdi->is_ascii ? ASCII_LOG : BINARY_LOG,
                            pdi->header, rolling_enabled, rolling_interval_sec, rolling_offset_hr, rolling_size_mb));

    if (collation_mode == SEND_STD_FMTS || collation_mode == SEND_STD_AND_NON_XML_CUSTOM_FMTS) {

      LogHost *loghost = NEW(new LogHost(obj->get_full_filename(),
                                         obj->get_signature()));
      ink_assert(loghost != NULL);

      loghost->set_name_port(collation_host, collation_port);
      obj->add_loghost(loghost, false);
    }

    for (size_t i = 0; i < num_filters; ++i) {
      obj->add_filter(filter[i]);
    }

    // give object to object manager
    //
    log_object_manager.manage_object(obj);
  }
}

/*-------------------------------------------------------------------------
  LogConfig::setup_collation
  -------------------------------------------------------------------------*/

void
LogConfig::setup_collation(LogConfig * prev_config)
{
  // Set-up the collation status, but only if collation is enabled and
  // there are valid entries for the collation host and port.
  //
  if (collation_mode<NO_COLLATION || collation_mode>= N_COLLATION_MODES) {
    Note("Invalid value %d for proxy.local.log.collation_mode"
         " configuration variable (valid range is from %d to %d)\n"
         "Log collation disabled", collation_mode, NO_COLLATION, N_COLLATION_MODES - 1);
  } else if (collation_mode == NO_COLLATION) {
    // if the previous configuration had a collation accept, delete it
    //
    if (prev_config && prev_config->m_log_collation_accept) {
      delete prev_config->m_log_collation_accept;
      prev_config->m_log_collation_accept = NULL;
    }
  } else {
    if (!collation_port) {
      Note("Cannot activate log collation, %d is and invalid " "collation port", collation_port);
    } else if (collation_mode > COLLATION_HOST && strcmp(collation_host, "none") == 0) {
      Note("Cannot activate log collation, \"%s\" is and invalid " "collation host", collation_host);
    } else {
      if (collation_mode == COLLATION_HOST) {
#if defined(IOCORE_LOG_COLLATION)

        ink_debug_assert(m_log_collation_accept == 0);

        if (prev_config && prev_config->m_log_collation_accept) {
          if (prev_config->collation_port == collation_port) {
            m_log_collation_accept = prev_config->m_log_collation_accept;
          } else {
            delete prev_config->m_log_collation_accept;
          }
        }

        if (!m_log_collation_accept) {
          Log::collation_port = collation_port;
          m_log_collation_accept = NEW(new LogCollationAccept(collation_port));
        }
#else
        // since we are the collation host, we need to signal the
        // collate_cond variable so that our collation thread wakes up.
        //
        ink_cond_signal(&Log::collate_cond);
#endif
        Debug("log", "I am a collation host listening on port %d.", collation_port);
      } else {
        Debug("log", "I am a collation client (%d)."
              " My collation host is %s:%d", collation_mode, collation_host, collation_port);
      }

#ifdef IOCORE_LOG_COLLATION
      Debug("log", "using iocore log collation");
#else
      Debug("log", "using socket log collation");
#endif
      if (collation_host_tagged) {
        LogFormat::turn_tagging_on();
      } else {
        LogFormat::turn_tagging_off();
      }
    }
  }
}



#if defined(IOCORE_LOG_COLLATION)
#include "LogCollationClientSM.h"
#endif

#define PING 	true
#define NOPING 	false

int
LogHost::write(LogBuffer * lb, size_t * to_disk, size_t * to_net, size_t * to_pipe)
{
  NOWARN_UNUSED(to_pipe);
  if (lb == NULL) {
    Note("Cannot write LogBuffer to LogHost %s; LogBuffer is NULL", name());
    return -1;
  }
  LogBufferHeader *buffer_header = lb->header();
  if (buffer_header == NULL) {
    Note("Cannot write LogBuffer to LogHost %s; LogBufferHeader is NULL", name());
    return -1;
  }
  if (buffer_header->entry_count == 0) {
    // no bytes to write
    return 0;
  }
#if !defined(IOCORE_LOG_COLLATION)

  // make sure we're connected & authenticated

  if (!connected(NOPING)) {
    if (!connect()) {
      Note("Cannot write LogBuffer to LogHost %s; not connected", name());
      return orphan_write(lb);
    }
  }
  // try sending the logbuffer

  int bytes_to_send, bytes_sent;
  bytes_to_send = buffer_header->byte_count;
  lb->convert_to_network_order();
  bytes_sent = m_sock->write(m_sock_fd, buffer_header, bytes_to_send);
  if (bytes_to_send != bytes_sent) {
    Note("Bad write to LogHost %s; bad send count %d/%d", name(), bytes_sent, bytes_to_send);
    disconnect();
    lb->convert_to_host_order();
    return orphan_write(lb);
  }

  Debug("log-host", "%d bytes sent to LogHost %s:%u", bytes_sent, name(), port());
  // BUGBUG:: fix this, Log Collation should work on NT as well
  SUM_DYN_STAT(log_stat_bytes_sent_to_network_stat, bytes_sent);
  return bytes_sent;

#else // !defined(IOCORE_LOG_COLLATION)

  // make a copy of our log_buffer
  int buffer_header_size = buffer_header->byte_count;
  LogBufferHeader *buffer_header_copy = (LogBufferHeader *) NEW(new char[buffer_header_size]);
  ink_assert(buffer_header_copy != NULL);

  memcpy(buffer_header_copy, buffer_header, buffer_header_size);
  LogBuffer *lb_copy = NEW(new LogBuffer(lb->get_owner(),
                                         buffer_header_copy));
  ink_assert(lb_copy != NULL);

  // create a new collation client if necessary
  if (m_log_collation_client_sm == NULL) {
    m_log_collation_client_sm = NEW(new LogCollationClientSM(this));
    ink_assert(m_log_collation_client_sm != NULL);
  }
  // send log_buffer; orphan if necessary
  int bytes_sent = m_log_collation_client_sm->send(lb_copy);
  if (bytes_sent <= 0) {
#ifndef TS_MICRO
    bytes_sent = orphan_write_and_delete(lb_copy, to_disk);
#if defined(LOG_BUFFER_TRACKING)
    Debug("log-buftrak", "[%d]LogHost::write - orphan write complete", lb_copy->header()->id);
#endif // defined(LOG_BUFFER_TRACKING)
#else
    Note("Starting dropping log buffer due to overloading");
    delete lb_copy;
    lb_copy = 0;
#endif // TS_MICRO
  } else {
    if (to_net) {
      *to_net += bytes_sent;
    }
  }

  return bytes_sent;

#endif // !defined(IOCORE_LOG_COLLATION)
}
