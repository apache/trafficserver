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

/*
 * This plugin looks for range requests and then creates a new
 * cache key url so that each individual range requests is written
 * to the cache as a individual object so that subsequent range
 * requests are read accross different disk drives reducing I/O
 * wait and load averages when there are large numbers of range
 * requests.
 */

#include "data.h"

#include "config.h"
#include "ts/apidefs.h"

#include <cstddef>

SlicerData :: SlicerData
	( SlicerConfig const * const _config
	)
	: config(_config)
	, output_vio(NULL)
	, output_buffer(NULL)
	, output_reader(NULL)
{ }

SlicerData :: ~SlicerData
	()
{
	if (NULL != output_vio)
	{
		;
	}

	if (NULL != output_buffer)
	{
		TSIOBufferDestroy(output_buffer);
	}

	if (NULL != output_reader)
	{
	}
}
