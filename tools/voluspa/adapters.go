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

import "bytes"

type PluginType int

const (
	UnknownVoluspaAdapter PluginType = iota
	GeneralAdapter
	ActionAdapter
	CompoundAdapter
)

type ConfigEnvironment struct {
	Property       string
	Options        Options
	RemapOptions   RemapOptions
	ConfigLocation ConfigLocation
	id             int
}

func newConfigEnvironment(p *PropertyConfig, options Options, mappingID int, configLocation ConfigLocation) *ConfigEnvironment {
	return &ConfigEnvironment{
		Property:       p.Property,
		Options:        options,
		ConfigLocation: configLocation,
		id:             mappingID,
	}
}

// Adapter needs to be implemented by all adapters
type Adapter interface {
	PluginType() PluginType
	Type() AdapterType
	ConfigParameters() []string
}

// ContentBasedAdapter should be implemented if a configuration is a blob of text (opposite of ParameterAdapter)
type ContentBasedAdapter interface {
	Content(*ConfigEnvironment) (*bytes.Buffer, error)
}

// SharedLibraryAdapter should be implemented if a configuration has an associated shared library
type SharedLibraryAdapter interface {
	SharedLibraryName() string
}

// CompoundTypeAdapter should be implemented if an adapter's output is shared with other like adapters
type CompoundTypeAdapter interface {
	CompoundType() AdapterType
}

// ParameterAdapter should be implemented if a configuration has @pparams
type ParameterAdapter interface {
	PParams(*ConfigEnvironment) ([]string, error)
}

// RawParameterAdapter should be implemented if a configuration has @pparams that should not be processed
type RawParameterAdapter interface {
	Raw() bool
}

// RolePParams is a map of role names to a list of PParam options
type RolePParams map[string][]string

// RoleifiedParameterAdapter should be implemented in adapters
// that can vary their options based on host role/type
type RoleifiedParameterAdapter interface {
	PParams(*ConfigEnvironment) (RolePParams, error)
}

// SubConfigAdapter should be implemented if a sub config is involved with rule/plugin
type SubConfigAdapter interface {
	Name() string
}

// ConfigFileSwitch should be implemented if a sub config is specified with a plugin switch (eg --config=)
type ConfigFileSwitch interface {
	ConfigFileSwitch() string
}

// ParentChildAdapter should be implemented if plugin is conditionally placed based on parent/child context
type ParentChildAdapter interface {
	ConfigLocations() []ConfigLocation
}

// RoleEnabled should be implemented if command-line should be protected/enabled by a jinja role-block
type RoleEnabled interface {
	Role() string
}

// Weighty should be implemented if an Adapter's output needs to be placed in a certain
// order (in relation to other adapter's output)
type Weighty interface {
	Weight() int
}
