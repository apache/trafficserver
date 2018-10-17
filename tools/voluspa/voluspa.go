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
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io/ioutil"
	"sort"
	"strconv"
	"strings"

	"github.com/xeipuuv/gojsonschema"
	yaml "gopkg.in/yaml.v2"
)

var (
	ErrMinimumConfigsNotMet = errors.New("Need at least 1 parsed config file")
)

type Voluspa struct {
	parsedConfigs []*CustomerConfig

	installLocation      string
	options              *Options
	defaultSchemaVersion int
}

func NewVoluspa() (*Voluspa, error) {
	return NewVoluspaWithOptions(&Options{}, DefaultTrafficserverConfigurationDir)
}

func NewVoluspaWithOptions(options *Options, installLocation string) (*Voluspa, error) {
	return &Voluspa{
		installLocation:      installLocation,
		options:              options,
		defaultSchemaVersion: 1,
	}, nil
}

func (v *Voluspa) SchemaDefaultVersion() int {
	return v.defaultSchemaVersion
}

func (v *Voluspa) SchemaDefinition(version int) ([]byte, error) {
	if version == 0 {
		version = v.SchemaDefaultVersion()
	}

	var filename string
	if len(v.options.SchemaLocation) > 0 {
		filename = fmt.Sprintf("%s/schema_v%d.json", v.options.SchemaLocation, version)
	} else {
		filename = fmt.Sprintf("schema_v%d.json", version)
	}

	return ioutil.ReadFile(filename)
}

func (v *Voluspa) validateSchema(contents []byte) error {
	var ycfg interface{}
	if err := yaml.Unmarshal(contents, &ycfg); err != nil {
		return err
	}

	ycfg = convert(ycfg)

	jcfg, err := json.Marshal(ycfg)
	if err != nil {
		return err
	}

	schemaVersion := v.SchemaDefaultVersion() // TODO extract schema_version from ycfg
	schemaContent, err := v.SchemaDefinition(schemaVersion)
	if err != nil {
		return err
	}

	schema, err := gojsonschema.NewSchema(gojsonschema.NewBytesLoader(schemaContent))
	if err != nil {
		return fmt.Errorf("error loading the version %d schema definition: %v", schemaVersion, err)
	}

	result, err := schema.Validate(gojsonschema.NewBytesLoader(jcfg))
	if err != nil {
		return err
	}

	if result.Valid() {
		return nil
	}

	var buf bytes.Buffer
	for _, desc := range result.Errors() {
		buf.WriteString(fmt.Sprintf("    - %s\n", desc))
	}

	return fmt.Errorf("\n%s", buf.String())
}

func convert(i interface{}) interface{} {
	switch x := i.(type) {
	case map[interface{}]interface{}:
		m2 := map[string]interface{}{}
		for k, v := range x {
			var k2 string
			switch k := k.(type) {
			case int:
				k2 = strconv.Itoa(k)
			default:
				k2 = k.(string)
			}
			m2[k2] = convert(v)
		}
		return m2
	case []interface{}:
		for i, v := range x {
			x[i] = convert(v)
		}
	}
	return i
}

// AddConfigByBytes takes a []byte buffer containing a Voluspa YAML config file and a filename and processes it
func (v *Voluspa) AddConfigByBytes(buffer []byte, filename string) error {
	if !v.options.SkipSchemaValidation {
		if err := v.validateSchema(buffer); err != nil {
			return wrapError(filename, err)
		}
	}

	cfg, err := NewPropertyConfigWithDefaults(buffer, v.options.DefaultsLocation)
	if err != nil {
		return wrapError(filename, err)
	}

	cc, err := NewCustomerConfig(cfg, filename, *v.options)
	if err != nil {
		return wrapError(filename, err)
	}

	v.parsedConfigs = append(v.parsedConfigs, cc)

	return nil
}

func (v *Voluspa) AddConfig(filename string) error {
	buffer, err := ioutil.ReadFile(filename)
	if err != nil {
		return err
	}
	return v.AddConfigByBytes(buffer, filename)
}

func (v *Voluspa) filterConfigsByCDNAndRole(cdns, roles []string) []*CustomerConfig {
	if len(cdns) == 0 {
		return v.parsedConfigs
	}

	cdnMap := make(map[string]interface{})
	for k := range cdns {
		cdnMap[cdns[k]] = nil
	}

	roleMap := make(map[string]interface{})
	for k := range roles {
		roleMap[roles[k]] = nil
	}

	var grouped []*CustomerConfig
	for _, config := range v.parsedConfigs {
		if config.lifecycle == Retired {
			continue
		}

		if !v.options.PromoteRolesToCDN && isPromotableRole(config.role) {
			continue
		}

		includeCDN := false
		for cdn := range config.cdn {
			if _, exists := cdnMap[cdn]; exists {
				includeCDN = true
				break
			}
		}

		var includeRole bool
		if len(config.role) > 0 {
			_, includeRole = roleMap[config.role]
		} else {
			includeRole = true
		}

		if includeCDN && includeRole {
			grouped = append(grouped, config)
		}
	}

	sort.Stable(sort.Reverse(byQPSAndName(grouped)))

	return grouped
}

// Validate will run all validation methods and return an error if any issues were found
// strict will enable extra checks that are known to fail with reasonable sets of configuration files
func (v *Voluspa) Validate(strict bool) error {
	var errs Errors
	errs = append(errs, v.ensurePropertyNameUniqueness()...)
	errs = append(errs, v.ensureAliasUniqueness()...)
	errs = append(errs, v.validateRegexOptions()...)
	errs = append(errs, v.ensureValidSchemes()...)

	if strict {
		errs = append(errs, v.ensureHostsResolve()...)
	}

	if len(errs) > 0 {
		return errs
	}

	return nil
}

// WriteAllFiles will write out all files, filter by passed in cdns and roles
func (v *Voluspa) WriteAllFiles(cdns, roles []string) error {
	allFiles, err := v.GetManagedFilesByCDNAndRole(cdns, roles)
	if err != nil {
		return err
	}

	writer, err := NewFilesystemWriter(v.options)
	if err != nil {
		return err
	}

	return writer.WriteFiles(allFiles)
}

// PropertyCount returns the number of properties
func (v *Voluspa) PropertyCount() int {
	return len(v.parsedConfigs)
}

type configGenerator interface {
	// Do processes parsedConfigs, optionally merging, returning a slice of ManagedFiles
	Do(parsedConfigs []*CustomerConfig, merge bool) ([]ManagedFile, error)
}

func getGenerators(options *Options) []configGenerator {
	var cg []configGenerator
	cg = append(cg, newPropertyRemapGenerator(options))
	cg = append(cg, newTopLevelRemapGenerator(options))
	cg = append(cg, newParentConfigurator(options))
	cg = append(cg, newSSLMulticertConfigurator(options))
	cg = append(cg, &HAProxyConfigGenerator{})
	cg = append(cg, newHostingConfigurator(options))
	return cg
}

func (v *Voluspa) GetManagedFiles(cdns []string) ([]ManagedFile, error) {
	return v.GetManagedFilesByCDNAndRole(cdns, nil)
}

func (v *Voluspa) GetManagedFilesByCDNAndRole(cdns, roles []string) ([]ManagedFile, error) {
	cc := v.filterConfigsByCDNAndRole(cdns, roles)
	merged := false
	if len(cdns) > 0 {
		merged = true
	}

	var allFiles []ManagedFile
	for _, g := range getGenerators(v.options) {
		managedFiles, err := g.Do(cc, merged)
		if err != nil {
			return nil, err
		}
		allFiles = append(allFiles, managedFiles...)
	}
	return allFiles, nil
}

func wrapError(filename string, err error) error {
	_, ok := err.(Errors)
	if !ok {
		return fmt.Errorf("problem loading %q: %s", filename, err)
	}
	return fmt.Errorf("problem loading %q:\n%s", filename, err)
}

// byQPSAndName implements sort.Interface, sorting by a CustomerConfig by qps/name.
type byQPSAndName []*CustomerConfig

func (s byQPSAndName) Len() int      { return len(s) }
func (s byQPSAndName) Swap(i, j int) { s[i], s[j] = s[j], s[i] }
func (s byQPSAndName) Less(i, j int) bool {
	if s[i].qps < s[j].qps {
		return true
	}
	if s[i].qps > s[j].qps {
		return false
	}

	// normalize the property names for sorting
	return strings.ToLower(s[i].property) > strings.ToLower(s[j].property)
}

// isPromotableRole returns true of role is promotable
func isPromotableRole(role string) bool {
	return strings.HasPrefix(role, "roles_")
}
