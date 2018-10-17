/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

package confremap

import (
	"fmt"

	"github.com/apache/trafficserver/tools/voluspa"
)

func init() {
	voluspa.AdaptersRegistry.AddAdapter(&CacheVersionAdapter{})
}

type CacheVersionAdapter struct {
}

func (s *CacheVersionAdapter) PluginType() voluspa.PluginType {
	return voluspa.CompoundAdapter
}

func (s *CacheVersionAdapter) Type() voluspa.AdapterType {
	return voluspa.AdapterType("cache_version")
}

func (s *CacheVersionAdapter) CompoundType() voluspa.AdapterType {
	return adapterType
}

func (s *CacheVersionAdapter) ConfigParameters() []string {
	return []string{"cache_version"}
}

func (s *CacheVersionAdapter) PParams(env *voluspa.ConfigEnvironment) ([]string, error) {
	cacheVersion, err := env.RemapOptions.ValueByNameAsInt("cache_version")
	if err != nil {
		return nil, err
	}

	return []string{
		fmt.Sprintf("proxy.config.http.cache.generation=%d", cacheVersion),
	}, nil
}
