#######################
#
#  Licensed to the Apache Software Foundation (ASF) under one or more contributor license
#  agreements.  See the NOTICE file distributed with this work for additional information regarding
#  copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
#  (the "License"); you may not use this file except in compliance with the License.  You may obtain
#  a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software distributed under the License
#  is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
#  or implied. See the License for the specific language governing permissions and limitations under
#  the License.
#
#######################

add_atsplugin(otel_tracer otel_tracer.cc)
target_link_libraries(
  otel_tracer
  PRIVATE
    "$<LINK_GROUP:RESCAN,opentelemetry::opentelemetry_common,opentelemetry::opentelemetry_exporter_in_memory,opentelemetry::opentelemetry_exporter_ostream_logs,opentelemetry::opentelemetry_exporter_ostream_metrics,opentelemetry::opentelemetry_exporter_ostream_span,opentelemetry::opentelemetry_exporter_otlp_http,opentelemetry::opentelemetry_exporter_otlp_http_client,opentelemetry::opentelemetry_exporter_otlp_http_log,opentelemetry::opentelemetry_exporter_otlp_http_metric,opentelemetry::opentelemetry_http_client_curl,opentelemetry::opentelemetry_logs,opentelemetry::opentelemetry_metrics,opentelemetry::opentelemetry_otlp_recordable,opentelemetry::opentelemetry_proto,opentelemetry::opentelemetry_resources,opentelemetry::opentelemetry_trace,opentelemetry::opentelemetry_version,protobuf::libprotobuf,CURL::libcurl>"
)
verify_global_plugin(otel_tracer)
