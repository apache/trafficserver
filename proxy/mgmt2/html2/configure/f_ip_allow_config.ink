/////////////////////////////////////////////////////////////////////
// ip_allow.config form and rule contains following:
//    Source IP Address (single or range)        src_ip
//    IP action type                             ip_action 
// ** NOTE: the input type names must match those created in 
//          writeIpAllowConfigForm()
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
function Rule(src_ip, ip_action) {
 this.src_ip = src_ip;
 this.ip_action = ip_action;
}

// 
// This function creates a new Rule object for ruleList based
// on values entered in form. 
// NOTE: In Netscape, we cannot access the selected value with: 
// document.form1.rule_type.value. So we have to first get the 
// selected index to get selected option's value
// 
function createRuleFromForm() {
  var sel = document.form1.ip_action.selectedIndex; 
  var ip_action = document.form1.ip_action.options[sel].value;
  var src_ip = document.form1.src_ip.value;
  
  var rule = new Rule(src_ip, ip_action);
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
   
  text = "IP Action" + eq + rule.ip_action + delim + "Source IP" + eq + rule.src_ip;

  return text;
}

//
// A Rule object also has a hidden format which will be used to help convert
// it into an Ele when user hits "Apply"
// 
function hiddenFormat(rule)
{
  var text = rule.src_ip + "^" + rule.ip_action + "^"; 
  return text; 
}

// 
// This function updates the selected Rule object with the values 
// entered on the form. 
// 
function updateRule(index) 
{
  var sel = document.form1.ip_action.selectedIndex; 
  ruleList[index].ip_action = document.form1.ip_action.options[sel].value;
  ruleList[index].src_ip = document.form1.src_ip.value;
}

// 
// This function updates the elements on the form displayed to the 
// user with the values stored in the ruleList; has an optional index arg
//
function updateForm(index)
{ 
  var i;

  if (ruleList.length == 0) 
	return; 
	
  if (index == -1)	
    index = document.form1.list1.selectedIndex;

  document.form1.src_ip.value = ruleList[index].src_ip;
	
  for (i=0; i < document.form1.ip_action.length; i++) {
     if (document.form1.ip_action.options[i].value == ruleList[index].ip_action)
       document.form1.ip_action.selectedIndex = i;
  }
}

// 
// clears all the fields in the form
// 
function clearForm() 
{
  document.form1.src_ip.value = "";
  document.form1.ip_action.value = "ip_allow";

  document.form1.list1.selectedIndex = -1; 
}

// 
// form validation - put detailed alert messages in this function
//
function validInput()
{
  if (document.form1.src_ip.value == "" || document.form1.ip_action.value == "") {
	alert("Need to specify the source IP and the IP action"); 
	return false;
  } 

  return true;   	
}
