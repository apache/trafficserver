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

package voluspa

import (
	"fmt"
)

type CDNTest struct {
	Type        CheckType                `json:"type,omitempty" yaml:"type,omitempty"`
	DomainNames []string                 `json:"domain_names,omitempty" yaml:"domain_names,omitempty"`
	URLs        []string                 `json:"urls,omitempty" yaml:"urls,omitempty"`
	HostTypes   []HostType               `json:"host_types,omitempty" yaml:"host_types,omitempty"`
	Description string                   `json:"description,omitempty" yaml:"description,omitempty"`
	Role        string                   `json:"role,omitempty" yaml:"role,omitempty"`
	Range       string                   `json:"range,omitempty" yaml:"range,omitempty"`
	Headers     map[string]string        `json:"headers,omitempty" yaml:"headers,omitempty"`
	PurgeBefore bool                     `json:"purge_before,omitempty" yaml:"purge_before,omitempty"`
	Insecure    bool                     `json:"insecure" yaml:"insecure"`
	Success     *CDNCheckSuccessCriteria `json:"success,omitempty" yaml:"success,omitempty"`
	Ciphers     []string                 `json:"ciphers,omitempty" yaml:"ciphers,omitempty"`
}

type CDNCheckSuccessCriteria struct {
	StatusCode    int               `json:"status_code" yaml:"status_code"`
	Headers       map[string]string `json:"headers,omitempty" yaml:"headers,omitempty"`
	SerialNumbers []string          `json:"serial_numbers,omitempty" yaml:"serial_numbers,omitempty"`
}

type HostType string

const (
	UnknownHostType HostType = "unknown"
	ParentHost      HostType = Parent
	ChildHost       HostType = Child
)

type CheckType string

const (
	UnknownCheckType CheckType = "unknown"
	HTTPCheckType    CheckType = "http"
	SSLCheckType     CheckType = "ssl"
)

func (h *HostType) UnmarshalYAML(unmarshal func(interface{}) error) error {
	var in string
	if err := unmarshal(&in); err != nil {
		return err
	}

	if len(in) == 0 {
		return fmt.Errorf("empty host type")
	}

	switch in {
	case Parent, Child:
		*h = HostType(in)
	default:
		return fmt.Errorf("unknown host type '%s'", in)
	}

	return nil
}
