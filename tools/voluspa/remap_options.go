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
	"encoding/json"
	"fmt"
	"strconv"
)

// RemapOptions key/value pairs from yaml configuration
type RemapOptions map[string]interface{}

func (r RemapOptions) HasOptionSet(name string) bool {
	var isSet bool
	switch v := r[name].(type) {
	case string:
		isSet = len(v) > 0
	case bool:
		isSet = v
	default:
		return false
	}
	return isSet
}

func (r RemapOptions) Clone() RemapOptions {
	ro := RemapOptions{}
	for k, v := range r {
		ro[k] = v
	}
	return ro
}

func (r RemapOptions) HasOption(name string) bool {
	_, ok := r[name]
	return ok
}

func (r RemapOptions) AddOption(name string, val interface{}) bool {
	r[name] = val
	return true
}

func (r RemapOptions) RemoveOption(name string) bool {
	delete(r, name)
	return true
}

func (r RemapOptions) ValueByNameAsBool(name string) (bool, error) {
	v, ok := r[name].(bool)
	if !ok {
		return false, fmt.Errorf("value for %q is not a boolean: %v", name, r[name])
	}
	return v, nil
}

func (r RemapOptions) ValueByNameAsString(name string) (string, error) {
	v, ok := r[name].(string)
	if !ok {
		return "", fmt.Errorf("value for %q is not a string: %v", name, r[name])
	}
	return v, nil
}

func (r RemapOptions) ValueByNameAsStringMapString(name string) (map[string]string, error) {
	v, ok := r[name].(map[interface{}]interface{})
	if !ok {
		return nil, fmt.Errorf("value for %q is of unexpected type, \"%T\". expected a map", name, r[name])
	}

	newv := make(map[string]string)
	for k, v := range v {
		newv[k.(string)] = v.(string)
	}
	return newv, nil
}

func (r RemapOptions) ValueByNameAsInt(key string) (int, error) {
	v, ok := r[key].(int)
	if !ok {
		return 0, fmt.Errorf("value for %q is not a integer: %v", key, r[key])
	}
	return v, nil
}

func (r RemapOptions) ValueByNameAsSlice(key string) ([]string, error) {
	switch t := r[key].(type) {
	case []interface{}:
		vs := make([]string, len(t))
		for i, d := range t {
			var val string
			switch d := d.(type) {
			case string:
				val = d
			case int:
				val = strconv.Itoa(d)
			}

			vs[i] = val
		}
		return vs, nil
	case []string:
		vs := make([]string, len(t))
		copy(vs, t)

		return vs, nil
	}
	return []string{}, fmt.Errorf("field %s is unhandled type: %T", key, r[key])
}

type remapOptionsJSON map[string]interface{}

func (r RemapOptions) makeRemapOptionsJSONStruct() (remapOptionsJSON, error) {
	rmo := remapOptionsJSON{}
	for k, v := range r {
		im, ok := r[k].(map[interface{}]interface{})
		if ok {
			m := make(map[string]interface{})
			for ik, iv := range im {
				sik, ok := ik.(string)
				if !ok {
					return nil, fmt.Errorf("non-string for key in map %+v", im)
				}
				m[sik] = iv
			}
			rmo[k] = m
			continue
		}
		rmo[k] = v
	}
	return rmo, nil
}

func (r RemapOptions) MarshalJSON() ([]byte, error) {
	roj, err := r.makeRemapOptionsJSONStruct()
	if err != nil {
		return nil, err
	}
	return json.Marshal(roj)
}

func (r RemapOptions) ValueByNameAsStringMapInterface(name string) (map[string]interface{}, error) {
	v, ok := r[name].(map[interface{}]interface{})
	if !ok {
		return nil, fmt.Errorf("value is of unexpected type. name=%s val=%v\n r=%+v\nt=%T", name, r[name], r, v)
	}

	newv := make(map[string]interface{})
	for k, v := range v {
		if v != nil {
			newv[k.(string)] = v.(interface{})
		}
	}
	return newv, nil
}
