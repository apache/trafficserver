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
	"github.com/apache/trafficserver/tools/voluspa/adapters/util"
)

const (
	NegativeCachingAdapterParameterTTL = "negative_ttl"
)

func init() {
	voluspa.AdaptersRegistry.AddAdapter(&NegativeCachingAdapter{})
}

type NegativeCachingAdapter struct {
}

func (p *NegativeCachingAdapter) PluginType() voluspa.PluginType {
	return voluspa.CompoundAdapter
}

func (p *NegativeCachingAdapter) Type() voluspa.AdapterType {
	return voluspa.AdapterType("negative_caching")
}

func (p *NegativeCachingAdapter) CompoundType() voluspa.AdapterType {
	return adapterType
}

func (p *NegativeCachingAdapter) ConfigParameters() []string {
	return []string{NegativeCachingAdapterParameterTTL}
}

func (p *NegativeCachingAdapter) PParams(env *voluspa.ConfigEnvironment) ([]string, error) {
	duration, err := env.RemapOptions.ValueByNameAsString(NegativeCachingAdapterParameterTTL)
	if err != nil {
		return nil, err
	}

	seconds, err := util.DurationToSeconds(duration)
	if err != nil {
		return nil, err
	}

	return []string{fmt.Sprintf("proxy.config.http.negative_caching_lifetime=%s", seconds)}, nil
}
