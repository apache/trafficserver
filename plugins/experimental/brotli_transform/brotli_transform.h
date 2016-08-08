/**
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
#ifndef _BROTLI_TRANSFORM_PLUGIN_
#define _BROTLI_TRANSFORM_PLUGIN_
#include <iostream>
#include <sstream>
#include <vector>
#include <zlib.h>
#include <getopt.h>
#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/TransformationPlugin.h>
#include <atscppapi/GzipInflateTransformation.h>
#include <atscppapi/PluginInit.h>
#include <atscppapi/Logger.h>
#include <brotli/enc/encode.h>

using namespace atscppapi;
using namespace atscppapi::transformations;
using namespace std;

class BrotliTransformationPlugin : public TransformationPlugin
{
public:
  BrotliTransformationPlugin(Transaction &);
  void handleReadResponseHeaders(Transaction &);
  void consume(const string &);
  void handleInputComplete();
  virtual ~BrotliTransformationPlugin();

private:
  brotli::BrotliParams params;
  brotli::BrotliCompressor *compressor;
};

class GlobalHookPlugin : public GlobalPlugin
{
public:
  GlobalHookPlugin() { registerHook(HOOK_READ_RESPONSE_HEADERS); }
  void handleReadResponseHeaders(Transaction &);

private:
  bool isBrotliSupported(Transaction &);
  bool inCompressBlacklist(Transaction &);
  void checkContentEncoding(Transaction &);
  enum ContentEncoding { GZIP, NONENCODE, OTHERENCODE };
  ContentEncoding osContentEncoding_;
};
#endif
