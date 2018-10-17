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
	"github.com/apache/trafficserver/tools/voluspa"
)

func init() {
	voluspa.AdaptersRegistry.AddAdapter(&DisableCacheAdapter{})
}

type DisableCacheAdapter struct {
}

func (p *DisableCacheAdapter) PluginType() voluspa.PluginType {
	return voluspa.CompoundAdapter
}

func (p *DisableCacheAdapter) Type() voluspa.AdapterType {
	return voluspa.AdapterType("disable_cache")
}

func (p *DisableCacheAdapter) CompoundType() voluspa.AdapterType {
	return adapterType
}

func (p *DisableCacheAdapter) ConfigParameters() []string {
	return []string{"disable_cache"}
}

func (p *DisableCacheAdapter) PParams(env *voluspa.ConfigEnvironment) ([]string, error) {
	return []string{"proxy.config.http.cache.http=0"}, nil
}
