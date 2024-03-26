/**
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

  Copyright 2020, Oath Inc.
*/

#include <random>
#include <chrono>

#include <swoc/TextView.h>
#include <swoc/ArenaWriter.h>

#include "txn_box/common.h"
#include "txn_box/yaml_util.h"
#include "txn_box/ts_util.h"
#include "txn_box/Extractor.h"
#include "txn_box/Config.h"
#include "txn_box/Context.h"

using swoc::TextView;
using swoc::BufferWriter;
using swoc::ArenaWriter;
using swoc::Errata;
using swoc::Rv;
namespace bwf = swoc::bwf;
using namespace swoc::literals;

/* ------------------------------------------------------------------------------------ */
/// Utility functions.
namespace
{
struct URLLocation {
  ts::URL &url;
};

struct ReqLocation {
  ts::HttpRequest &req;
};

} // namespace

namespace swoc
{
BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &, URLLocation const &url_loc)
{
  ts::URL &url{url_loc.url};
  auto host_name = url.host();
  if (!host_name.empty()) {
    auto port = url.port();
    if (port == 0 || url.is_port_canonical()) {
      w.write(host_name);
    } else {
      w.print("{}:{}", host_name, port);
    }
  }
  return w;
}

} // namespace swoc
/* ------------------------------------------------------------------------------------ */
class Ex_ua_req_method : public StringExtractor
{
public:
  static constexpr TextView NAME{"ua-req-method"};
  using Extractor::extract; // declare hidden member function

  Feature extract(Context &ctx, Spec const &spec) override;
};

Feature
Ex_ua_req_method::extract(Context &ctx, Spec const &)
{
  if (auto hdr{ctx.ua_req_hdr()}; hdr.is_valid()) {
    return FeatureView::Direct(hdr.method());
  }
  return NIL_FEATURE;
}

class Ex_proxy_req_method : public StringExtractor
{
public:
  static constexpr TextView NAME{"proxy-req-method"};
  using Extractor::extract; // declare hidden member function

  Feature extract(Context &ctx, Spec const &spec) override;
};

Feature
Ex_proxy_req_method::extract(Context &ctx, Spec const &)
{
  if (auto hdr{ctx.proxy_req_hdr()}; hdr.is_valid()) {
    return FeatureView::Direct(hdr.method());
  }
  return NIL_FEATURE;
}
/* ------------------------------------------------------------------------------------ */
/** The entire URL.
 * The C API doesn't provide direct access to a string for the URL, therefore the string
 * obtained here is transient.
 */
class Ex_ua_req_url : public Extractor
{
public:
  static constexpr TextView NAME{"ua-req-url"};
  using Extractor::extract; // declare hidden member function

  Feature extract(Context &ctx, Spec const &spec) override;
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

Feature
Ex_ua_req_url::extract(Context &ctx, const Spec &)
{
  if (auto hdr{ctx.ua_req_hdr()}; hdr.is_valid()) {
    if (auto url{hdr.url()}; url.is_valid()) {
      return ctx.render_transient([&](BufferWriter &w) { url.write_full(w); });
    }
  }
  return NIL_FEATURE;
}

BufferWriter &
Ex_ua_req_url::format(BufferWriter &w, Spec const &, Context &ctx)
{
  if (auto hdr{ctx.ua_req_hdr()}; hdr.is_valid()) {
    if (auto url{hdr.url()}; url.is_valid()) {
      url.write_full(w);
    }
  }
  return w;
}
// ----
class Ex_pre_remap_url : public Extractor
{
public:
  static constexpr TextView NAME{"pre-remap-url"};
  using Extractor::extract; // declare hidden member function
  Feature extract(Context &ctx, Spec const &spec) override;
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

Feature
Ex_pre_remap_url::extract(Context &ctx, const Spec &)
{
  if (ts::URL url{ctx._txn.pristine_url_get()}; url.is_valid()) {
    return ctx.render_transient([&](BufferWriter &w) { url.write_full(w); });
  }
  return NIL_FEATURE;
}

BufferWriter &
Ex_pre_remap_url::format(BufferWriter &w, Spec const &, Context &ctx)
{
  if (ts::URL url{ctx._txn.pristine_url_get()}; url.is_valid()) {
    url.write_full(w);
  }
  return w;
}
// ----
class Ex_remap_target_url : public Extractor
{
public:
  static constexpr TextView NAME{"remap-target-url"};
  using Extractor::extract; // declare hidden member function
  Feature extract(Context &ctx, Spec const &spec) override;
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

Feature
Ex_remap_target_url::extract(Context &ctx, const Spec &)
{
  if (ctx._remap_info) {
    if (ts::URL url{ctx._remap_info->requestBufp, ctx._remap_info->mapFromUrl}; url.is_valid()) {
      return ctx.render_transient([&](BufferWriter &w) { url.write_full(w); });
    }
  }
  return NIL_FEATURE;
}

BufferWriter &
Ex_remap_target_url::format(BufferWriter &w, Spec const &, Context &ctx)
{
  if (ctx._remap_info) {
    if (ts::URL url{ctx._remap_info->requestBufp, ctx._remap_info->mapFromUrl}; url.is_valid()) {
      url.write_full(w);
    }
  }
  return w;
}
// ----
class Ex_remap_replacement_url : public Extractor
{
public:
  static constexpr TextView NAME{"remap-replacement-url"};
  using Extractor::extract; // declare hidden member function

  Feature extract(Context &ctx, Spec const &spec) override;
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

Feature
Ex_remap_replacement_url::extract(Context &ctx, const Spec &)
{
  if (ctx._remap_info) {
    if (ts::URL url{ctx._remap_info->requestBufp, ctx._remap_info->mapToUrl}; url.is_valid()) {
      return ctx.render_transient([&](BufferWriter &w) { url.write_full(w); });
    }
  }
  return NIL_FEATURE;
}

BufferWriter &
Ex_remap_replacement_url::format(BufferWriter &w, Spec const &, Context &ctx)
{
  if (ctx._remap_info) {
    if (ts::URL url{ctx._remap_info->requestBufp, ctx._remap_info->mapToUrl}; url.is_valid()) {
      url.write_full(w);
    }
  }
  return w;
}
// ----
class Ex_proxy_req_url : public Extractor
{
public:
  static constexpr TextView NAME{"proxy-req-url"};
  using Extractor::extract; // declare hidden member function
  Feature extract(Context &ctx, Spec const &spec) override;
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

Feature
Ex_proxy_req_url::extract(Context &ctx, const Spec &)
{
  if (auto hdr{ctx.proxy_req_hdr()}; hdr.is_valid()) {
    if (auto url{hdr.url()}; url.is_valid()) {
      return ctx.render_transient([&](BufferWriter &w) { url.write_full(w); });
    }
  }
  return NIL_FEATURE;
}

BufferWriter &
Ex_proxy_req_url::format(BufferWriter &w, Spec const &, Context &ctx)
{
  if (auto hdr{ctx.proxy_req_hdr()}; hdr.is_valid()) {
    if (auto url{hdr.url()}; url.is_valid()) {
      url.write_full(w);
    }
  }
  return w;
}
/* ------------------------------------------------------------------------------------ */
class Ex_ua_req_scheme : public StringExtractor
{
public:
  static constexpr TextView NAME{"ua-req-scheme"};
  using Extractor::extract; // declare hidden member function
  Feature extract(Context &ctx, Spec const &spec) override;
};

Feature
Ex_ua_req_scheme::extract(Context &ctx, Spec const &)
{
  if (auto hdr{ctx.ua_req_hdr()}; hdr.is_valid()) {
    if (auto url{hdr.url()}; url.is_valid()) {
      return FeatureView::Direct(url.scheme());
    }
  }
  return NIL_FEATURE;
}

class Ex_pre_remap_scheme : public StringExtractor
{
public:
  static constexpr TextView NAME{"pre-remap-scheme"};
  using Extractor::extract; // declare hidden member function
  Feature extract(Context &ctx, Spec const &spec) override;
};

Feature
Ex_pre_remap_scheme::extract(Context &ctx, Spec const &)
{
  if (ts::URL url{ctx._txn.pristine_url_get()}; url.is_valid()) {
    return FeatureView::Direct(url.scheme());
  }
  return NIL_FEATURE;
}

class Ex_remap_target_scheme : public StringExtractor
{
public:
  static constexpr TextView NAME{"remap-target-scheme"};
  using Extractor::extract; // declare hidden member function
  Feature extract(Context &ctx, Spec const &spec) override;
};

Feature
Ex_remap_target_scheme::extract(Context &ctx, Spec const &)
{
  if (ctx._remap_info) {
    if (ts::URL url{ctx._remap_info->requestBufp, ctx._remap_info->mapFromUrl}; url.is_valid()) {
      return FeatureView::Direct(url.scheme());
    }
  }
  return NIL_FEATURE;
}

class Ex_remap_replacement_scheme : public StringExtractor
{
public:
  static constexpr TextView NAME{"remap-replacement-scheme"};
  using Extractor::extract; // declare hidden member function
  Feature extract(Context &ctx, Spec const &spec) override;
};

Feature
Ex_remap_replacement_scheme::extract(Context &ctx, Spec const &)
{
  if (ctx._remap_info) {
    if (ts::URL url{ctx._remap_info->requestBufp, ctx._remap_info->mapToUrl}; url.is_valid()) {
      return FeatureView::Direct(url.scheme());
    }
  }
  return NIL_FEATURE;
}

class Ex_proxy_req_scheme : public StringExtractor
{
public:
  static constexpr TextView NAME{"proxy-req-scheme"};
  using Extractor::extract; // declare hidden member function

  Feature extract(Context &ctx, Spec const &spec) override;
};

Feature
Ex_proxy_req_scheme::extract(Context &ctx, Spec const &)
{
  FeatureView zret;
  zret._direct_p = true;
  if (auto hdr{ctx.proxy_req_hdr()}; hdr.is_valid()) {
    if (ts::URL url{hdr.url()}; url.is_valid()) {
      return FeatureView::Direct(url.scheme());
    }
  }
  return NIL_FEATURE;
}
/* ------------------------------------------------------------------------------------ */
/// The network location in the URL.
class Ex_ua_req_loc : public StringExtractor
{
  using self_type  = Ex_ua_req_loc;
  using super_type = StringExtractor;

public:
  static constexpr TextView NAME{"ua-req-loc"};
  using Extractor::extract; // declare hidden member function
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

BufferWriter &
Ex_ua_req_loc::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  if (auto hdr{ctx.ua_req_hdr()}; hdr.is_valid()) {
    if (auto field{hdr.field(ts::HTTP_FIELD_HOST)}; field.is_valid()) {
      bwformat(w, spec, field.value());
    } else if (auto url{hdr.url()}; url.is_valid()) {
      bwformat(w, spec, URLLocation{url});
    }
  }
  return w;
}
// ----
class Ex_proxy_req_loc : public StringExtractor
{
  using self_type  = Ex_proxy_req_loc;
  using super_type = StringExtractor;

public:
  static constexpr TextView NAME{"proxy-req-loc"};
  using Extractor::extract; // declare hidden member function
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

BufferWriter &
Ex_proxy_req_loc::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  if (auto hdr{ctx.ua_req_hdr()}; hdr.is_valid()) {
    if (auto field{hdr.field(ts::HTTP_FIELD_HOST)}; field.is_valid()) {
      bwformat(w, spec, field.value());
    } else if (auto url{hdr.url()}; url.is_valid()) {
      bwformat(w, spec, URLLocation{url});
    }
  }
  return w;
}
/* ------------------------------------------------------------------------------------ */
/// Host name.
class Ex_ua_req_host : public Extractor
{
public:
  static constexpr TextView NAME{"ua-req-host"};
  using Extractor::extract; // declare hidden member function
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
  Feature extract(Context &ctx, Spec const &) override;
};

Feature
Ex_ua_req_host::extract(Context &ctx, Spec const &)
{
  if (auto hdr{ctx.ua_req_hdr()}; hdr.is_valid()) {
    return FeatureView::Direct(hdr.host());
  }
  return NIL_FEATURE;
}

BufferWriter &
Ex_ua_req_host::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  return bwformat(w, spec, this->extract(ctx, spec));
}
// ----
class Ex_proxy_req_host : public Extractor
{
public:
  static constexpr TextView NAME{"proxy-req-host"};
  using Extractor::extract; // declare hidden member function
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
  Feature extract(Context &ctx, Spec const &) override;
};

Feature
Ex_proxy_req_host::extract(Context &ctx, Spec const &)
{
  if (auto hdr{ctx.proxy_req_hdr()}; hdr.is_valid()) {
    return FeatureView::Direct(hdr.host());
  }
  return NIL_FEATURE;
}

BufferWriter &
Ex_proxy_req_host::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  return bwformat(w, spec, this->extract(ctx, spec));
}
// ----
class Ex_pre_remap_host : public Extractor
{
public:
  static constexpr TextView NAME{"pre-remap-host"};
  using Extractor::extract; // declare hidden member function
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
  Feature extract(Context &ctx, Spec const &spec) override;
};

Feature
Ex_pre_remap_host::extract(Context &ctx, Spec const &)
{
  if (ts::URL url{ctx._txn.pristine_url_get()}; url.is_valid()) {
    return FeatureView::Direct(url.host());
  }
  return NIL_FEATURE;
}

BufferWriter &
Ex_pre_remap_host::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  return bwformat(w, spec, this->extract(ctx, spec));
}
// ----
class Ex_remap_target_host : public Extractor
{
public:
  static constexpr TextView NAME{"remap-target-host"};
  using Extractor::extract; // declare hidden member function
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
  Feature extract(Context &ctx, Spec const &spec) override;
};

Feature
Ex_remap_target_host::extract(Context &ctx, Spec const &)
{
  if (ctx._remap_info) {
    if (ts::URL url{ctx._remap_info->requestBufp, ctx._remap_info->mapFromUrl}; url.is_valid()) {
      return FeatureView::Direct(url.host());
    }
  }
  return NIL_FEATURE;
}

BufferWriter &
Ex_remap_target_host::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  return bwformat(w, spec, this->extract(ctx, spec));
}
// ----
class Ex_remap_replacement_host : public Extractor
{
public:
  static constexpr TextView NAME{"remap-replacement-host"};
  using Extractor::extract; // declare hidden member function
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
  Feature extract(Context &ctx, Spec const &spec) override;
};

Feature
Ex_remap_replacement_host::extract(Context &ctx, Spec const &)
{
  if (ctx._remap_info) {
    if (ts::URL url{ctx._remap_info->requestBufp, ctx._remap_info->mapToUrl}; url.is_valid()) {
      return FeatureView::Direct(url.host());
    }
  }
  return NIL_FEATURE;
}

BufferWriter &
Ex_remap_replacement_host::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
/// Destination IP port.
class Ex_ua_req_port : public Extractor
{
public:
  static constexpr TextView NAME{"ua-req-port"};
  using Extractor::extract; // declare hidden member function
  Rv<ActiveType> validate(Config &cfg, Spec &spec, swoc::TextView const &arg) override;
  Feature extract(Context &ctx, Spec const &spec) override;
};

Rv<ActiveType>
Ex_ua_req_port::validate(Config &, Spec &, const swoc::TextView &)
{
  return ActiveType{NIL, INTEGER};
}

Feature
Ex_ua_req_port::extract(Context &ctx, Spec const &)
{
  Feature zret{};
  if (auto hdr{ctx.ua_req_hdr()}; hdr.is_valid()) {
    if (ts::URL url{hdr.url()}; url.is_valid()) {
      zret = static_cast<feature_type_for<INTEGER>>(url.port());
    }
  }
  return zret;
}
// ----
class Ex_proxy_req_port : public Extractor
{
public:
  static constexpr TextView NAME{"proxy-req-port"};
  using Extractor::extract; // declare hidden member function

  Rv<ActiveType> validate(Config &cfg, Spec &spec, swoc::TextView const &arg) override;
  Feature extract(Context &ctx, Spec const &spec) override;
};

Rv<ActiveType>
Ex_proxy_req_port::validate(Config &, Spec &, const swoc::TextView &)
{
  return ActiveType{NIL, INTEGER};
}

Feature
Ex_proxy_req_port::extract(Context &ctx, Spec const &)
{
  Feature zret{};
  if (auto hdr{ctx.proxy_req_hdr()}; hdr.is_valid()) {
    if (ts::URL url{hdr.url()}; url.is_valid()) {
      zret = static_cast<feature_type_for<INTEGER>>(url.port());
    }
  }
  return zret;
}
/* ------------------------------------------------------------------------------------ */
/// URL path.
class Ex_ua_req_path : public Extractor
{
public:
  static constexpr TextView NAME{"ua-req-path"};
  using Extractor::extract; // declare hidden member function
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
  Feature extract(Context &ctx, Spec const &spec) override;
};

Feature
Ex_ua_req_path::extract(Context &ctx, Spec const &)
{
  if (auto hdr{ctx.ua_req_hdr()}; hdr.is_valid()) {
    if (ts::URL url{hdr.url()}; url.is_valid()) {
      return FeatureView::Direct(url.path());
    }
  }
  return NIL_FEATURE;
}

BufferWriter &
Ex_ua_req_path::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  return bwformat(w, spec, this->extract(ctx, spec));
}

class Ex_pre_remap_path : public Extractor
{
public:
  static constexpr TextView NAME{"pre-remap-path"};
  using Extractor::extract; // declare hidden member function
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
  Feature extract(Context &ctx, Spec const &spec) override;
};

Feature
Ex_pre_remap_path::extract(Context &ctx, Spec const &)
{
  if (ts::URL url{ctx._txn.pristine_url_get()}; url.is_valid()) {
    return FeatureView::Direct(url.path());
  }
  return NIL_FEATURE;
}

BufferWriter &
Ex_pre_remap_path::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  return bwformat(w, spec, this->extract(ctx, spec));
}

class Ex_remap_target_path : public Extractor
{
public:
  static constexpr TextView NAME{"remap-target-path"};
  using Extractor::extract; // declare hidden member function
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
  Feature extract(Context &ctx, Spec const &spec) override;
};

Feature
Ex_remap_target_path::extract(Context &ctx, Spec const &)
{
  if (ctx._remap_info) {
    if (ts::URL url{ctx._remap_info->requestBufp, ctx._remap_info->mapFromUrl}; url.is_valid()) {
      return FeatureView::Direct(url.path());
    }
  }
  return NIL_FEATURE;
}

BufferWriter &
Ex_remap_target_path::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  return bwformat(w, spec, this->extract(ctx, spec));
}

class Ex_remap_replacement_path : public Extractor
{
public:
  static constexpr TextView NAME{"remap-replacement-path"};
  using Extractor::extract; // declare hidden member function
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
  Feature extract(Context &ctx, Spec const &spec) override;
};

Feature
Ex_remap_replacement_path::extract(Context &ctx, Spec const &)
{
  if (ctx._remap_info) {
    if (ts::URL url{ctx._remap_info->requestBufp, ctx._remap_info->mapToUrl}; url.is_valid()) {
      return FeatureView::Direct(url.path());
    }
  }
  return NIL_FEATURE;
}

BufferWriter &
Ex_remap_replacement_path::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  return bwformat(w, spec, this->extract(ctx, spec));
}

class Ex_proxy_req_path : public Extractor
{
public:
  static constexpr TextView NAME{"proxy-req-path"};
  using Extractor::extract; // declare hidden member function
  Feature extract(Context &ctx, Spec const &spec) override;
};

Feature
Ex_proxy_req_path::extract(Context &ctx, Spec const &)
{
  if (auto hdr{ctx.proxy_req_hdr()}; hdr.is_valid()) {
    if (ts::URL url{hdr.url()}; url.is_valid()) {
      return FeatureView::Direct(url.path());
    }
  }
  return NIL_FEATURE;
}
/* ------------------------------------------------------------------------------------ */
// Fragment.
// These have the @c extract method because it can be done as a @c Direct
// value and that's better than running through the formatting.
class Ex_ua_req_fragment : public StringExtractor
{
public:
  static constexpr TextView NAME{"ua-req-fragment"};
  using Extractor::extract; // declare hidden member function

  Feature extract(Context &ctx, Spec const &spec) override;
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

Feature
Ex_ua_req_fragment::extract(Context &ctx, Spec const &)
{
  if (auto hdr{ctx.ua_req_hdr()}; hdr.is_valid()) {
    if (ts::URL url{hdr.url()}; url.is_valid()) {
      return FeatureView::Direct(url.fragment());
    }
  }
  return NIL_FEATURE;
}

BufferWriter &
Ex_ua_req_fragment::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  return bwformat(w, spec, this->extract(ctx, spec));
}

class Ex_pre_remap_fragment : public StringExtractor
{
public:
  static constexpr TextView NAME{"pre-remap-fragment"};
  using Extractor::extract; // declare hidden member function
  Feature extract(Context &ctx, Spec const &spec) override;
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

Feature
Ex_pre_remap_fragment::extract(Context &ctx, Spec const &)
{
  if (auto url{ctx._txn.pristine_url_get()}; url.is_valid()) {
    return FeatureView::Direct(url.fragment());
  }
  return NIL_FEATURE;
}

BufferWriter &
Ex_pre_remap_fragment::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  return bwformat(w, spec, this->extract(ctx, spec));
}

class Ex_proxy_req_fragment : public StringExtractor
{
public:
  static constexpr TextView NAME{"proxy-req-fragment"};
  using Extractor::extract; // declare hidden member function

  Feature extract(Context &ctx, Spec const &spec) override;
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

Feature
Ex_proxy_req_fragment::extract(Context &ctx, Spec const &)
{
  if (auto hdr{ctx.proxy_req_hdr()}; hdr.is_valid()) {
    if (ts::URL url{hdr.url()}; url.is_valid()) {
      return FeatureView::Direct(url.fragment());
    }
  }
  return NIL_FEATURE;
}

BufferWriter &
Ex_proxy_req_fragment::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  return bwformat(w, spec, this->extract(ctx, spec));
}

/* ------------------------------------------------------------------------------------ */
/// The network location in the URL.
class Ex_ua_req_url_loc : public StringExtractor
{
public:
  static constexpr TextView NAME{"ua-req-url-loc"};
  using Extractor::extract; // declare hidden member function
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

BufferWriter &
Ex_ua_req_url_loc::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  if (auto hdr{ctx.ua_req_hdr()}; hdr.is_valid()) {
    if (auto url{hdr.url()}; url.is_valid()) {
      bwformat(w, spec, URLLocation{url});
    }
  }
  return w;
}
// ----
class Ex_proxy_req_url_loc : public StringExtractor
{
public:
  static constexpr TextView NAME{"proxy-req-url-loc"};
  using Extractor::extract; // declare hidden member function
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

BufferWriter &
Ex_proxy_req_url_loc::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  if (auto hdr{ctx.proxy_req_hdr()}; hdr.is_valid()) {
    if (auto url{hdr.url()}; url.is_valid()) {
      bwformat(w, spec, URLLocation{url});
    }
  }
  return w;
}
// ----
class Ex_pre_remap_loc : public StringExtractor
{
public:
  static constexpr TextView NAME{"pre-remap-req-loc"};
  using Extractor::extract; // declare hidden member function
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

BufferWriter &
Ex_pre_remap_loc::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  if (ts::URL url{ctx._txn.pristine_url_get()}; url.is_valid()) {
    bwformat(w, spec, URLLocation{url});
  }
  return w;
}
// ----
class Ex_remap_target_loc : public StringExtractor
{
public:
  static constexpr TextView NAME{"remap-target-loc"};
  using Extractor::extract; // declare hidden member function
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

BufferWriter &
Ex_remap_target_loc::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  if (ctx._remap_info) {
    if (ts::URL url{ctx._remap_info->requestBufp, ctx._remap_info->mapFromUrl}; url.is_valid()) {
      bwformat(w, spec, URLLocation{url});
    }
  }
  return w;
}
// ----
class Ex_remap_replacement_loc : public StringExtractor
{
public:
  static constexpr TextView NAME{"remap-replacement-loc"};
  using Extractor::extract; // declare hidden member function
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

BufferWriter &
Ex_remap_replacement_loc::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  if (ctx._remap_info) {
    if (ts::URL url{ctx._remap_info->requestBufp, ctx._remap_info->mapToUrl}; url.is_valid()) {
      bwformat(w, spec, URLLocation{url});
    }
  }
  return w;
}
/* ------------------------------------------------------------------------------------ */
class Ex_ua_req_url_host : public Extractor
{
public:
  static constexpr TextView NAME{"ua-req-url-host"};
  using Extractor::extract; // declare hidden member function

  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
  Feature extract(Context &ctx, Spec const &spec) override;
};

Feature
Ex_ua_req_url_host::extract(Context &ctx, Spec const &)
{
  if (auto hdr{ctx.ua_req_hdr()}; hdr.is_valid()) {
    if (ts::URL url{hdr.url()}; url.is_valid()) {
      return FeatureView::Direct(url.host());
    }
  }
  return NIL_FEATURE;
}

BufferWriter &
Ex_ua_req_url_host::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  return bwformat(w, spec, this->extract(ctx, spec));
}
// ----
class Ex_proxy_req_url_host : public Extractor
{
public:
  static constexpr TextView NAME{"proxy-req-url-host"};
  using Extractor::extract; // declare hidden member function
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
  Feature extract(Context &ctx, Spec const &spec) override;
};

Feature
Ex_proxy_req_url_host::extract(Context &ctx, Spec const &)
{
  if (auto hdr{ctx.proxy_req_hdr()}; hdr.is_valid()) {
    if (ts::URL url{hdr.url()}; url.is_valid()) {
      return FeatureView::Direct(url.host());
    }
  }
  return NIL_FEATURE;
}

BufferWriter &
Ex_proxy_req_url_host::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
class Ex_ua_req_url_port : public Extractor
{
public:
  static constexpr TextView NAME{"ua-req-url-port"};
  using Extractor::extract; // declare hidden member function

  Rv<ActiveType> validate(Config &cfg, Spec &spec, swoc::TextView const &arg) override;
  Feature extract(Context &ctx, Spec const &spec) override;
};

Rv<ActiveType>
Ex_ua_req_url_port::validate(Config &, Spec &, const swoc::TextView &)
{
  return ActiveType{NIL, INTEGER};
}

Feature
Ex_ua_req_url_port::extract(Context &ctx, Spec const &)
{
  FeatureView zret;
  zret._direct_p = true;
  if (auto hdr{ctx.ua_req_hdr()}; hdr.is_valid()) {
    if (ts::URL url{hdr.url()}; url.is_valid()) {
      zret = url.host();
    }
  }
  return zret;
}

// ----
class Ex_proxy_req_url_port : public Extractor
{
public:
  static constexpr TextView NAME{"proxy-req-url-port"};
  using Extractor::extract; // declare hidden member function
  Rv<ActiveType> validate(Config &cfg, Spec &spec, swoc::TextView const &arg) override;
  Feature extract(Context &ctx, Spec const &spec) override;
};

Rv<ActiveType>
Ex_proxy_req_url_port::validate(Config &, Spec &, const swoc::TextView &)
{
  return ActiveType{NIL, INTEGER};
}

Feature
Ex_proxy_req_url_port::extract(Context &ctx, Spec const &)
{
  FeatureView zret;
  zret._direct_p = true;
  if (auto hdr{ctx.proxy_req_hdr()}; hdr.is_valid()) {
    if (ts::URL url{hdr.url()}; url.is_valid()) {
      zret = url.host();
    }
  }
  return zret;
}

// ----
class Ex_pre_remap_port : public Extractor
{
public:
  static constexpr TextView NAME{"pre-remap-port"};
  using Extractor::extract; // declare hidden member function
  Rv<ActiveType> validate(Config &cfg, Spec &spec, swoc::TextView const &arg) override;
  Feature extract(Context &ctx, Spec const &spec) override;
};

Rv<ActiveType>
Ex_pre_remap_port::validate(Config &, Spec &, const swoc::TextView &)
{
  return ActiveType{NIL, INTEGER};
}

Feature
Ex_pre_remap_port::extract(Context &ctx, Spec const &)
{
  Feature zret{};
  if (auto url{ctx._txn.pristine_url_get()}; url.is_valid()) {
    zret = static_cast<feature_type_for<INTEGER>>(url.port());
  }
  return zret;
}

// ----
class Ex_remap_target_port : public Extractor
{
public:
  static constexpr TextView NAME{"remap-target-port"};
  using Extractor::extract; // declare hidden member function
  Rv<ActiveType> validate(Config &cfg, Spec &spec, swoc::TextView const &arg) override;
  Feature extract(Context &ctx, Spec const &spec) override;
};

Rv<ActiveType>
Ex_remap_target_port::validate(Config &, Spec &, const swoc::TextView &)
{
  return ActiveType{NIL, INTEGER};
}

Feature
Ex_remap_target_port::extract(Context &ctx, Spec const &)
{
  Feature zret{};
  if (ctx._remap_info) {
    if (ts::URL url{ctx._remap_info->requestBufp, ctx._remap_info->mapFromUrl}; url.is_valid()) {
      zret = static_cast<feature_type_for<INTEGER>>(url.port());
    }
  }
  return zret;
}

// ----
class Ex_remap_replacement_port : public Extractor
{
public:
  static constexpr TextView NAME{"remap-replacement-port"};
  using Extractor::extract; // declare hidden member function

  Rv<ActiveType> validate(Config &cfg, Spec &spec, swoc::TextView const &arg) override;
  Feature extract(Context &ctx, Spec const &spec) override;
};

Rv<ActiveType>
Ex_remap_replacement_port::validate(Config &, Spec &, const swoc::TextView &)
{
  return ActiveType{NIL, INTEGER};
}

Feature
Ex_remap_replacement_port::extract(Context &ctx, Spec const &)
{
  Feature zret{};
  if (ctx._remap_info) {
    if (ts::URL url{ctx._remap_info->requestBufp, ctx._remap_info->mapToUrl}; url.is_valid()) {
      zret = static_cast<feature_type_for<INTEGER>>(url.port());
    }
  }
  return zret;
}

/* ------------------------------------------------------------------------------------ */
class ExHttpField : public Extractor
{
public:
  Rv<ActiveType> validate(Config &cfg, Spec &spec, TextView const &arg) override;
  using Extractor::extract; // declare hidden member function
  Feature extract(Context &ctx, Spec const &spec) override;

protected:
  struct Data {
    TextView _arg;
    union {
      uint32_t all = 0;
      struct {
        unsigned by_value : 1;
        unsigned by_field : 1;
      } f;
    } opt;
  };

  /// @return The key (name) for the extractor.
  virtual TextView const &key() const = 0;

  /** Get the appropriate @c HttpHeader.
   *
   * @param ctx Runtime context.
   * @return The HTTP header to manipulate.
   *
   * This is used by subclasses to specify which HTTP header in the transaction is in play.
   */
  virtual ts::HttpHeader hdr(Context &ctx) const = 0;
};

Rv<ActiveType>
ExHttpField::validate(Config &cfg, Extractor::Spec &spec, TextView const &arg)
{
  auto span       = cfg.alloc_span<Data>(1);
  spec._data.span = span;
  auto &data      = span[0];
  data.opt.all    = 0;
  data._arg       = cfg.localize(arg);
  if (0 == strcasecmp(spec._ext, "by-field"_tv)) {
    data.opt.f.by_field = true;
  } else if (0 == strcasecmp(spec._ext, "by-value"_tv)) {
    data.opt.f.by_value = true;
  }
  return ActiveType{NIL, STRING, ActiveType::TupleOf(STRING)};
}

Feature
ExHttpField::extract(Context &ctx, const Spec &spec)
{
  Data &data = spec._data.span.rebind<Data>()[0];
  if (data.opt.f.by_field) {
    return NIL_FEATURE;
  } else if (data.opt.f.by_value) {
    return NIL_FEATURE;
  }

  if (ts::HttpHeader hdr{this->hdr(ctx)}; hdr.is_valid()) {
    if (auto field{hdr.field(data._arg)}; field.is_valid()) {
      if (field.next_dup().is_valid()) {
        auto n    = field.dup_count();
        auto span = ctx.alloc_span<Feature>(n);
        for (auto &item : span) {
          item  = field.value();
          field = field.next_dup();
        }
        return span;
      } else {
        return field.value();
      }
    }
  }
  return NIL_FEATURE;
};

// -----
class Ex_ua_req_field : public ExHttpField
{
public:
  static constexpr TextView NAME{"ua-req-field"};

protected:
  TextView const &key() const override;
  ts::HttpHeader hdr(Context &ctx) const override;
};

TextView const &
Ex_ua_req_field::key() const
{
  return NAME;
}
ts::HttpHeader
Ex_ua_req_field::hdr(Context &ctx) const
{
  return ctx.ua_req_hdr();
}
// -----
class Ex_proxy_req_field : public ExHttpField
{
public:
  static constexpr TextView NAME{"proxy-req-field"};

protected:
  TextView const &key() const override;
  ts::HttpHeader hdr(Context &ctx) const override;
};

TextView const &
Ex_proxy_req_field::key() const
{
  return NAME;
}
ts::HttpHeader
Ex_proxy_req_field::hdr(Context &ctx) const
{
  return ctx.proxy_req_hdr();
}
// -----
class Ex_proxy_rsp_field : public ExHttpField
{
public:
  static constexpr TextView NAME{"proxy-rsp-field"};

protected:
  TextView const &key() const override;
  ts::HttpHeader hdr(Context &ctx) const override;
};

TextView const &
Ex_proxy_rsp_field::key() const
{
  return NAME;
}
ts::HttpHeader
Ex_proxy_rsp_field::hdr(Context &ctx) const
{
  return ctx.proxy_rsp_hdr();
}
// -----
class Ex_upstream_rsp_field : public ExHttpField
{
public:
  static constexpr TextView NAME{"upstream-rsp-field"};

protected:
  TextView const &key() const override;
  ts::HttpHeader hdr(Context &ctx) const override;
};

TextView const &
Ex_upstream_rsp_field::key() const
{
  return NAME;
}
ts::HttpHeader
Ex_upstream_rsp_field::hdr(Context &ctx) const
{
  return ctx.upstream_rsp_hdr();
}
/* ------------------------------------------------------------------------------------ */
class Ex_upstream_rsp_status : public Extractor
{
public:
  static constexpr TextView NAME{"upstream-rsp-status"};
  using Extractor::extract; // declare hidden member function
  Rv<ActiveType> validate(Config &cfg, Spec &spec, TextView const &arg) override;

  /// Extract the feature from the @a ctx.
  Feature extract(Context &ctx, Extractor::Spec const &) override;

  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

Rv<ActiveType>
Ex_upstream_rsp_status::validate(Config &, Spec &, TextView const &)
{
  return {INTEGER};
}

Feature
Ex_upstream_rsp_status::extract(Context &ctx, Extractor::Spec const &)
{
  return static_cast<feature_type_for<INTEGER>>(ctx._txn.ursp_hdr().status());
}

BufferWriter &
Ex_upstream_rsp_status::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  return bwformat(w, spec, ctx._txn.ursp_hdr().status());
}
// ----
class Ex_upstream_rsp_status_reason : public StringExtractor
{
public:
  static constexpr TextView NAME{"upstream-rsp-status-reason"};
  using Extractor::extract; // declare hidden member function
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

BufferWriter &
Ex_upstream_rsp_status_reason::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  return bwformat(w, spec, ctx._txn.ursp_hdr().reason());
}
/* ------------------------------------------------------------------------------------ */
class Ex_proxy_rsp_status : public Extractor
{
public:
  static constexpr TextView NAME{"proxy-rsp-status"};
  using Extractor::extract; // declare hidden member function

  Rv<ActiveType> validate(Config &cfg, Spec &spec, TextView const &arg) override;

  /// Extract the feature from the @a ctx.
  Feature extract(Context &ctx, Extractor::Spec const &) override;

  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

Rv<ActiveType>
Ex_proxy_rsp_status::validate(Config &, Spec &, TextView const &)
{
  return {INTEGER};
}

Feature
Ex_proxy_rsp_status::extract(Context &ctx, Extractor::Spec const &)
{
  return static_cast<feature_type_for<INTEGER>>(ctx._txn.prsp_hdr().status());
}

BufferWriter &
Ex_proxy_rsp_status::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  return bwformat(w, spec, ctx._txn.prsp_hdr().status());
}
// ----
class Ex_proxy_rsp_status_reason : public StringExtractor
{
public:
  static constexpr TextView NAME{"proxy-rsp-status-reason"};
  using Extractor::extract; // declare hidden member function
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

BufferWriter &
Ex_proxy_rsp_status_reason::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  return bwformat(w, spec, ctx._txn.prsp_hdr().reason());
}
/* ------------------------------------------------------------------------------------ */
class Ex_outbound_txn_count : public Extractor
{
public:
  static constexpr TextView NAME{"outbound-txn-count"};
  using Extractor::extract; // declare hidden member function
  Rv<ActiveType> validate(Config &cfg, Spec &spec, TextView const &arg) override;

  /// Extract the feature from the @a ctx.
  Feature extract(Context &ctx, Extractor::Spec const &) override;
};

Rv<ActiveType>
Ex_outbound_txn_count::validate(Config &, Spec &, TextView const &)
{
  return {INTEGER};
}

Feature
Ex_outbound_txn_count::extract(Context &ctx, Extractor::Spec const &)
{
  return static_cast<feature_type_for<INTEGER>>(ctx._txn.outbound_txn_count());
}
/* ------------------------------------------------------------------------------------ */
/// Extract the transaction remote address.
class Ex_outbound_addr_remote : public Extractor
{
public:
  static constexpr TextView NAME{"outbound-addr-remote"};
  using Extractor::extract; // declare hidden member function
  Rv<ActiveType> validate(Config &cfg, Spec &spec, TextView const &arg) override;
  Feature extract(Context &ctx, Spec const &spec) override;
};

Rv<ActiveType>
Ex_outbound_addr_remote::validate(Config &, Extractor::Spec &, TextView const &)
{
  return ActiveType{NIL, IP_ADDR};
}

Feature
Ex_outbound_addr_remote::extract(Context &ctx, Spec const &)
{
  if (auto addr = ctx._txn.outbound_remote_addr(); addr) {
    return swoc::IPAddr{addr};
  }

  return NIL_FEATURE;
}
/* ------------------------------------------------------------------------------------ */
/// Extract the transaction local address.
class Ex_outbound_addr_local : public Extractor
{
public:
  static constexpr TextView NAME{"outbound-addr-local"};
  using Extractor::extract; // declare hidden member function
  Rv<ActiveType> validate(Config &cfg, Spec &spec, TextView const &arg) override;
  Feature extract(Context &ctx, Spec const &spec) override;
};

Rv<ActiveType>
Ex_outbound_addr_local::validate(Config &, Extractor::Spec &, TextView const &)
{
  return ActiveType{NIL, IP_ADDR};
}

Feature
Ex_outbound_addr_local::extract(Context &ctx, Spec const &)
{
  if (auto addr = ctx._txn.outbound_local_addr(); addr) {
    return swoc::IPAddr{addr};
  }

  return NIL_FEATURE;
}
/* ------------------------------------------------------------------------------------ */
namespace
{
// Extractors aren't constructed, they are always named references to singletons.
// These are the singletons.

Ex_ua_req_method ua_req_method;
Ex_proxy_req_method proxy_req_method;

Ex_ua_req_scheme ua_req_scheme;
Ex_pre_remap_scheme pre_remap_scheme;
Ex_remap_target_scheme remap_target_scheme;
Ex_remap_replacement_scheme remap_replacement_scheme;
Ex_proxy_req_scheme proxy_req_scheme;

Ex_ua_req_url ua_req_url;
Ex_pre_remap_url pre_remap_url;
Ex_proxy_req_url proxy_req_url;
Ex_remap_replacement_url remap_replacement_url;
Ex_remap_target_url remap_target_url;

Ex_ua_req_loc ua_req_loc;
Ex_proxy_req_loc proxy_req_loc;

Ex_ua_req_host ua_req_host;
Ex_pre_remap_host pre_remap_host;
Ex_remap_target_host remap_target_host;
Ex_remap_replacement_host remap_replacement_host;
Ex_proxy_req_host proxy_req_host;

Ex_ua_req_port ua_req_port;
Ex_proxy_req_port proxy_req_port;

Ex_ua_req_path ua_req_path;
Ex_pre_remap_path pre_remap_path;
Ex_remap_replacement_path remap_replacement_path;
Ex_remap_target_path remap_target_path;
Ex_proxy_req_path proxy_req_path;

Ex_ua_req_fragment ua_req_fragment;
Ex_pre_remap_fragment pre_remap_fragment;
Ex_proxy_req_fragment proxy_req_fragment;

Ex_ua_req_url_host ua_req_url_host;
Ex_proxy_req_url_host proxy_req_url_host;

Ex_ua_req_url_port ua_req_url_port;
Ex_proxy_req_url_port proxy_req_url_port;
Ex_pre_remap_port pre_remap_port;
Ex_remap_replacement_port remap_replacement_port;
Ex_remap_target_port remap_target_port;

Ex_ua_req_url_loc ua_req_url_loc;
Ex_proxy_req_url_loc proxy_req_url_loc;
Ex_pre_remap_loc pre_remap_loc;
Ex_remap_replacement_loc remap_replacement_loc;
Ex_remap_target_loc remap_target_loc;

Ex_ua_req_field ua_req_field;
Ex_proxy_req_field proxy_req_field;
Ex_proxy_rsp_field proxy_rsp_field;
Ex_upstream_rsp_field upstream_rsp_field;

Ex_proxy_rsp_status proxy_rsp_status;
Ex_upstream_rsp_status upstream_rsp_status;
Ex_proxy_rsp_status_reason proxy_rsp_status_reason;
Ex_upstream_rsp_status_reason upstream_rsp_status_reason;
Ex_outbound_txn_count outbound_txn_count;
Ex_outbound_addr_remote outbound_addr_remote;
Ex_outbound_addr_local outbound_addr_local;

[[maybe_unused]] bool INITIALIZED = []() -> bool {
  Extractor::define(Ex_ua_req_method::NAME, &ua_req_method);
  Extractor::define(Ex_proxy_req_method::NAME, &proxy_req_method);

  Extractor::define(Ex_ua_req_scheme::NAME, &ua_req_scheme);
  Extractor::define(Ex_pre_remap_scheme::NAME, &pre_remap_scheme);
  Extractor::define(Ex_remap_replacement_scheme::NAME, &remap_replacement_scheme);
  Extractor::define(Ex_remap_target_scheme::NAME, &remap_target_scheme);
  Extractor::define(Ex_proxy_req_scheme::NAME, &proxy_req_scheme);

  Extractor::define(Ex_ua_req_url::NAME, &ua_req_url);
  Extractor::define(Ex_pre_remap_url::NAME, &pre_remap_url);
  Extractor::define(Ex_remap_target_url::NAME, &remap_target_url);
  Extractor::define(Ex_remap_replacement_url::NAME, &remap_replacement_url);
  Extractor::define(Ex_proxy_req_url::NAME, &proxy_req_url);

  Extractor::define(Ex_ua_req_loc::NAME, &ua_req_loc);
  Extractor::define(Ex_proxy_req_loc::NAME, &proxy_req_loc);

  Extractor::define(Ex_ua_req_host::NAME, &ua_req_host);
  Extractor::define(Ex_proxy_req_host::NAME, &proxy_req_host);

  Extractor::define(Ex_ua_req_port::NAME, &ua_req_port);
  Extractor::define(Ex_proxy_req_port::NAME, &proxy_req_port);

  Extractor::define(Ex_ua_req_path::NAME, &ua_req_path);
  Extractor::define(Ex_pre_remap_path::NAME, &pre_remap_path);
  Extractor::define(Ex_remap_target_path::NAME, &remap_target_path);
  Extractor::define(Ex_remap_replacement_path::NAME, &remap_replacement_path);
  Extractor::define(Ex_proxy_req_path::NAME, &proxy_req_path);

  Extractor::define(Ex_ua_req_fragment::NAME, &ua_req_fragment);
  Extractor::define(Ex_pre_remap_fragment::NAME, &pre_remap_fragment);
  Extractor::define(Ex_proxy_req_fragment::NAME, &proxy_req_fragment);

  Extractor::define(Ex_ua_req_url_loc::NAME, &ua_req_url_loc);
  Extractor::define(Ex_proxy_req_url_loc::NAME, &proxy_req_url_loc);
  Extractor::define(Ex_pre_remap_loc::NAME, &pre_remap_loc);
  Extractor::define(Ex_remap_target_loc::NAME, &remap_target_loc);
  Extractor::define(Ex_remap_replacement_loc::NAME, &remap_replacement_loc);

  Extractor::define(Ex_ua_req_url_host::NAME, &ua_req_url_host);
  Extractor::define(Ex_proxy_req_url_host::NAME, &proxy_req_url_host);
  Extractor::define(Ex_pre_remap_host::NAME, &pre_remap_host);
  Extractor::define(Ex_remap_target_host::NAME, &remap_target_host);
  Extractor::define(Ex_remap_replacement_host::NAME, &remap_replacement_host);

  Extractor::define(Ex_ua_req_url_port::NAME, &ua_req_url_port);
  Extractor::define(Ex_proxy_req_url_port::NAME, &proxy_req_url_port);
  Extractor::define(Ex_pre_remap_port::NAME, &pre_remap_port);
  Extractor::define(Ex_remap_target_port::NAME, &remap_target_port);
  Extractor::define(Ex_remap_replacement_port::NAME, &remap_replacement_port);

  Extractor::define(Ex_proxy_rsp_status::NAME, &proxy_rsp_status);
  Extractor::define(Ex_upstream_rsp_status::NAME, &upstream_rsp_status);
  Extractor::define(Ex_proxy_rsp_status_reason::NAME, &proxy_rsp_status_reason);
  Extractor::define(Ex_upstream_rsp_status_reason::NAME, &upstream_rsp_status_reason);
  Extractor::define(Ex_outbound_txn_count::NAME, &outbound_txn_count);
  Extractor::define(Ex_outbound_addr_remote::NAME, &outbound_addr_remote);
  Extractor::define(Ex_outbound_addr_local::NAME, &outbound_addr_local);

  Extractor::define(Ex_ua_req_field::NAME, &ua_req_field);
  Extractor::define(Ex_proxy_req_field::NAME, &proxy_req_field);
  Extractor::define(Ex_proxy_rsp_field::NAME, &proxy_rsp_field);
  Extractor::define(Ex_upstream_rsp_field::NAME, &upstream_rsp_field);

  return true;
}();
} // namespace
