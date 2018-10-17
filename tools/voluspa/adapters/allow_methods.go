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
	"bytes"
	"fmt"

	"github.com/apache/trafficserver/tools/voluspa"
)

func init() {
	voluspa.AdaptersRegistry.AddAdapter(&AllowMethodsAdapter{})
}

type AllowMethodsAdapter struct {
}

func (p *AllowMethodsAdapter) Weight() int {
	return 140
}

func (p *AllowMethodsAdapter) PluginType() voluspa.PluginType {
	return voluspa.ActionAdapter
}

func (p *AllowMethodsAdapter) Type() voluspa.AdapterType {
	return voluspa.AdapterType("allow_methods")
}

func (p *AllowMethodsAdapter) ConfigParameters() []string {
	return []string{"allow_methods"}
}

func (p *AllowMethodsAdapter) Content(env *voluspa.ConfigEnvironment) (*bytes.Buffer, error) {
	methods, err := env.RemapOptions.ValueByNameAsSlice("allow_methods")
	if err != nil {
		return nil, err
	}

	content := &bytes.Buffer{}
	content.WriteString("@action=allow")
	for _, method := range methods {
		fmt.Fprintf(content, " @method=%s", method)
	}

	return content, nil
}
