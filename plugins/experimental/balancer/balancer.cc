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

#include "balancer.h"
#include "roundrobin.h"
#include <ts/remap.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <stdlib.h>
#include <iterator>

// Using ink_inet API is cheating, but I was too lazy to write new IPv6 address parsing routines ;)

static int arg_index = 0;


// The policy type is the first comma-separated token.
static RoundRobinBalancer *
MakeBalancerInstance(const char *opt) {
	const char *end = strchr(opt, ',');
	size_t len = end ? std::distance(opt, end) : strlen(opt);

	if (len == lengthof("roundrobin") && strncmp(opt, "roundrobin", len) == 0) {
		RoundRobinBalancer *roundrobin = new RoundRobinBalancer();
		roundrobin->hold();
		const char *options = end ? end + 1 : NULL;
		if (options) {
			if (strchr(options, ',')) {
				TSError("[%s] Ignoring invalid round robin field '%s'", PLUGIN_NAME, options);
			}
			roundrobin->set_path(strdup(options));
		}
		return roundrobin;
	} else {
		TSError("[%s] Invalid balancing policy '%.*s'", PLUGIN_NAME,(int) len, opt);
		return NULL;
	}
}

TSReturnCode TSRemapInit(TSRemapInterface * /* api */, char * /* errbuf */, int /* bufsz */) {
	return TS_SUCCESS;
}

static TSReturnCode send_response_handle(TSHttpTxn txnp, BalancerTargetStatus *targetstatus) {
	TSHttpStatus status;

	TSMBuffer bufp;
	TSMLoc hdr_loc;
	RoundRobinBalancer *balancer = (RoundRobinBalancer *)TSHttpTxnArgGet((TSHttpTxn)txnp, arg_index);
	if ( NULL == targetstatus || balancer == NULL) {
		return TS_SUCCESS;
	}

	if(targetstatus && targetstatus->object_status < TS_CACHE_LOOKUP_MISS) {
		return TS_SUCCESS;
	}

	//回源check 包括down check
	if ( targetstatus->target_id >= 0  && (!targetstatus->target_down or (targetstatus->target_down && targetstatus->is_down_check) )) {
		//当源站没有正常返回的情况下，都会返回ts_error
		status = TS_HTTP_STATUS_NONE;
		//TODO 如果是回源304 check 的情况该如何处理？
		//当前的ats ，当文件过期，正好源站不通的时候，返回旧文件，当源站有任务返回的时候，ats 将会返回该内容
		//TSHttpTxnServerRespNoStoreSet(txn, 1);
		if(TSHttpTxnClientRespGet(txnp, &bufp, &hdr_loc) == TS_SUCCESS) { //ats内部处理，比如purge
			status = TSHttpHdrStatusGet(bufp,hdr_loc);
			TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
		}

		if(status > TS_HTTP_STATUS_NONE && targetstatus && balancer) {
			TSDebug(PLUGIN_NAME, "handle_response (): Get status %d, do something.",status);
			balancer->os_response_back_status(targetstatus->target_id, status);
		}

	} else {
		TSDebug(PLUGIN_NAME, " target.id == -1 or target_down  == 1!");
		TSHttpTxnSetHttpRetStatus(txnp, TS_HTTP_STATUS_SERVICE_UNAVAILABLE);

		TSHttpTxnErrorBodySet(txnp, TSstrdup("503 Source Service Unavailable!"), sizeof("503 Source Service Unavailable!") - 1, NULL);
		return TS_ERROR;
	}

	return TS_SUCCESS;
}

//如果命中
static TSReturnCode look_up_handle (TSCont contp, TSHttpTxn txnp, BalancerTargetStatus *targetstatus) {

	int obj_status;
	RoundRobinBalancer *balancer = (RoundRobinBalancer *)TSHttpTxnArgGet((TSHttpTxn)txnp, arg_index);
	if ( NULL == targetstatus || balancer == NULL) {
		return TS_ERROR;
	}

	 if (TSHttpTxnCacheLookupStatusGet(txnp, &obj_status) == TS_ERROR) {
	   TSError("[%s]  [%s] Couldn't get cache status of object",PLUGIN_NAME, __FUNCTION__);
	    return TS_ERROR;
	 }
	 TSDebug(PLUGIN_NAME, "look_up_handle  obj_status = %d\n",obj_status);
	 targetstatus->object_status = obj_status;
	 //排除 hit_fresh 和 hit_stale的情况，不需要回源
	 if (obj_status == TS_CACHE_LOOKUP_HIT_FRESH) {
		 return TS_ERROR;
	 }

	 //修改成https请求
	 if(balancer && balancer->get_https_backend_tag()) {
		TSMBuffer req_bufp;
		TSMLoc req_loc;
		TSMLoc url_loc;
		if (TSHttpTxnClientReqGet(txnp, &req_bufp, &req_loc) == TS_ERROR) {
			   TSDebug(PLUGIN_NAME, "Error while retrieving client request header\n");
			   return TS_ERROR;
		}

		if (TSHttpHdrUrlGet(req_bufp, req_loc, &url_loc) == TS_ERROR) {
		  TSDebug(PLUGIN_NAME, "Couldn't get the url\n");
		  TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, req_loc);
		  return TS_ERROR;
		}
		TSUrlSchemeSet(req_bufp, url_loc,TS_URL_SCHEME_HTTPS,TS_URL_LEN_HTTPS);
		TSHandleMLocRelease(req_bufp, req_loc, url_loc);
		TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, req_loc);
	 }

	  if(balancer && balancer->get_path() != NULL) {
		  TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_REQUEST_HDR_HOOK, contp);
	  }

	 //排除 hit_fresh 和 hit_stale的情况，不需要添加TS_HTTP_SEND_RESPONSE_HDR_HOOK钩子
	 if (obj_status == TS_CACHE_LOOKUP_HIT_STALE) {
		 return TS_ERROR;
	 }

	 TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
	 TSDebug(PLUGIN_NAME, "add TS_HTTP_SEND_RESPONSE_HDR_HOOK");

	if (targetstatus && targetstatus->target_down && !targetstatus->is_down_check)
		return TS_SUCCESS;

	 return TS_ERROR;
}

/**
 * add by daemon.xie
 * reason:  we need modify origin request URL's path, if we need.
 **/
static TSReturnCode
rewrite_send_request_path(TSHttpTxn txnp, BalancerTargetStatus *targetstatus)
{
	RoundRobinBalancer *balancer = (RoundRobinBalancer *)TSHttpTxnArgGet((TSHttpTxn)txnp, arg_index);
	if ( NULL == targetstatus || balancer == NULL) {
		return TS_ERROR;
	}

	TSMBuffer bufp;
	TSMLoc hdr_loc,url_loc;
	int len;
	const char *old_path;
	const char *add_path = balancer->get_path();

//	TSDebug("balancer", "do TS_HTTP_POST_REMAP_HOOK event '%s' ",add_path);
	if (add_path == NULL) {
		return TS_SUCCESS;
	}
	if(TSHttpTxnServerReqGet(txnp,&bufp,&hdr_loc)  != TS_SUCCESS ) {
		TSError("[%s] couldn't retrieve request header",PLUGIN_NAME);
		TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
		return TS_SUCCESS;
	}

	if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) != TS_SUCCESS) {
		TSError("[%s] couldn't retrieve request url", PLUGIN_NAME);
        TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
        TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
        return TS_SUCCESS;
     }

     old_path = TSUrlPathGet(bufp, url_loc, &len);
     if (!old_path) {
    	 	 TSError("[%s] couldn't retrieve request path",PLUGIN_NAME);
         TSHandleMLocRelease(bufp, hdr_loc, url_loc);
         TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
         TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
         return TS_SUCCESS;
     }
//     TSDebug("balancer", "get old path %s", old_path);
     int add_len = strlen(add_path);
     int new_len = len + add_len;
     char new_path[new_len];
     memcpy(new_path, add_path, add_len);
     memcpy(&new_path[add_len], old_path , len);
     if (TSUrlPathSet(bufp, url_loc, new_path, new_len) != TS_SUCCESS) {
             TSError("[%s]: Set new Path field '%.*s'", PLUGIN_NAME,new_len, new_path);
     }
//     TSDebug("balancer", "new path '%.*s'", new_len, new_path);
     TSHandleMLocRelease(bufp, hdr_loc, url_loc);
     TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);

     return TS_SUCCESS;
}


/**
 * Transaction event handler.
 */
static void balancer_handler(TSCont contp, TSEvent event, void *edata) {
	TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);
	BalancerTargetStatus *targetstatus;
	targetstatus = (struct BalancerTargetStatus *) TSContDataGet(contp);
	RoundRobinBalancer *balancer = (RoundRobinBalancer *)TSHttpTxnArgGet((TSHttpTxn)txnp, arg_index);
	TSEvent reenable = TS_EVENT_HTTP_CONTINUE;

	switch (event) {
	case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
		if (look_up_handle(contp, txnp, targetstatus) == TS_SUCCESS) {
			reenable = TS_EVENT_HTTP_ERROR;
		}
		break;
	case TS_EVENT_HTTP_SEND_REQUEST_HDR:
		rewrite_send_request_path(txnp, targetstatus);
		break;
	case TS_EVENT_HTTP_SEND_RESPONSE_HDR://放在lookup 里添加
		if (send_response_handle(txnp, targetstatus) == TS_ERROR) {
			reenable = TS_EVENT_HTTP_ERROR;
		}
		break;
	case TS_EVENT_HTTP_TXN_CLOSE:
		if (balancer)
			balancer->release();
		if (targetstatus)
			TSfree(targetstatus);
		TSContDestroy(contp);
		break;
	default:
		break;
	}
	TSHttpTxnReenable(txnp, reenable);
}

///////////////////////////////////////////////////////////////////////////////
// One instance per remap.config invocation.
//
TSReturnCode TSRemapNewInstance(int argc, char *argv[], void **instance,
		char *errbuf, int errbuf_size) {
	static const struct option longopt[] = { { const_cast<char *>("policy"),
			required_argument, 0, 'p' }, { const_cast<char *>("https"),no_argument, 0, 's' }, { 0, 0, 0, 0 } };

	RoundRobinBalancer *balancer = NULL;
	bool need_https_backend = false;

	// The first two arguments are the "from" and "to" URL string. We need to
	// skip them, but we also require that there be an option to masquerade as
	// argv[0], so we increment the argument indexes by 1 rather than by 2.
	argc--;
	argv++;

	optind = 0;
	for (;;) {
		int opt;

		opt = getopt_long(argc, (char * const *) argv, "", longopt, NULL);
		switch (opt) {
		case 'p':
			balancer = MakeBalancerInstance(optarg);
			break;
		case 's':
			need_https_backend = true;
			break;
		case -1:
			break;
		default:
			snprintf(errbuf, errbuf_size, "invalid balancer option '%d'", opt);
			delete balancer;
			return TS_ERROR;
		}

		if (opt == -1) {
			break;
		}
	}

	if (!balancer) {
		strncpy(errbuf, "missing balancer policy", errbuf_size);
		return TS_ERROR;
	}

	balancer->set_https_backend_tag(need_https_backend);
	// Pick up the remaining options as balance targets.
	uint s_count = 0;
	int i;
	for (i = optind; i < argc; ++i) {
		BalancerTarget *target = balancer->MakeBalancerTarget(argv[i]);
		target->id = s_count;
		s_count ++;
		balancer->push_target(target);
		if (target->port) {
			TSDebug(PLUGIN_NAME, "added target -> %s:%u", target->name.c_str(), target->port);
		} else {
			TSDebug(PLUGIN_NAME, "added target -> %s", target->name.c_str());
		}
	}

	if(s_count == 0) {
		TSDebug(PLUGIN_NAME, "no target have create!");
		return TS_ERROR;
	}
	*instance = balancer;
	return TS_SUCCESS;
}

void TSRemapDeleteInstance(void *instance) {
	TSDebug(PLUGIN_NAME, "Delete Instance BalancerInstance!");
	static_cast<RoundRobinBalancer *>(instance)->release();
}

TSRemapStatus TSRemapDoRemap(void *instance, TSHttpTxn txn,TSRemapRequestInfo *rri) {
	TSCont txn_contp;
	int method_len;
	const char *method;

	method = TSHttpHdrMethodGet(rri->requestBufp, rri->requestHdrp, &method_len);
	if (method == TS_HTTP_METHOD_PURGE) {
		return TSREMAP_NO_REMAP;
	}
	RoundRobinBalancer *balancer = (RoundRobinBalancer *) instance;
	if (balancer == NULL) {
		return TSREMAP_NO_REMAP;
	}
	balancer->hold();
	const BalancerTarget *target = balancer->balance(txn, rri);

	TSUrlHostSet(rri->requestBufp, rri->requestUrl, target->name.data(),target->name.size());
	TSDebug(PLUGIN_NAME,"balancer target.name -> %s target.port -> %d ", target->name.c_str(), target->port);
	if (target->port) {
		TSUrlPortSet(rri->requestBufp, rri->requestUrl, target->port);
	}

	BalancerTargetStatus *targetstatus;
	targetstatus = (BalancerTargetStatus *) TSmalloc(sizeof(BalancerTargetStatus));
	targetstatus->target_id = target->id;
	targetstatus->target_down = target->down;
	targetstatus->is_down_check = false;//是否需要down check
	targetstatus->object_status = -1;// < TS_CACHE_LOOKUP_MISS

	if (target->down ) {
		time_t now = TShrtime() / TS_HRTIME_SECOND;
		if ((now - target->accessed) > (target->timeout_fails * target->fail_timeout)) {
			targetstatus->is_down_check = true;
		}
	}

	if (NULL == (txn_contp = TSContCreate((TSEventFunc) balancer_handler, NULL))) {
		TSError("[%s] TSContCreate(): failed to create the transaction handler continuation.", PLUGIN_NAME);
		balancer->release();
		TSfree(targetstatus);
	} else {
		TSContDataSet(txn_contp, targetstatus);
		TSHttpTxnArgSet((TSHttpTxn)txn, arg_index, (void *) balancer);
		TSHttpTxnHookAdd(txn, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, txn_contp);
		TSHttpTxnHookAdd(txn, TS_HTTP_TXN_CLOSE_HOOK, txn_contp);
	}

	return TSREMAP_DID_REMAP;
}
