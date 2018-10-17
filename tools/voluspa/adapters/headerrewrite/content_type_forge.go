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
	"sort"

	"github.com/apache/trafficserver/tools/voluspa"
	"github.com/apache/trafficserver/tools/voluspa/adapters/util"
)

const (
	ContentTypeForgeAdapterParameter = "content_type_forge"
)

func init() {
	voluspa.AdaptersRegistry.AddAdapter(&ContentTypeForgeAdapter{})
}

type ContentTypeForgeAdapter struct {
}

func (p *ContentTypeForgeAdapter) PluginType() voluspa.PluginType {
	return voluspa.CompoundAdapter
}

func (p *ContentTypeForgeAdapter) Type() voluspa.AdapterType {
	return voluspa.AdapterType("content_type_forge")
}

func (p *ContentTypeForgeAdapter) CompoundType() voluspa.AdapterType {
	return adapterType
}

func (p *ContentTypeForgeAdapter) Name() string {
	return ""
}

func (p *ContentTypeForgeAdapter) ConfigParameters() []string {
	return []string{ContentTypeForgeAdapterParameter}
}

func (p *ContentTypeForgeAdapter) Content(env *voluspa.ConfigEnvironment) (*bytes.Buffer, error) {
	content := &bytes.Buffer{}

	contentTypes, err := env.RemapOptions.ValueByNameAsStringMapString(ContentTypeForgeAdapterParameter)
	if err != nil {
		return nil, util.FormatError(ContentTypeForgeAdapterParameter, err)
	}

	var keys []string
	for k := range contentTypes {
		keys = append(keys, k)
	}
	sort.Strings(keys)

	for _, regex := range keys {
		contentType := contentTypes[regex]
		content.WriteString(fmt.Sprintf(
			`cond %%{SEND_RESPONSE_HDR_HOOK} [AND]
    cond %%{PATH} /%s/
    set-header Content-Type %s`,
			regex, contentType,
		))
		content.WriteString("\n")
	}

	return content, nil
}
