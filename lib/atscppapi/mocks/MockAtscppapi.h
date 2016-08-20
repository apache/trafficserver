/*
 * MockAtscppapi.h
 *
 *  Created on: Mar 15, 2015
 *      Author: sdavu
 */

#ifndef MOCKATSCPPAPI_H_
#define MOCKATSCPPAPI_H_


#include <atscppapi/Transaction.h>
#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/TransactionPlugin.h>
#include <atscppapi/Async.h>
#include <atscppapi/AsyncHttpFetch.h>
#include <atscppapi/Headers.h>
#include <atscppapi/HttpMethod.h>
#include <atscppapi/HttpStatus.h>
#include <atscppapi/HttpVersion.h>
#include <atscppapi/InterceptPlugin.h>
#include <atscppapi/TransformationPlugin.h>
#include <atscppapi/Request.h>
#include <atscppapi/Response.h>
#include <atscppapi/shared_ptr.h>
#include <atscppapi/Url.h>
#include <stdarg.h>

class MockAtscppapi {
public:
	MockAtscppapi();
	~MockAtscppapi();
};

#endif /* MOCKATSCPPAPI_H_ */
