/////////////////////////////////////////////////////////////////////
// hosting.config form and rule contains following:
//                              (Input Type Names)
//    Primary Dest Type   	pd_type
//    Primary Dest Value	pd_value	
//    Partitions                partitions
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
function Rule(pd_type, pd_val, partitions) { 
 this.pd_type = pd_type;
 this.pd_val = pd_val;
 this.partitions = partitions;
}

// 
// This function creates a new Rule object for ruleList based
// on values entered in form. 
// NOTE: In Netscape, we cannot access the selected value with: 
// document.form1.rule_type.value. So we have to first get the 
// selected index to get selected option's value
// 
function createRuleFromForm() {
  index = document.form1.pd_type.selectedIndex; 
  var pd_type = document.form1.pd_type.options[index].value;

  var pd_val = document.form1.pd_val.value;
  var partitions = document.form1.partitions.value;
 
  var rule = new Rule(pd_type, pd_val, partitions); 

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

  text = rule.pd_type + eq + rule.pd_val;
   
  if (rule.partitions != "") text += delim + "partitions" + eq + rule.partitions; 
  
  return text;
}

//
// A Rule object also has a hidden format which will be used to help convert
// it into an Ele when user hits "Apply"
// 
function hiddenFormat(rule)
{
  var text = rule.pd_type + "^" + rule.pd_val + "^" + rule.partitions + "^"; 
  return text; 
}

// 
// This function updates the selected Rule object with the values 
// entered on the form. 
// 
function updateRule(index) 
{
  sel = document.form1.pd_type.selectedIndex;
  ruleList[index].pd_type = document.form1.pd_type.options[sel].value;

  ruleList[index].pd_val = document.form1.pd_val.value;
  ruleList[index].partitions = document.form1.partitions.value;
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

  for (i=0; i < document.form1.pd_type.length; i++) {
    if (document.form1.pd_type.options[i].value == ruleList[index].pd_type)
       document.form1.pd_type.selectedIndex = i;
  }

  document.form1.pd_val.value = ruleList[index].pd_val;
  document.form1.partitions.value = ruleList[index].partitions;
}

// 
// clears all the fields in the form
// 
function clearForm() 
{
  document.form1.pd_type.value = "domain";
  document.form1.pd_val.value = "";
  document.form1.partitions.value = "";

  document.form1.list1.selectedIndex = -1; 	
}

// 
// form validation - put detailed alert messages in this function
//
function validInput()
{
  var pd_index = document.form1.pd_type.selectedIndex;
  if (document.form1.pd_type.options[pd_index].value == "" || 
      document.form1.pd_val.value == "" ||
      document.form1.partitions.value == ""){
    alert("Invalid input. Cannot add the rule.");
    return false;
  }
	
  return true;   	
}
