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
	"strings"

	"github.com/apache/trafficserver/tools/voluspa"
	"github.com/apache/trafficserver/tools/voluspa/adapters/util"
)

func init() {
	voluspa.AdaptersRegistry.AddAdapter(&VideoBackgroundFetchAdapter{})
}

const (
	VideoBackgroundFetchDirective = "video_background_fetch"

	VideoBackgroundFetchAdapterFrontend = "frontend"
	VideoBackgroundFetchAdapterBackend  = "backend"

	VideoBackgroundFetchAdapterFetchCount       = "fetch_count"
	VideoBackgroundFetchAdapterFetchPolicy      = "fetch_policy"
	VideoBackgroundFetchAdapterFetchMax         = "fetch_max"
	VideoBackgroundFetchAdapterAPIHeader        = "api_header"
	VideoBackgroundFetchAdapterFetchPathPattern = "fetch_path_pattern"
	VideoBackgroundFetchAdapterReplaceHost      = "replace_host"
	VideoBackgroundFetchAdapterNameSpace        = "name_space"
	VideoBackgroundFetchAdapterMetricsPrefix    = "metrics_prefix"
	VideoBackgroundFetchAdapterExactMatch       = "exact_match"
	VideoBackgroundFetchAdapterLogName          = "log_name"
)

type VideoBackgroundFetchAdapter struct {
}

func (p *VideoBackgroundFetchAdapter) PluginType() voluspa.PluginType {
	return voluspa.GeneralAdapter
}

func (p *VideoBackgroundFetchAdapter) Type() voluspa.AdapterType {
	return voluspa.AdapterType("video_background_fetch")
}

func (p *VideoBackgroundFetchAdapter) ConfigLocations() []voluspa.ConfigLocation {
	return []voluspa.ConfigLocation{voluspa.ChildConfig}
}

func (p *VideoBackgroundFetchAdapter) ConfigParameters() []string {
	return []string{VideoBackgroundFetchDirective}
}

func (p *VideoBackgroundFetchAdapter) Name() string {
	return "volcano"
}

func (p *VideoBackgroundFetchAdapter) SharedLibraryName() string {
	return "volcano.so"
}

func (p *VideoBackgroundFetchAdapter) PParams(env *voluspa.ConfigEnvironment) (voluspa.RolePParams, error) {

	alt, err := env.RemapOptions.ValueByNameAsStringMapInterface(VideoBackgroundFetchDirective)
	if err != nil {
		return nil, util.FormatError(VideoBackgroundFetchDirective, err)
	}

	// process default args
	args, err := p.processArgs(alt)
	if err != nil {
		return nil, err
	}

	results := voluspa.RolePParams{}
	results[voluspa.DefaultRole] = args

	for _, v := range []string{VideoBackgroundFetchAdapterFrontend, VideoBackgroundFetchAdapterBackend} {
		sub, found := alt[v]
		if !found {
			continue
		}

		submap, ok := sub.(map[interface{}]interface{})
		if !ok {
			continue
		}

		convertedMap, err := convertMap(submap)
		if err != nil {
			return nil, err
		}

		args, err := p.processArgs(convertedMap)
		if err != nil {
			return nil, err
		}

		args = append(args, fmt.Sprintf("--front=%t", v == VideoBackgroundFetchAdapterFrontend))

		// eg roles_video_fetch_backend or roles_video_fetch_frontend
		role := fmt.Sprintf("roles_video_fetch_%s", v)
		results[role] = args
	}

	return results, nil
}

// convertMap attempts to create a usable submap from the very generic map returned by RemapOptions
func convertMap(in map[interface{}]interface{}) (map[string]interface{}, error) {
	results := make(map[string]interface{})
	for k, v := range in {
		ck, ok := k.(string)
		if !ok {
			return nil, fmt.Errorf("key %+q is not string", k)
		}
		results[ck] = v
	}
	return results, nil
}

func (p *VideoBackgroundFetchAdapter) processArgs(alt map[string]interface{}) ([]string, error) {
	var args []string

	args = p.maybeAddArgs(alt[VideoBackgroundFetchAdapterFetchPolicy], "fetch-policy", args)
	args = p.maybeAddArgs(alt[VideoBackgroundFetchAdapterFetchCount], "fetch-count", args)
	args = p.maybeAddArgs(alt[VideoBackgroundFetchAdapterFetchMax], "fetch-max", args)
	args = p.maybeAddArgs(alt[VideoBackgroundFetchAdapterAPIHeader], "api-header", args)
	args = p.maybeAddArgs(alt[VideoBackgroundFetchAdapterFetchPathPattern], "fetch-path-pattern", args)
	args = p.maybeAddArgs(alt[VideoBackgroundFetchAdapterReplaceHost], "replace-host", args)
	args = p.maybeAddArgs(alt[VideoBackgroundFetchAdapterNameSpace], "name-space", args)
	args = p.maybeAddArgs(alt[VideoBackgroundFetchAdapterMetricsPrefix], "metrics-prefix", args)
	args = p.maybeAddArgs(alt[VideoBackgroundFetchAdapterExactMatch], "exact-match", args)
	args = p.maybeAddArgs(alt[VideoBackgroundFetchAdapterLogName], "log-name", args)

	//  from here on out, it's parsing fetch_path_pattern
	if _, found := alt[VideoBackgroundFetchAdapterFetchPathPattern]; !found {
		return args, nil
	}

	var fetchPatterns []string
	switch val := alt[VideoBackgroundFetchAdapterFetchPathPattern].(type) {
	case []interface{}:
		for _, ipattern := range val {
			pattern, ok := ipattern.(string)
			if !ok {
				return nil, util.FormatError(VideoBackgroundFetchAdapterFetchPathPattern, fmt.Errorf("Expected a string"))
			}
			fetchPatterns = append(fetchPatterns, pattern)
		}
	case []string:
		fetchPatterns = val
	default:
		return nil, util.FormatError(VideoBackgroundFetchAdapterFetchPathPattern, fmt.Errorf("Expected an array of strings"))
	}

	for _, pattern := range fetchPatterns {
		args = p.maybeAddArgs(pattern, "fetch-path-pattern", args)
	}

	return args, nil
}

func isValidFetchPolicy(policy string) bool {
	return policy == "simple" || strings.Contains(policy, "lru")
}

func (p *VideoBackgroundFetchAdapter) maybeAddArgs(option interface{}, param string, args []string) []string {
	value := util.ParamToValue(option)
	switch param {
	case "fetch-policy":
		if isValidFetchPolicy(value) {
			return append(args, fmt.Sprintf("--%s=%s", param, value))
		}
	default:
		if len(value) > 0 {
			return append(args, fmt.Sprintf("--%s=%s", param, value))
		}
	}
	return args
}
