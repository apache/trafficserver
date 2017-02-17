/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "roundrobin.h"

RoundRobinBalancer::RoundRobinBalancer() :
		targets_s(), targets_b(), _ref_count(0) {
	this->next = 0;
	this->peersS_number = 0;
	this->peersB_number = 0;
	this->path = NULL;
	this->need_https_backend = false;
}

RoundRobinBalancer::~RoundRobinBalancer() {
	if (this->path != NULL) {
		free((char *) this->path);
		this->path = NULL;
	}
	uint i;
	size_t t_len;

	t_len = targets_s.size();
	for (i = 0; i < t_len; i++) {
		if (targets_s[i]) {
			delete (targets_s[i]);
		}
	}

	t_len = targets_b.size();
	for (i = 0; i < t_len; i++) {
		if (targets_b[i]) {
			delete (targets_b[i]);
		}
	}
	TSDebug(PLUGIN_NAME, "----------~RoundRobinBalancer---------------");
}

void RoundRobinBalancer::push_target(BalancerTarget *target) {
	if (target->backup) {
		this->targets_b.push_back(target);
		this->peersB_number++;
	} else {
		this->targets_s.push_back(target);
		this->peersS_number++;
	}
}

//获取一个后端
BalancerTarget * RoundRobinBalancer::balance(TSHttpTxn, TSRemapRequestInfo *) {
	BalancerTarget *peer;
	time_t now;
	now = TShrtime() / TS_HRTIME_SECOND;

	peer = get_down_timeout_peer(now);

	if (peer != NULL) {
//			TSDebug(PLUGIN_NAME,"down timeout target is not NULL !  target id-> %d now-> %ld checked-> %ld down-> %d ",
//					peer->id, now, peer->checked, peer->down);
		return peer;
	}

	if (this->peersS_number == OS_SINGLE) {
		if (this->targets_s[0]->down) {
			goto failed;
		}
		return this->targets_s[0];
	} else {
//			TSDebug(PLUGIN_NAME, "go get_healthy_peer main targets !");
		peer = get_healthy_peer(targets_s, now);
		if (peer == NULL) {
			goto failed;
		}
		return peer;
	}

	failed: if (!targets_b.empty()) {
//			TSDebug(PLUGIN_NAME, "backup targets is not NULL !");
		if (peersB_number == OS_SINGLE) {
			if (targets_b[0]->down) {
				goto clear_fails;
			}
			return targets_b[0];
		} else {
//				TSDebug(PLUGIN_NAME, "go get_healthy_peer backup targets !");
			peer = get_healthy_peer(targets_b, now);
			if (peer == NULL) {
				goto clear_fails;
			}
			return peer;
		}
	}

	clear_fails: clean_peer_status();
	//当所有服务都down的时候，进入轮询模式,(主备都需要轮询,尽快找出健康的os)
	//该状态下的target 都不会回源（除了hit_stale）
	++next;
	next = (next == UINT64_MAX ? 0 : next);
	if (peersB_number && (next % 2)) {    //主备选择
		return this->targets_b[next % this->targets_b.size()];
	}

	//防止主不存在
	if (this->peersS_number)
		return this->targets_s[next % this->targets_s.size()];
	else
		return this->targets_b[next % this->targets_b.size()];
}

//清除peer 的fails 和 timeout_fails状态
void RoundRobinBalancer::clean_peer_status() {
	uint i;
	size_t t_len;

	t_len = targets_s.size();
	for (i = 0; i < t_len; i++) {
		targets_s[i]->fails = 0;
		targets_s[i]->timeout_fails = 1;
	}

	t_len = targets_b.size();
	for (i = 0; i < t_len; i++) {
		targets_b[i]->fails = 0;
		targets_b[i]->timeout_fails = 1;
	}
}

//首先给down状态下的服务器一次机会 now - check >= fail_timeout * timeout_fails
//如果主还有存活的，就不用考虑down状态下冷却超时的备用，只有当主都不存活，才考虑
BalancerTarget * RoundRobinBalancer::get_down_timeout_peer(time_t now) {
	uint i;
	size_t t_len;
	BalancerTarget *check_peer;
	check_peer = NULL;

	t_len = targets_s.size();
	for (i = 0; i < t_len; i++) {
		if (targets_s[i]->down
				&& (now - targets_s[i]->checked) > (targets_s[i]->timeout_fails* targets_s[i]->fail_timeout)) {
			targets_s[i]->checked = now;
			return targets_s[i];
		}
	}

	t_len = targets_b.size();
	for (i = 0; i < t_len; i++) {
		if (targets_b[i]->down
				&& (now - targets_b[i]->checked) > (targets_b[i]->timeout_fails * targets_b[i]->fail_timeout)) {
			targets_b[i]->checked = now;
			return targets_b[i];
		}
	}

	return check_peer;
}

//获取最优的target 此处参考nginx rr 算法
BalancerTarget * RoundRobinBalancer::get_healthy_peer(
		std::vector<BalancerTarget *> &targets, time_t now) {
	BalancerTarget *best;
	int total;
	uint i;

	best = NULL;
	total = 0;

	size_t t_len = targets.size();

	for (i = 0; i < t_len; i++) {

		if (targets[i]->down) {
			continue;
		}
		//如果在fail_timeout内 失败次数fails >= max_fails 不可取
		if (targets[i]->max_fails && targets[i]->fails >= targets[i]->max_fails
				&& now - targets[i]->checked <= targets[i]->fail_timeout) {
			continue;
		}

		targets[i]->current_weight += targets[i]->effective_weight;
		total += targets[i]->effective_weight;

		if (targets[i]->effective_weight < int(targets[i]->weight)) {
			targets[i]->effective_weight++;
		}

		if (best == NULL
				|| targets[i]->current_weight > best->current_weight) {
			best = targets[i];
		}
	}

	if (best == NULL) {
		return NULL;
	}

	best->current_weight -= total;

	if (now - best->checked > best->fail_timeout) {
		best->checked = now;
	}

	return best;
}

//更改后端状态,后端返回5xx，就认为失败
TSReturnCode RoundRobinBalancer::os_response_back_status(uint target_id,
		TSHttpStatus status) {
//		TSDebug(PLUGIN_NAME," os_response_back_status => target_id -> %d, status -> %d ",target_id, status);
	BalancerTarget *peer;
	size_t t_len;
	uint i;
	time_t now;

	peer = NULL;
	t_len = 0;

	if (!targets_s.empty())
		t_len = targets_s.size();
	for (i = 0; i < t_len; i++) {
		if (targets_s[i]->id == target_id) {
			peer = targets_s[i];
			break;
		}
	}
	if (peer == NULL && !targets_b.empty()) {
		t_len = targets_b.size();
		for (i = 0; i < t_len; i++) {
			if (targets_b[i]->id == target_id) {
				peer = targets_b[i];
				break;
			}
		}
	}

	if (peer == NULL)
		return TS_SUCCESS;

//		TSDebug(PLUGIN_NAME, "os_response_back_status check time %ld accessed time %ld! ",	peer->checked, peer->accessed);

	if (status >= FAIL_STATUS) {
		now = TShrtime() / TS_HRTIME_SECOND;
		peer->checked = now;
		peer->accessed = now;
		if (peer->down) {
			peer->timeout_fails++;
			peer->timeout_fails =
					peer->timeout_fails > MAX_FAIL_TIME ?
							MAX_FAIL_TIME : peer->timeout_fails;
//				TSDebug(PLUGIN_NAME, " os_response_back_status  target id-> %d is down again timeout_fails-> %d ",
//						peer->id, peer->timeout_fails);

		} else {
			peer->fails++;
			if (peer->max_fails) {
				peer->effective_weight -= peer->weight / peer->max_fails;
			}

			if (peer->fails >= peer->max_fails) {
				peer->down = 1;
				peer->timeout_fails = 1;
//					TSDebug(PLUGIN_NAME, " os_response_back_status  target id-> %d is down ", peer->id);
			}
		}

		if (peer->effective_weight < 0) {
			peer->effective_weight = 0;
		}

	} else {

		if (peer->accessed < peer->checked) {
			peer->fails = 0;
		}

		//如果有一次探测正常，就将timeout_fail--, 直到为1，则将该后端服务down状态去掉,后续可以优化一下
		if (peer->down) {			  //可以不用防止并发的情况
			if (peer->timeout_fails <= 1) {
				peer->down = 0;
				peer->timeout_fails = 1;
				peer->fails = 0;
				peer->effective_weight = peer->weight;
				peer->current_weight = 0;
				peer->accessed = 0;
				peer->checked = 0;
			} else {
				//当服务器状态从坏到好的时候，下降的基数稍微大点
				now = TShrtime() / TS_HRTIME_SECOND;
				peer->timeout_fails = peer->timeout_fails / 2;
				peer->timeout_fails =
						peer->timeout_fails ? peer->timeout_fails : 1;
				peer->checked = now;
				peer->accessed = now; //因为peer 状态还是down ，所以这里accessed 还需要赋值
			}
//				TSDebug(PLUGIN_NAME, " os_response_back_status target is down but return is OK, target->id %d", peer->id);
		}

	}
	peer = NULL;
	return TS_SUCCESS;
}

BalancerTarget *RoundRobinBalancer::MakeBalancerTarget(const char *strval) {
	BalancerTarget *target = new BalancerTarget();

	union {
		struct sockaddr_storage storage;
		struct sockaddr sa;
	} address;

	memset(&address, 0, sizeof(address));

	// First, check whether we have an address literal.
	const char *is_address_literal = strrchr(strval, ',');
	if ( NULL == is_address_literal && ats_ip_pton(strval, &address.sa) == 0) {
		char namebuf[INET6_ADDRSTRLEN];

		target->port = ats_ip_port_host_order(&address.sa);
		target->name = ats_ip_ntop(&address.sa, namebuf, sizeof(namebuf));

	} else {
		//格式ip:port,是否为备用线路,权重,最大失败次数,禁用时间
		// 192.168.8.7:80,0,1,10,20   如果只有ip 后面几个参数都是默认值
		int target_array[4] = { 0, 1, 10, 20 };
		uint a_count = sizeof(target_array) / sizeof(target_array[0]);
		uint s_count = 0;
		const char *comma = strrchr(strval, ':');
		if (comma) {
			target->name = std::string(strval, (comma - strval));
			target->port = strtol(comma + 1, NULL, 10);

			comma = strchr(comma + 1, ',');
			while ( NULL != comma && s_count <= a_count) {
				target_array[s_count] = strtol(comma + 1, NULL, 10);
				s_count += 1;
				comma = strchr(comma + 1, ',');
			}
		} else {
			comma = strchr(strval, ',');
			if (comma) {
				target->name = std::string(strval, (comma - strval));
				while ( NULL != comma && s_count <= a_count) {
					target_array[s_count] = strtol(comma + 1, NULL, 10);
					s_count += 1;
					comma = strchr(comma + 1, ',');
				}
			} else {
				target->name = strval;
			}
		}
		target->backup = target_array[0];
		target->weight = target_array[1];
		target->max_fails = target_array[2];
		target->fail_timeout = target_array[3];
	}

	if (target->port > INT16_MAX) {
		TSError("[%s] Ignoring invalid port number for target '%s'",PLUGIN_NAME,strval);
		target->port = 0;
	}

	TSDebug(PLUGIN_NAME,
			"balancer target -> %s  target->name -> %s target->port -> %d target->backup ->%d target->weight -> %d target->max_fails ->%d target->fail_timeout -> %ld",
			strval, target->name.c_str(), target->port, target->backup,
			target->weight, target->max_fails, target->fail_timeout);

	return target;
}


