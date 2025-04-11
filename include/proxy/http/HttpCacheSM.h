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

   HttpCacheSM.h

   Description:


 ****************************************************************************/

#pragma once

#include "iocore/cache/Cache.h"
#include "iocore/eventsystem/ConfigProcessor.h"
#include "proxy/hdrs/URL.h"
#include "proxy/hdrs/HTTP.h"
#include "proxy/http/HttpConfig.h"

class HttpSM;
class HttpCacheSM;

struct HttpCacheAction : public Action {
  HttpCacheAction();
  void cancel(Continuation *c = nullptr) override;
  void
  init(HttpCacheSM *sm_arg)
  {
    sm = sm_arg;
  };
  void
  reset()
  {
    cancelled = false;
  }

  HttpCacheSM *sm = nullptr;
};

class HttpCacheSM : public Continuation
{
public:
  HttpCacheSM();

  void
  init(HttpSM *sm_arg, Ptr<ProxyMutex> &amutex)
  {
    master_sm = sm_arg;
    mutex     = amutex;
    captive_action.init(this);
  }
  void reset();

  Action *open_read(const HttpCacheKey *key, URL *url, HTTPHdr *hdr, const OverridableHttpConfigParams *params,
                    time_t pin_in_cache);

  Action *open_write(const HttpCacheKey *key, URL *url, HTTPHdr *request, CacheHTTPInfo *old_info, time_t pin_in_cache, bool retry,
                     bool allow_multiple);

  CacheVConnection *cache_read_vc  = nullptr;
  CacheVConnection *cache_write_vc = nullptr;

  // Flag to check whether read-while-write is in progress or not
  bool readwhilewrite_inprogress = false;

  HttpSM *master_sm      = nullptr;
  Action *pending_action = nullptr;

  // Function to set readwhilewrite_inprogress flag
  inline void
  set_readwhilewrite_inprogress(bool value)
  {
    readwhilewrite_inprogress = value;
  }

  // Function to get the readwhilewrite_inprogress flag
  inline bool
  is_readwhilewrite_inprogress()
  {
    return readwhilewrite_inprogress;
  }

  bool
  is_ram_cache_hit()
  {
    return cache_read_vc ? (cache_read_vc->is_ram_cache_hit()) : false;
  }

  bool
  is_compressed_in_ram()
  {
    return cache_read_vc ? (cache_read_vc->is_compressed_in_ram()) : false;
  }

  inline void
  set_open_read_tries(int value)
  {
    open_read_tries = value;
  }

  int
  get_open_read_tries()
  {
    return open_read_tries;
  }

  inline void
  set_open_write_tries(int value)
  {
    open_write_tries = value;
  }

  int
  get_open_write_tries()
  {
    return open_write_tries;
  }

  int
  get_volume_number()
  {
    if (cache_read_vc) {
      return cache_read_vc->get_volume_number();
    } else if (cache_write_vc) {
      return cache_write_vc->get_volume_number();
    }

    return -1;
  }

  const char *
  get_disk_path()
  {
    if (cache_read_vc) {
      return cache_read_vc->get_disk_path();
    } else if (cache_write_vc) {
      return cache_write_vc->get_disk_path();
    }

    return nullptr;
  }

  inline void
  abort_read()
  {
    if (cache_read_vc) {
      Metrics::Gauge::decrement(http_rsb.current_cache_connections);
      cache_read_vc->do_io_close(0); // passing zero as aborting read is not an error
      cache_read_vc = nullptr;
    }
  }
  inline void
  abort_write()
  {
    if (cache_write_vc) {
      Metrics::Gauge::decrement(http_rsb.current_cache_connections);
      cache_write_vc->do_io_close(0); // passing zero as aborting write is not an error
      cache_write_vc = nullptr;
    }
  }
  inline void
  close_write()
  {
    if (cache_write_vc) {
      Metrics::Gauge::decrement(http_rsb.current_cache_connections);
      cache_write_vc->do_io_close();
      cache_write_vc = nullptr;
    }
  }
  inline void
  close_read()
  {
    if (cache_read_vc) {
      Metrics::Gauge::decrement(http_rsb.current_cache_connections);
      cache_read_vc->do_io_close();
      cache_read_vc = nullptr;
    }
  }
  inline void
  end_both()
  {
    // We close the read so that cache
    //   records its stats
    close_read();
    abort_write();
  }

  inline int
  get_last_error() const
  {
    return err_code;
  }

private:
  class HttpConfigAccessorImpl : public HttpConfigAccessor
  {
  public:
    HttpConfigAccessorImpl(const OverridableHttpConfigParams *params) : HttpConfigAccessor(), _params(params) {}

    int8_t
    get_ignore_accept_mismatch() const override
    {
      return this->_params->ignore_accept_mismatch;
    }

    int8_t
    get_ignore_accept_charset_mismatch() const override
    {
      return this->_params->ignore_accept_charset_mismatch;
    }

    int8_t
    get_ignore_accept_encoding_mismatch() const override
    {
      return this->_params->ignore_accept_encoding_mismatch;
    }

    int8_t
    get_ignore_accept_language_mismatch() const override
    {
      return this->_params->ignore_accept_language_mismatch;
    }

    const char *
    get_global_user_agent_header() const override
    {
      return this->_params->global_user_agent_header;
    }

  private:
    const OverridableHttpConfigParams *_params = nullptr;
  };

  void    do_schedule_in();
  Action *do_cache_open_read(const HttpCacheKey &);

  bool write_retry_done() const;

  int state_cache_open_read(int event, void *data);
  int state_cache_open_write(int event, void *data);

  HttpCacheAction captive_action;
  bool            open_read_cb  = false;
  bool            open_write_cb = false;

  // Open read parameters
  int                    open_read_tries  = 0;
  HTTPHdr               *read_request_hdr = nullptr;
  HttpConfigAccessorImpl http_params{nullptr};
  time_t                 read_pin_in_cache = 0;

  // Open write parameters
  bool       retry_write      = true;
  int        open_write_tries = 0;
  ink_hrtime open_write_start = 0; // overrides open_write_tries

  // Common parameters
  URL         *lookup_url = nullptr;
  HttpCacheKey cache_key;

  // to keep track of multiple cache lookups
  int lookup_max_recursive = 0;
  int current_lookup_level = 0;

  // last error from the cache subsystem
  int err_code = 0;
};
