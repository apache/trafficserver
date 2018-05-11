/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/* data.h:  data needed to conduct a sliced transaction.
 */

#pragma once

#include "ts/ts.h"

class SliceConfig;

class SliceData
{
	SliceData() = delete;
	SliceData(SliceData const &) = delete;
	SliceData & operator=(SliceData const &) = delete;

public:

	SliceConfig const * config; // buffer sizes, etc

	TSHttpTxn txnp;
	TSVIO output_vio;
	TSIOBuffer output_buffer;
	TSIOBufferReader output_reader;

	explicit
	SliceData
		( SliceConfig const * const _config
		, TSHttpTxn txn
		);

	~SliceData
		();
};
