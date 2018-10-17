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

	"github.com/apache/trafficserver/tools/voluspa"
	"github.com/apache/trafficserver/tools/voluspa/adapters/util"
)

const (
	LogTypeAdapterParameter = "log_type"
)

func init() {
	voluspa.AdaptersRegistry.AddAdapter(&LogTypeAdapter{})
}

type LogTypeAdapter struct {
}

func (p *LogTypeAdapter) PluginType() voluspa.PluginType {
	return voluspa.CompoundAdapter
}

func (p *LogTypeAdapter) Type() voluspa.AdapterType {
	return voluspa.AdapterType("log_type")
}

func (p *LogTypeAdapter) CompoundType() voluspa.AdapterType {
	return adapterType
}

func (p *LogTypeAdapter) Name() string {
	return ""
}

func (p *LogTypeAdapter) ConfigParameters() []string {
	return []string{LogTypeAdapterParameter}
}

func (p *LogTypeAdapter) Content(env *voluspa.ConfigEnvironment) (*bytes.Buffer, error) {
	content := &bytes.Buffer{}

	val, err := env.RemapOptions.ValueByNameAsString(LogTypeAdapterParameter)
	if err != nil {
		return nil, err
	}

	if val == "public" {
		return nil, nil
	}
	content.WriteString(util.FormatSimpleHeaderRewrite("REMAP_PSEUDO_HOOK", `set-header @cdnlog "private"`))

	return content, nil
}
