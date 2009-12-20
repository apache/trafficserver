/////////////////////////////////////////////////////////////////////
// nntp_servers.config form and rule contains following:
//    Hostname              hostname
//    Groups                groups
//    Treatment Type        treatment
//    Priority #            priority
//    Interface             intr
// ** NOTE: the input type names must match those created in 
//          writeNntpServersConfigForm
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
function Rule(hostname, groups, treatment, priority, intr) {
 this.hostname = hostname;
 this.groups = groups;
 this.treatment = treatment;
 this.priority = priority;
 this.intr = intr;
}

// 
// This function creates a new Rule object for ruleList based
// on values entered in form. 
// NOTE: In Netscape, we cannot access the selected value with: 
// document.form1.rule_type.value. So we have to first get the 
// selected index to get selected option's value
// 
function createRuleFromForm() {
  var index;

  var hostname = document.form1.hostname.value;
  var groups = document.form1.groups.value;

  index = document.form1.treatment.selectedIndex; 
  var treatment = document.form1.treatment.options[index].value;

  var priority = document.form1.priority.value;
  var intr = document.form1.intr.value;	

  var rule = new Rule(hostname, groups, treatment, priority, intr);

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

  text = "Hostname" + eq + rule.hostname + delim + "Newsgroups" + eq + rule.groups;
   
  if (rule.treatment != "") text += delim + "Treatment" + eq + rule.treatment;
  if (rule.priority != "") text += delim + "Priority" + eq + rule.priority;
  if (rule.intr != "") text += delim + "Interface" + eq + rule.intr;
  	
  return text;
}

//
// A Rule object also has a hidden format which will be used to help convert
// it into an Ele when user hits "Apply"
// 
function hiddenFormat(rule)
{
  var delim = "^"; 

  var text = rule.hostname + delim + rule.groups + delim + rule.treatment + delim + rule.priority + delim + rule.intr + delim; 
  return text; 
}

// 
// This function updates the selected Rule object with the values 
// entered on the form. 
// 
function updateRule(index) 
{
  var sel;

  ruleList[index].hostname = document.form1.hostname.value;
  ruleList[index].groups = document.form1.groups.value;

  sel = document.form1.treatment.selectedIndex; 
  ruleList[index].treatment =  document.form1.treatment.options[sel].value;

  ruleList[index].priority = document.form1.priority.value;
  ruleList[index].intr = document.form1.intr.value;
}

// 
// This function updates the elements on the form displayed to the 
// user with the values stored in the ruleList; has an optional index arg
//
function updateForm(index)
{ 
  if (ruleList.length == 0) 
	return; 
	
  if (index == -1)	
    index = document.form1.list1.selectedIndex;
 
  var i;

  for (i=0; i < document.form1.treatment.length; i++) {
     if (document.form1.treatment.options[i].value == ruleList[index].treatment)
       document.form1.treatment.selectedIndex = i;
  }

  document.form1.hostname.value = ruleList[index].hostname;
  document.form1.groups.value = ruleList[index].groups;	
  document.form1.priority.value = ruleList[index].priority;
  document.form1.intr.value = ruleList[index].intr;
}

// 
// clears all the fields in the form
// 
function clearForm() 
{
  document.form1.hostname.value = "";
  document.form1.groups.value = "";
  document.form1.treatment.value = "";
  document.form1.priority.value = "";
  document.form1.intr.value = "";

  document.form1.list1.selectedIndex = -1; 
}

// 
// form validation - put detailed alert messages in this function
//
function validInput()
{
  var treat_index = document.form1.treatment.selectedIndex;

  if (treat_index < 0) {
	alert("Need to specify 'Treatment Type'");
	return false;
  }

  if (document.form1.groups.value == "") {
	alert("Need to specify 'Groups'");
	return false;
  }

  if (document.form1.priority.value != "" && document.form1.priority.value != "0"){
	if (document.form1.priority.value < 0) {
	    alert ("'Priority' must be a positive number."); 
	    return false;
        }

	if (document.form1.treatment.options[treat_index].value != "") {
	    alert("'Priority' must be unspecified if a 'Treatment Type' is selected");
	    return false;
	}
  }

  return true;   	
}
