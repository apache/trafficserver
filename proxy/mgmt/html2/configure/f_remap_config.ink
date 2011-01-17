/////////////////////////////////////////////////////////////////////
// remap.config form and rule contains following:
//    Rule Type                 rule_type
//    Scheme TYpe               from_scheme
//    Target:
//           host               from_host
//           port               from_port
//           path_prefix        from_path
//    Replacement:
//                              to_scheme
//           host               to_host
//           port               to_port
//           path_prefix        to_path
//    Media IXT tag             mixt
// ** NOTE: the input type names must match those created in 
//          writeRemapConfigForm
//
//  Licensed to the Apache Software Foundation (ASF) under one
//  or more contributor license agreements.  See the NOTICE file
//  distributed with this work for additional information
//  regarding copyright ownership.  The ASF licenses this file
//  to you under the Apache License, Version 2.0 (the
//  "License"); you may not use this file except in compliance
//  with the License.  You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.
/////////////////////////////////////////////////////////////////////

// 
// creates a new Rule object; initializes with parameters passed in
//
function Rule(rule_type, from_scheme, from_host, from_port, from_path, to_scheme, to_host, to_port, to_path, mixt) {
 this.rule_type = rule_type; 
 this.from_scheme = from_scheme;
 this.from_host = from_host;
 this.from_port = from_port;
 this.from_path = from_path;
 this.to_scheme = to_scheme; 
 this.to_host = to_host;
 this.to_port = to_port;
 this.to_path = to_path;
 this.mixt = mixt; 
}

// 
// This function creates a new Rule object for ruleList based
// on values entered in form. 
// NOTE: In Netscape, we cannot access the selected value with: 
// document.form1.rule_type.value. So we have to first get the 
// selected index to get selected option's value
// 
function createRuleFromForm() {
  var index = document.form1.rule_type.selectedIndex;
  var rule_type = document.form1.rule_type.options[index].value;
  
  index = document.form1.from_scheme.selectedIndex; 
  var from_scheme = document.form1.from_scheme.options[index].value;

  var from_host = document.form1.from_host.value;
  var from_port = document.form1.from_port.value;
  var from_path = document.form1.from_path.value;

  index = document.form1.to_scheme.selectedIndex; 
  var to_scheme = document.form1.to_scheme.options[index].value;

  var to_host = document.form1.to_host.value;
  var to_port = document.form1.to_port.value;
  var to_path = document.form1.to_path.value;

  index = document.form1.mixt.selectedIndex;
  var mixt = document.form1.mixt.options[index].value;
 
  var rule = new Rule(rule_type, from_scheme, from_host, from_port, from_path, to_scheme, to_host, to_port, to_path, mixt);

  return rule;
}

//
// This function displays the Rule object's information in the format
// that's used in the select list.
//
function textFormat(rule, write)
{
  var text = "";
  var delim = "";
  var eq = "=";  
  var space = "";
  if (write == 1) {
     space = "&nbsp;&nbsp;";
  } else {  
     space = "  ";
  }
  delim = space + "," + space;

  text = "Rule Type" + eq + rule.rule_type;

  text += delim + "From URL" + eq + rule.from_scheme + "://" + rule.from_host;
  if (rule.from_port != "") text += ":" + rule.from_port;
  if (rule.from_path != "") text += "/" + rule.from_path;

  text += delim + "To URL" + eq + rule.to_scheme + "://" + rule.to_host;
  if (rule.to_port != "") text += ":" + rule.to_port;
  if (rule.to_path != "") text += "/" + rule.to_path;
  if (rule.mixt != "") text += delim + "MIXT Scheme" + eq + rule.mixt; 

  return text;
}

//
// A Rule object also has a hidden format which will be used to help convert
// it into an Ele when user hits "Apply"
// 
function hiddenFormat(rule)
{
  var delim = "^";

  var text = rule.rule_type + delim + rule.from_scheme + delim + rule.from_host + delim + rule.from_port + delim + rule.from_path + delim + rule.to_scheme + delim + rule.to_host + delim + rule.to_port + delim + rule.to_path + delim + rule.mixt + delim; 
  return text; 
}

// 
// This function updates the selected Rule object with the values 
// entered on the form. 
// 
function updateRule(index) 
{
  var sel = document.form1.rule_type.selectedIndex; 
  ruleList[index].rule_type =  document.form1.rule_type.options[sel].value;

  sel = document.form1.from_scheme.selectedIndex;
  ruleList[index].from_scheme = document.form1.from_scheme.options[sel].value;

  ruleList[index].from_host = document.form1.from_host.value;
  ruleList[index].from_port = document.form1.from_port.value;
  ruleList[index].from_path = document.form1.from_path.value;

  sel = document.form1.to_scheme.selectedIndex;
  ruleList[index].to_scheme = document.form1.to_scheme.options[sel].value;

  ruleList[index].to_host = document.form1.to_host.value;
  ruleList[index].to_port = document.form1.to_port.value;
  ruleList[index].to_path = document.form1.to_path.value;

  sel = document.form1.mixt.selectedIndex; 
  ruleList[index].mixt = document.form1.mixt.options[sel].value;
}

// 
// This function updates the elements on the form displayed to the 
// user with the values sotred in the ruleList; has an optional index arg
//
function updateForm(index)
{ 
  if (ruleList.length == 0) 
	return; 
	
  if (index == -1)	
    index = document.form1.list1.selectedIndex;
 
  var i;

  for (i=0; i < document.form1.rule_type.length; i++) {
     if (document.form1.rule_type.options[i].value == ruleList[index].rule_type)
       document.form1.rule_type.selectedIndex = i;
  }

  for (i=0; i < document.form1.from_scheme.length; i++) {
    if (document.form1.from_scheme.options[i].value == ruleList[index].from_scheme)
       document.form1.from_scheme.selectedIndex = i;
  }

  document.form1.from_host.value = ruleList[index].from_host;
  document.form1.from_port.value = ruleList[index].from_port;
  document.form1.from_path.value = ruleList[index].from_path;

  for (i=0; i < document.form1.to_scheme.length; i++) {
    if (document.form1.to_scheme.options[i].value == ruleList[index].to_scheme)
       document.form1.to_scheme.selectedIndex = i;
  }

  document.form1.to_host.value = ruleList[index].to_host;
  document.form1.to_port.value = ruleList[index].to_port;
  document.form1.to_path.value = ruleList[index].to_path;

  for (i=0; i < document.form1.mixt.length; i++) {
    if (document.form1.mixt.options[i].value == ruleList[index].mixt)
       document.form1.mixt.selectedIndex = i;
  }
}

// 
// clears all the fields in the form
// 
function clearForm() 
{
  document.form1.rule_type.value = "map";
  document.form1.from_scheme.value = "http";
  document.form1.from_host.value = "";
  document.form1.from_port.value = "";
  document.form1.from_path.value = "";
  document.form1.to_scheme.value = "http";
  document.form1.to_host.value = "";
  document.form1.to_port.value = "";
  document.form1.to_path.value = "";
  document.form1.mixt.value=""; 

  document.form1.list1.selectedIndex = -1; 	
}

// 
// form validation - put detailed alert messages in this function
//
function validInput()
{
  var rule_index = document.form1.rule_type.selectedIndex;
  var fr_scheme_index = document.form1.from_scheme.selectedIndex;
  var to_scheme_index = document.form1.to_scheme.selectedIndex;	
  var mixt_index = document.form1.mixt.selectedIndex;

  if (rule_index < 0 || fr_scheme_index < 0 || to_scheme_index < 0) {
	alert("Need to specify a 'Rule Type' and 'Scheme'");
	return false;
  }

  if (document.form1.rule_type.options[rule_index].value == "" || 
      document.form1.from_scheme.options[fr_scheme_index].value == "" || 
      document.form1.from_host.value == "" ||
      document.form1.to_host.value == "" ||
      document.form1.to_scheme.options[to_scheme_index].value == ""){
    	alert("Invalid input. Cannot add the rule.");
    	return false;
  }

  if (document.form1.from_port.value != "") {
	if (document.form1.from_port.value <= 0) {
	  alert("Port number must be >= 0");
	  return false;
	}
  }

  if (document.form1.to_port.value != "") {
	if (document.form1.to_port.value <= 0) {
	  alert("Port number must be >= 0");
	  return false;
	}
  }

  if (document.form1.mixt.options[mixt_index].value != "") {
	if (document.form1.from_scheme.options[fr_scheme_index].value != "rtsp" || 
            document.form1.to_scheme.options[to_scheme_index].value != "rtsp") {
	   alert("'MIXT Scheme' can only be specified with 'rtsp' scheme.");
	   return false;
        }
  }

  return true;   	
}
