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
	"strconv"
	"strings"

	"github.com/apache/trafficserver/tools/voluspa"
)

const (
	CacheKeyAdapterParameter = "cachekey"
)

func init() {
	voluspa.AdaptersRegistry.AddAdapter(&CacheKeyAdapter{})
}

type CacheKeyAdapter struct {
}

func (p *CacheKeyAdapter) PluginType() voluspa.PluginType {
	return voluspa.GeneralAdapter
}

func (p *CacheKeyAdapter) Type() voluspa.AdapterType {
	return voluspa.AdapterType("cache_key")
}

func (p *CacheKeyAdapter) SharedLibraryName() string {
	return "cachekey.so"
}

func (p *CacheKeyAdapter) ConfigParameters() []string {
	return []string{CacheKeyAdapterParameter}
}

func (p *CacheKeyAdapter) PParams(env *voluspa.ConfigEnvironment) ([]string, error) {
	alt, err := env.RemapOptions.ValueByNameAsStringMapInterface(CacheKeyAdapterParameter)
	if err != nil {
		return nil, err
	}

	args, err := p.processArgs(alt)
	if err != nil {
		return nil, err
	}

	return args, nil
}

func (p *CacheKeyAdapter) processArgs(alt map[string]interface{}) ([]string, error) {
	var args []string
	args = p.maybeAddArgs(alt["include_params"], "include-params", args)
	args = p.maybeAddArgs(alt["include_cookies"], "include-cookies", args)
	args = p.maybeAddArgs(alt["include_headers"], "include-headers", args)
	args = p.maybeAddArgs(alt["exclude_params"], "exclude-params", args)
	args = p.maybeAddArgs(alt["static_prefix"], "static-prefix", args)
	args = p.maybeAddArgs(alt["sort_query"], "sort-params", args)
	args = p.maybeAddArgs(alt["remove_all_params"], "remove-all-params", args)
	args = p.maybeAddArgs(alt["regex_replace_path"], "capture-path", args)
	args = p.maybeAddArgs(alt["regex_replace_path_uri"], "capture-path-uri", args)
	args = p.maybeAddArgs(alt["capture_header"], "capture-header", args)

	return args, nil
}

func (p *CacheKeyAdapter) paramToValue(i interface{}) string {
	switch val := i.(type) {
	case string:
		return val
	case int:
		return strconv.Itoa(val)
	case bool:
		return strconv.FormatBool(val)
	case []string:
		return strings.Join(val, ",")
	case []interface{}:
		var vals []string
		for _, v1 := range i.([]interface{}) {
			vS := v1.(string)
			vals = append(vals, vS)
		}
		return strings.Join(vals, ",")
	default:
		return ""
	}
}

func (p *CacheKeyAdapter) maybeAddArgs(option interface{}, param string, args []string) []string {

	// capture-header needs a separate "--capture-header=" flag for each one when there are multiple
	if param == "capture-header" && option != nil {
		for _, o := range option.([]interface{}) {
			args = append(args, "--capture-header="+o.(string))
		}
		return args
	}

	value := p.paramToValue(option)
	if len(value) > 0 {
		return append(args, fmt.Sprintf("--%s=%s", param, value))
	}

	return args
}
