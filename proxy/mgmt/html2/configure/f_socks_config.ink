/////////////////////////////////////////////////////////////////////
// socks.config form and rule contains following:
//    Rule Type                 rule_type
// (INK_SOCKS_BYPASS rule type) 
//    Dest IP address list       dest_ip
// (INK_SOCKS_AUTH rule type)
//    Username                  user
//    Password                  password
// (INK_SOCKS_MULTIPLE rule type)
//    SOck Servers List         socks_servers
//    Round Robin               round_robin
// ** NOTE: the input type names must match those created in 
//          writeSocksConfigForm
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
function Rule(rule_type, dest_ip, user, password, socks_servers, round_robin) {
 this.rule_type = rule_type; 
 this.dest_ip = dest_ip;
 this.user = user;
 this.password = password;
 this.socks_servers = socks_servers;
 this.round_robin = round_robin;
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

  var user = document.form1.user.value;
  var password = document.form1.password.value;
  var dest_ip = document.form1.dest_ip.value;
  var socks_servers = document.form1.socks_servers.value;
 
  index = document.form1.round_robin.selectedIndex;
  var round_robin = document.form1.round_robin.options[index].value;
 
  var rule = new Rule(rule_type, dest_ip, user, password, socks_servers, round_robin);

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
  if (rule.user != "") text += delim + "Username" + eq + rule.user;
  if (rule.password != "") text += delim + "Password" + eq + rule.password;
  if (rule.dest_ip != "") text += delim + "Destination IP" + eq + rule.dest_ip;
  if (rule.socks_servers != "") text += delim + "Socks Servers" + eq + rule.socks_servers;
  if (rule.round_robin != "") text += delim + "Round Robin" + eq + rule.round_robin; 

  return text;
}

//
// A Rule object also has a hidden format which will be used to help convert
// it into an Ele when user hits "Apply"
// 
function hiddenFormat(rule)
{
  var delim = "^";

  var text = rule.rule_type + delim + rule.dest_ip + delim + rule.user + delim + rule.password + delim + rule.socks_servers + delim + rule.round_robin + delim; 
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

  ruleList[index].user = document.form1.user.value;
  ruleList[index].password = document.form1.password.value;
  ruleList[index].dest_ip = document.form1.dest_ip.value;
  ruleList[index].socks_servers = document.form1.socks_servers.value;

  sel = document.form1.round_robin.selectedIndex; 
  ruleList[index].round_robin = document.form1.round_robin.options[sel].value;
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

  document.form1.user.value = ruleList[index].user;
  document.form1.password.value = ruleList[index].password;
  document.form1.dest_ip.value = ruleList[index].dest_ip;
  document.form1.socks_servers.value = ruleList[index].socks_servers;

  for (i=0; i < document.form1.round_robin.length; i++) {
    if (document.form1.round_robin.options[i].value == ruleList[index].round_robin)
       document.form1.round_robin.selectedIndex = i;
  }
}

// 
// clears all the fields in the form
// 
function clearForm() 
{
  document.form1.rule_type.value = "no_socks";
  document.form1.user.value = "";
  document.form1.password.value = "";
  document.form1.dest_ip.value = "";
  document.form1.socks_servers.value = "";
  document.form1.round_robin.value = "";

  document.form1.list1.selectedIndex = -1; 	
}

// 
// form validation - put detailed alert messages in this function
//
function validInput()
{
  var rule_index = document.form1.rule_type.selectedIndex;
  var rr_index = document.form1.round_robin.selectedIndex;

  if (rule_index < 0 ) {
	alert("Need to specify a 'Rule Type'");
	return false;
  }

  var type = document.form1.rule_type.options[rule_index].value;

  if (type == "no_socks") {
    if (document.form1.dest_ip.value == "" ||
	document.form1.user.value != "" ||
	document.form1.password.value != "" ||
	document.form1.socks_servers.value != "" ||
	document.form1.round_robin.options[rr_index].value != "") {
	   alert("Only specify 'Destination IP'."); 
	   return false;
     }
  } else if (type == "auth") { 
    if (document.form1.user.value == "" ||
	document.form1.password.value == "" ||
	document.form1.dest_ip.value != "" ||
	document.form1.socks_servers.value != "" ||
	document.form1.round_robin.options[rr_index].value != "") {
	   alert("Only specify 'Username' and 'Password'"); 
	   return false;
     }
  } else if (type == "multiple_socks") {
    if (document.form1.dest_ip.value == "" ||
	document.form1.socks_servers.value == "") {
	   alert("Need to specify 'Destination IP' and 'SOCKS Servers'"); 
	   return false;
    }
    if (document.form1.user.value != "" ||
	document.form1.password.value != "") {
	   alert("Only specify 'Destination IP', 'SOCKS Servers', and/or 'Round Robin'"); 
	   return false;
     }
  }

  return true;   	
}
