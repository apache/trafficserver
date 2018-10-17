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
	"strings"

	"github.com/apache/trafficserver/tools/voluspa"
	"github.com/apache/trafficserver/tools/voluspa/adapters/util"
)

const (
	AccessApprovalAdapterParameter = "access_approval"
)

func init() {
	voluspa.AdaptersRegistry.AddAdapter(&AccessApprovalAdapter{})
}

type AccessApprovalAdapter struct {
}

func (*AccessApprovalAdapter) Weight() int {
	return 1
}

func (p *AccessApprovalAdapter) PluginType() voluspa.PluginType {
	return voluspa.GeneralAdapter
}

func (p *AccessApprovalAdapter) Type() voluspa.AdapterType {
	return voluspa.AdapterType("access_approval")
}

func (p *AccessApprovalAdapter) Name() string {
	return "access_approval"
}

func (p *AccessApprovalAdapter) ConfigParameters() []string {
	return []string{AccessApprovalAdapterParameter}
}

func (p *AccessApprovalAdapter) SharedLibraryName() string {
	return "access_control.so"
}

func (p *AccessApprovalAdapter) PParams(env *voluspa.ConfigEnvironment) ([]string, error) {
	alt, err := env.RemapOptions.ValueByNameAsStringMapInterface(AccessApprovalAdapterParameter)
	if err != nil {
		return nil, util.FormatError(AccessApprovalAdapterParameter, err)
	}

	val, ok := alt["raw"]
	if !ok {
		return nil, nil
	}

	var pparams []string
	pparams = append(pparams, strings.Split(val.(string), "\n")...)

	return pparams, nil
}

func (p *AccessApprovalAdapter) Raw() bool {
	return true
}
