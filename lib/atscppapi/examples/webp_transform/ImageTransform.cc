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

#include <sstream>
#include <iostream>
#include <atscppapi/PluginInit.h>
#include <atscppapi/Logger.h>

#include "compress.h"
#include "Common.h"
#include "ImageTransform.h"

using namespace atscppapi;
using std::string;


std::string ImageTransform::FIELD_USER_AGENT("User-Agent");
std::string ImageTransform::FIELD_CONTENT_TYPE("Content-Type");
std::string ImageTransform::FIELD_TRANSFORM_IMAGE("@X-Transform-Image");
std::string ImageTransform::CONTEXT_IMG_TRANSFORM("Transform-Image");
std::string ImageTransform::USER_AGENT_CROME("Chrome");


ImageTransform::ImageTransform(Transaction &transaction, TransformationPlugin::Type xformType)
: TransformationPlugin(transaction, xformType),
  webp_transform_(){
	TransactionPlugin::registerHook(HOOK_SEND_REQUEST_HEADERS);
	TransformationPlugin::registerHook((xformType == TransformationPlugin::REQUEST_TRANSFORMATION) ?
			HOOK_SEND_REQUEST_HEADERS : HOOK_READ_RESPONSE_HEADERS);
}

ImageTransform::~ImageTransform() {
	webp_transform_.Finalize();
}

void
ImageTransform::handleReadResponseHeaders(Transaction &transaction)
{
	Headers& headers = transaction.getServerResponse().getHeaders();
	headers.set("Vary", ImageTransform::FIELD_TRANSFORM_IMAGE);
	headers.set(ImageTransform::FIELD_CONTENT_TYPE, "image/webp");

	TS_DEBUG(TAG, "Image Transformation Plugin for url %s", transaction.getServerRequest().getUrl().getUrlString().c_str() );
	transaction.resume();
}

void
ImageTransform::consume(const string &data) {
	img_.write(data.data(), data.size());
}

void
ImageTransform::handleInputComplete() {
	webp_transform_.Init();
	webp_transform_.Transform(img_);
	produce(webp_transform_.getTransformedImage().str());

	setOutputComplete();
}


GlobalHookPlugin::GlobalHookPlugin() {
	registerHook(HOOK_READ_REQUEST_HEADERS);
	registerHook(HOOK_READ_RESPONSE_HEADERS);
}

void
GlobalHookPlugin::handleReadRequestHeaders(Transaction &transaction) {
	//add transformation only for jpeg files
	Headers& hdrs = transaction.getClientRequest().getHeaders();
	string uagent = hdrs.values(ImageTransform::FIELD_USER_AGENT);
	if(uagent.find(ImageTransform::USER_AGENT_CROME) != string::npos) {
		TS_DEBUG(TAG, "Setting Context for useragent chrome.");
		transaction.setContextValue(ImageTransform::CONTEXT_IMG_TRANSFORM, shared_ptr<Transaction::ContextValue>(new ImageValue(true)));
		transaction.getClientRequest().getHeaders().set(ImageTransform::FIELD_TRANSFORM_IMAGE, "1");
	}
	transaction.resume();
}

void
GlobalHookPlugin::handleReadResponseHeaders(Transaction &transaction) {
	//add transformation only for jpeg files
	Headers& hdrs = transaction.getServerResponse().getHeaders();
	string ctype = hdrs.values(ImageTransform::FIELD_CONTENT_TYPE);
	ImageValue* img_context = dynamic_cast<ImageValue *>(transaction.getContextValue(ImageTransform::CONTEXT_IMG_TRANSFORM).get());
	if(img_context && (ctype.find("jpeg") != string::npos || ctype.find("png") != string::npos)) {
		//modify clientrequest at this point. Lets see if this will effect the caching.
		transaction.addPlugin(new ImageTransform(transaction, TransformationPlugin::RESPONSE_TRANSFORMATION));
	}
	transaction.resume();
}

void TSPluginInit(int argc ATSCPPAPI_UNUSED, const char *argv[] ATSCPPAPI_UNUSED) {
	TS_DEBUG(TAG, "TSPluginInit");
	new GlobalHookPlugin();
}
