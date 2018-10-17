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

package headerrewrite

import (
	"bytes"
	"fmt"

	"github.com/apache/trafficserver/tools/voluspa"
	"github.com/apache/trafficserver/tools/voluspa/adapters/util"
)

const (
	DefaultTTLAdapterParameter = "default_ttl"
)

func init() {
	voluspa.AdaptersRegistry.AddAdapter(&DefaultTTLAdapter{})
}

type DefaultTTLAdapter struct {
}

func (p *DefaultTTLAdapter) PluginType() voluspa.PluginType {
	return voluspa.CompoundAdapter
}

func (p *DefaultTTLAdapter) Type() voluspa.AdapterType {
	return voluspa.AdapterType("default_ttl")
}

func (p *DefaultTTLAdapter) CompoundType() voluspa.AdapterType {
	return adapterType
}

func (p *DefaultTTLAdapter) Name() string {
	return ""
}

func (p *DefaultTTLAdapter) ConfigParameters() []string {
	return []string{DefaultTTLAdapterParameter}
}

func (p *DefaultTTLAdapter) Content(env *voluspa.ConfigEnvironment) (*bytes.Buffer, error) {
	content := &bytes.Buffer{}

	cacheControl, err := env.RemapOptions.ValueByNameAsString(DefaultTTLAdapterParameter)
	if err != nil {
		return nil, util.FormatError(DefaultTTLAdapterParameter, err)
	}

	// translate a simple duration to a max-age public directive
	if maxAge, err := util.DurationToSeconds(cacheControl); err == nil {
		cacheControl = fmt.Sprintf("max-age=%s, public", maxAge)
	}

	content.WriteString(fmt.Sprintf(
		`cond %%{READ_RESPONSE_HDR_HOOK} [AND]
cond %%{HEADER:Cache-Control} ="" [AND]
%s
    set-header Cache-Control %q`,
		conditionStatusCodeSuccess, cacheControl,
	))

	return content, nil
}
