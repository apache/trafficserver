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

package adapters

import (
	"fmt"

	"github.com/apache/trafficserver/tools/voluspa"
	"github.com/apache/trafficserver/tools/voluspa/adapters/util"
)

const (
	CachePromoteAdapterParameter       = "cache_promote"
	CachePromoteAdapterPolicyParameter = "policy"
)

func init() {
	voluspa.AdaptersRegistry.AddAdapter(&CachePromoteAdapter{})
}

type CachePromoteAdapter struct {
}

func (p *CachePromoteAdapter) PluginType() voluspa.PluginType {
	return voluspa.GeneralAdapter
}

func (p *CachePromoteAdapter) Type() voluspa.AdapterType {
	return voluspa.AdapterType("cache_promote")
}

func (p *CachePromoteAdapter) Name() string {
	return "cache_promote"
}

func (p *CachePromoteAdapter) SharedLibraryName() string {
	return "cache_promote.so"
}

func (p *CachePromoteAdapter) Role() string {
	return "roles_trafficserver_cache_promote"
}

func (p *CachePromoteAdapter) ConfigLocations() []voluspa.ConfigLocation {
	return []voluspa.ConfigLocation{voluspa.ChildConfig}
}

func (p *CachePromoteAdapter) ConfigParameters() []string {
	return []string{CachePromoteAdapterParameter}
}

func (p *CachePromoteAdapter) isValidPolicy(policy string) bool {
	return policy == "lru" || policy == "chance"
}

func (p *CachePromoteAdapter) PParams(env *voluspa.ConfigEnvironment) ([]string, error) {

	var alt map[string]interface{}
	alt, err := env.RemapOptions.ValueByNameAsStringMapInterface(CachePromoteAdapterParameter)
	if err != nil {
		return nil, err
	}

	if err = p.validateArgs(alt); err != nil {
		return nil, err
	}

	args, err := p.processArgs(alt)
	if err != nil {
		return nil, err
	}

	return args, nil
}

func (p *CachePromoteAdapter) validateArgs(alt map[string]interface{}) error {
	val := util.ParamToValue(alt["policy"])
	if len(val) == 0 || !p.isValidPolicy(val) {
		return util.FormatError(CachePromoteAdapterPolicyParameter, fmt.Errorf("invalid policy specified"))
	}

	return nil
}

func (p *CachePromoteAdapter) processArgs(alt map[string]interface{}) ([]string, error) {
	var args []string
	args = p.maybeAddArgs(alt["policy"], "policy", args)
	args = p.maybeAddArgs(alt["sample"], "sample", args)

	policy := util.ParamToValue(alt["policy"])
	if policy != "lru" {
		return args, nil
	}
	args = p.maybeAddArgs(alt["lru_hits"], "hits", args)
	args = p.maybeAddArgs(alt["lru_buckets"], "buckets", args)

	return args, nil
}

func (p *CachePromoteAdapter) maybeAddArgs(option interface{}, param string, args []string) []string {
	value := util.ParamToValue(option)
	switch param {
	case "policy":
		if p.isValidPolicy(value) {
			return append(args, fmt.Sprintf("--%s=%s", param, value))
		}
	case "sample":
		if len(value) > 0 {
			return append(args, fmt.Sprintf("--%s=%s%%", param, value))
		}
	default:
		if len(value) > 0 {
			return append(args, fmt.Sprintf("--%s=%s", param, value))
		}
	}
	return args
}
