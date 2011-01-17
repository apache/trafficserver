/////////////////////////////////////////////////////////////////////
// vaddrs.config form and rule contains following:
//    virtual IP            ip 
//    Interface             intr
//    Sub-Interface         sub_intr
// ** NOTE: the input type names must match those created in 
//          writeVaddrsConfigForm
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
function Rule(ip, intr, sub_intr) { 
 this.ip = ip;
 this.intr = intr;
 this.sub_intr = sub_intr;
}

// 
// This function creates a new Rule object for ruleList based
// on values entered in form. 
// NOTE: In Netscape, we cannot access the selected value with: 
// document.form1.rule_type.value. So we have to first get the 
// selected index to get selected option's value
// 
function createRuleFromForm() {
  var ip = document.form1.ip.value;
  var intr = document.form1.intr.value;
  var sub_intr = document.form1.sub_intr.value;
 
  var rule = new Rule(ip, intr, sub_intr);

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

  text = "Virtual IP" + eq + rule.ip + delim + "Ethernet Interface" + eq + rule.intr + delim + "Sub-Interface" + eq + rule.sub_intr;

  return text;
}

//
// A Rule object also has a hidden format which will be used to help convert
// it into an Ele when user hits "Apply"
// 
function hiddenFormat(rule)
{
  var delim = "^"; 

  var text = rule.ip + delim + rule.intr + delim + rule.sub_intr + delim; 
  return text; 
}

// 
// This function updates the selected Rule object with the values 
// entered on the form. 
// 
function updateRule(index) 
{
  ruleList[index].ip = document.form1.ip.value;
  ruleList[index].intr = document.form1.intr.value;
  ruleList[index].sub_intr = document.form1.sub_intr.value;
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
 
  document.form1.ip.value = ruleList[index].ip;
  document.form1.intr.value = ruleList[index].intr;
  document.form1.sub_intr.value = ruleList[index].sub_intr;
}

// 
// clears all the fields in the form
// 
function clearForm() 
{
  document.form1.ip.value = "";
  document.form1.intr.value = "";
  document.form1.sub_intr.value = "";

  document.form1.list1.selectedIndex = -1; 	
}

// 
// form validation - put detailed alert messages in this function
//
function validInput()
{
  if (document.form1.ip.value == "" || 
      document.form1.intr.value == "" ||
      document.form1.sub_intr.value == ""){
    alert("Need to specify 'Virtual IP', 'Interface', 'Sub-Interface'.");
    return false;
  }

  return true;   	
}
