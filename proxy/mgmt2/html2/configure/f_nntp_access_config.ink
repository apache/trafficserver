/////////////////////////////////////////////////////////////////////
// nntp_access.config form and rule contains following:
//    Client Group Type (ip, domain, hostname)    grp_type
//    Client Group Value                          grp_val
//    Access Type                                 access
//    Authenticator                               auth
//    User                                        user
//    Password                                    pass
//    Groups                                      groups
//    Deny Posting?                               post
// ** NOTE: the input type names must match those created in 
//          writeNntpAccessConfigForm
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
function Rule(grp_type, grp_val, access, auth, user, pass, groups, post) {
 this.grp_type = grp_type;
 this.grp_val = grp_val;
 this.access = access;
 this.auth = auth;
 this.user = user;
 this.pass = pass;
 this.groups = groups;
 this.post = post;
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

  index = document.form1.grp_type.selectedIndex; 
  var grp_type = document.form1.grp_type.options[index].value;

  var grp_val = document.form1.grp_val.value;

  index = document.form1.access.selectedIndex; 
  var access = document.form1.access.options[index].value;

  var auth = document.form1.auth.value;
  var user = document.form1.user.value;	
  var pass = document.form1.pass.value;
  var groups = document.form1.groups.value;

  index = document.form1.post.selectedIndex; 
  var post = document.form1.post.options[index].value;

  var rule = new Rule(grp_type, grp_val, access, auth, user, pass, groups, post);

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

  text = rule.grp_type + eq + rule.grp_val + delim + "Access Type" + eq + rule.access;
   
  if (rule.auth != "") text += delim + "Authenticator" + eq + rule.auth;
  if (rule.user != "") text += delim + "Username" + eq + rule.user;
  if (rule.pass != "") text += delim + "Password" + eq + rule.pass;
  if (rule.groups != "") text += delim + "Newsgroups" + eq + rule.groups;
  if (rule.post != "") text += delim + "Posting" + eq + rule.post;
  	
  return text;
}

//
// A Rule object also has a hidden format which will be used to help convert
// it into an Ele when user hits "Apply"
// 
function hiddenFormat(rule)
{
  var delim = "^"; 

  var text = rule.grp_type + delim + rule.grp_val + delim + rule.access + delim + rule.auth + delim + rule.user + delim + rule.pass + delim + rule.groups + delim + rule.post + delim; 
  return text; 
}

// 
// This function updates the selected Rule object with the values 
// entered on the form. 
// 
function updateRule(index) 
{
  var sel;

  sel = document.form1.grp_type.selectedIndex; 
  ruleList[index].grp_type =  document.form1.grp_type.options[sel].value;

  ruleList[index].grp_val = document.form1.grp_val.value;

  sel = document.form1.access.selectedIndex; 
  ruleList[index].access =  document.form1.access.options[sel].value;

  ruleList[index].auth = document.form1.auth.value;
  ruleList[index].user = document.form1.user.value;
  ruleList[index].pass = document.form1.pass.value;
  ruleList[index].groups = document.form1.groups.value;
 
  sel = document.form1.post.selectedIndex; 
  ruleList[index].post =  document.form1.post.options[sel].value;
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

  for (i=0; i < document.form1.grp_type.length; i++) {
     if (document.form1.grp_type.options[i].value == ruleList[index].grp_type)
       document.form1.grp_type.selectedIndex = i;
  }

  for (i=0; i < document.form1.access.length; i++) {
     if (document.form1.access.options[i].value == ruleList[index].access)
       document.form1.access.selectedIndex = i;
  }

  for (i=0; i < document.form1.post.length; i++) {
    if (document.form1.post.options[i].value == ruleList[index].post)
       document.form1.post.selectedIndex = i;
  }

  document.form1.grp_val.value = ruleList[index].grp_val;
  document.form1.auth.value = ruleList[index].auth;	
  document.form1.user.value = ruleList[index].user;
  document.form1.pass.value = ruleList[index].pass;
  document.form1.groups.value = ruleList[index].groups;	
}

// 
// clears all the fields in the form
// 
function clearForm() 
{
  document.form1.grp_type.value = "ip";
  document.form1.grp_val.value = "";
  document.form1.access.value = "allow";
  document.form1.auth.value = "";
  document.form1.user.value = "";
  document.form1.pass.value = "";
  document.form1.groups.value = "";
  document.form1.post.value = "";

  document.form1.list1.selectedIndex = -1; 
}

// 
// form validation - put detailed alert messages in this function
//
function validInput()
{
  var grp_index = document.form1.grp_type.selectedIndex;
  var acc_index = document.form1.access.selectedIndex;
  var post_index = document.form1.post.selectedIndex;

  if (grp_index < 0 || acc_index < 0) {
	alert("Need to specify a 'Client Group Type' and 'Access Type'");
	return false;
  }

  if (document.form1.grp_val.value == "" ) {
        alert("Need to specify 'Client Group Value'");
	return false;
  } 

  var accessT = document.form1.access.options[acc_index].value;
  if (accessT == "allow" || accessT == "deny") {
	if (document.form1.auth.value != "" || 
            document.form1.user.value != "" ||
	    document.form1.pass.value != "") {
          	alert ("Cannot specify 'Authenticator', 'User', or 'Password'"); 
		return false;
	} 
  } else if (accessT == "basic") {
	if (document.form1.user.value == "") {
		alert("Need to specify 'User'.");
		return false;
	} 
	if (document.form1.auth.value != "") {
		alert ("Cannot specify 'Authenticator'");
		return false;
	} 
  } else if (accessT == "generic") {
	if (document.form1.user.value != "" ||
	    document.form1.pass.value != "") {
          	alert ("Cannot specify 'User', or 'Password'"); 
		return false;
	} 
  } else if (accessT == "custom") {
	if (document.form1.auth.value == "") {
		alert("Need to specify 'Authenticator'.");
		return false;
	} 
	if ((document.form1.user.value != "" && document.form1.user.value != "required") ||
            (document.form1.pass.value!= "" && document.form1.pass.value != "required")){
		alert("'User' and 'Password' must either be unspecified or 'required'");
		return false;
	}
  }

  return true;   	
}
