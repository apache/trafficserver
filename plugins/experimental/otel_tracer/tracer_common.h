/*
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

#pragma once
#include "opentelemetry/exporters/otlp/otlp_http_exporter.h"
#include "opentelemetry/sdk/trace/batch_span_processor.h"
#include "opentelemetry/sdk/trace/span_data.h"
#include "opentelemetry/trace/propagation/jaeger.h"
#include "opentelemetry/trace/propagation/b3_propagator.h"

#include "opentelemetry/exporters/ostream/span_exporter.h"
#include "opentelemetry/sdk/trace/simple_processor.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "opentelemetry/trace/provider.h"

#include "opentelemetry/context/propagation/global_propagator.h"
#include "opentelemetry/context/propagation/text_map_propagator.h"
#include "opentelemetry/trace/propagation/http_trace_context.h"

#include "opentelemetry/sdk/trace/samplers/always_off.h"
#include "opentelemetry/sdk/trace/samplers/always_on.h"
#include "opentelemetry/sdk/trace/samplers/parent.h"
#include "opentelemetry/sdk/trace/samplers/trace_id_ratio.h"

#include <cstring>
#include <iostream>
#include <vector>
#include <string>
#include "opentelemetry/ext/http/client/http_client.h"
#include "opentelemetry/nostd/shared_ptr.h"

namespace trace    = opentelemetry::trace;
namespace nostd    = opentelemetry::nostd;
namespace sdktrace = opentelemetry::sdk::trace;
namespace context  = opentelemetry::context;
namespace otlp     = opentelemetry::exporter::otlp;
namespace
{
constexpr const char *attrHttpStatusCode = {"http.status_code"};
constexpr const char *attrHttpMethod     = {"http.method"};
constexpr const char *attrHttpUrl        = {"http.url"};
constexpr const char *attrHttpRoute      = {"http.route"};
constexpr const char *attrHttpHost       = {"http.host"};
constexpr const char *attrHttpUserAgent  = {"http.user_agent"};
constexpr const char *attrNetHostPort    = {"net.host.port"};
constexpr const char *attrHttpScheme     = {"http.scheme"};

template <typename T> class HttpTextMapCarrier : public context::propagation::TextMapCarrier
{
public:
  HttpTextMapCarrier<T>(T &headers) : headers_(headers) {}
  HttpTextMapCarrier() = default;
  virtual nostd::string_view
  Get(nostd::string_view key) const noexcept override
  {
    std::string key_to_compare = key.data();
    auto it                    = headers_.find(key_to_compare);
    if (it != headers_.end()) {
      return it->second;
    }
    return "";
  }

  virtual void
  Set(nostd::string_view key, nostd::string_view value) noexcept override
  {
    headers_.insert(std::pair<std::string, std::string>(std::string(key), std::string(value)));
  }

  T headers_;
};

// this object is created using placement new therefore all destructors needs
// to be called explictly inside Destruct method
struct ExtraRequestData {
  nostd::shared_ptr<trace::Span> span;

  static void
  SetSpanStatus(ExtraRequestData *This, int status)
  {
    This->span->SetAttribute(attrHttpStatusCode, status);
  }

  static void
  SetSpanError(ExtraRequestData *This)
  {
    This->span->SetStatus(trace::StatusCode::kError);
  }

  static int
  Destruct(ExtraRequestData *This)
  {
    if (This->span) {
      This->span->End();
      This->span = nullptr;
    }
    return 0;
  }
};

void
InitTracer(std::string url, std::string service_name, double rate)
{
  otlp::OtlpHttpExporterOptions opts;

  if (url != "") {
    opts.url = url;
  }

  auto exporter  = std::unique_ptr<sdktrace::SpanExporter>(new otlp::OtlpHttpExporter(opts));
  auto processor = std::unique_ptr<sdktrace::SpanProcessor>(new sdktrace::SimpleSpanProcessor(std::move(exporter)));

  std::vector<std::unique_ptr<sdktrace::SpanProcessor>> processors;
  processors.push_back(std::move(processor));

  // Set service name
  opentelemetry::sdk::resource::ResourceAttributes attributes = {{"service.name", service_name}, {"version", (uint32_t)1}};
  auto resource                                               = opentelemetry::sdk::resource::Resource::Create(attributes);

  auto context  = std::make_shared<sdktrace::TracerContext>(std::move(processors), resource,
                                                           std::unique_ptr<sdktrace::Sampler>(new sdktrace::ParentBasedSampler(
                                                             std::make_shared<sdktrace::TraceIdRatioBasedSampler>(rate))));
  auto provider = nostd::shared_ptr<trace::TracerProvider>(new sdktrace::TracerProvider(context));

  // Set the global trace provider
  trace::Provider::SetTracerProvider(provider);

  // format: b3
  context::propagation::GlobalTextMapPropagator::SetGlobalPropagator(
    nostd::shared_ptr<context::propagation::TextMapPropagator>(new trace::propagation::B3PropagatorMultiHeader()));
}

nostd::shared_ptr<trace::Tracer>
get_tracer(std::string tracer_name)
{
  auto provider = trace::Provider::GetTracerProvider();
  return provider->GetTracer(tracer_name, OPENTELEMETRY_SDK_VERSION);
}

nostd::string_view
get_span_name(std::string_view path_str)
{
  nostd::string_view span_name(path_str.data(), path_str.length());
  return span_name;
}

std::map<nostd::string_view, opentelemetry::common::AttributeValue>
get_span_attributes(std::string_view method_str, std::string_view target_str, std::string_view path_str, std::string_view host_str,
                    std::string_view ua_str, int port, std::string_view scheme_str)
{
  std::map<nostd::string_view, opentelemetry::common::AttributeValue> attributes;
  attributes[attrHttpMethod]    = nostd::string_view(method_str.data(), method_str.length());
  attributes[attrHttpUrl]       = nostd::string_view(target_str.data(), target_str.length());
  attributes[attrHttpRoute]     = nostd::string_view(path_str.data(), path_str.length());
  attributes[attrHttpHost]      = nostd::string_view(host_str.data(), host_str.length());
  attributes[attrHttpUserAgent] = nostd::string_view(ua_str.data(), ua_str.length());
  attributes[attrNetHostPort]   = port;
  attributes[attrHttpScheme]    = nostd::string_view(scheme_str.data(), scheme_str.length());

  return attributes;
}

trace::StartSpanOptions
get_span_options(std::map<std::string, std::string> parent_headers)
{
  trace::StartSpanOptions options;

  options.kind = trace::SpanKind::kServer; // server

  const HttpTextMapCarrier<std::map<std::string, std::string>> parent_carrier(parent_headers);
  auto parent_prop        = context::propagation::GlobalTextMapPropagator::GetGlobalPropagator();
  auto parent_current_ctx = context::RuntimeContext::GetCurrent();
  auto parent_new_context = parent_prop->Extract(parent_carrier, parent_current_ctx);
  options.parent          = trace::GetSpan(parent_new_context)->GetContext();

  return options;
}

std::map<std::string, std::string>
get_trace_headers()
{
  auto current_ctx = opentelemetry::context::RuntimeContext::GetCurrent();
  HttpTextMapCarrier<std::map<std::string, std::string>> carrier;
  auto prop = opentelemetry::context::propagation::GlobalTextMapPropagator::GetGlobalPropagator();
  prop->Inject(carrier, current_ctx);

  return carrier.headers_;
}

} // namespace
