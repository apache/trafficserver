/////////////////////////////////////////////////////////////////////
// filter.config form and rule contains following:
//                              (Input Type Names)
//    Rule Type			rule_type
//    Primary Dest Type   	pd_type
//    Primary Dest Value	pd_value	
//    Time                      time
//    Source IP                 src_ip
//    Prefix			prefix
//    Suffix			suffix
//    Port			port
//    Method			method
//    Scheme			scheme
//    Header Type               hdr_type (only for keep_hdr rule type)
//    (The following apply only to ldap rule type)
//                              server
//                              dn
//                              realm
//                              uid_filter
//                              attr_name 
//                              attr_val
//                              redirect_url
//                              bind_dn
//                              bind_pwd
//                              bind_pwd_file
//    Media-IXT Tag             mixt
// ** NOTE: the input type names must match those created in 
//          writeFilterConfigForm
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
function Rule(rule_type, pd_type, pd_val, time, src_ip, prefix, suffix, port, method, scheme, hdr_type, server, dn, realm, uid_filter, attr_name, attr_val, redirect_url, bind_dn, bind_pwd, bind_pwd_file, mixt) {
 this.rule_type = rule_type; 
 this.pd_type = pd_type;
 this.pd_val = pd_val;
 this.time = time;
 this.src_ip = src_ip;
 this.prefix = prefix;
 this.suffix = suffix;
 this.port = port;
 this.method = method;
 this.scheme = scheme;
 this.hdr_type = hdr_type; 
 this.server = server;
 this.dn = dn;
 this.realm = realm; 
 this.uid_filter = uid_filter;
 this.attr_name = attr_name;
 this.attr_val = attr_val; 
 this.redirect_url = redirect_url;
 this.bind_dn = bind_dn;
 this.bind_pwd = bind_pwd;
 this.bind_pwd_file = bind_pwd_file; 
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
  
  index = document.form1.pd_type.selectedIndex; 
  var pd_type = document.form1.pd_type.options[index].value;

  var pd_val = document.form1.pd_val.value;
  var time = document.form1.time.value;
  var src_ip = document.form1.src_ip.value;
  var prefix = document.form1.prefix.value;
  var suffix = document.form1.suffix.value;
  var port = document.form1.port.value;

  index = document.form1.method.selectedIndex;
  var method = document.form1.method.options[index].value;

  index = document.form1.scheme.selectedIndex; 
  var scheme = document.form1.scheme.options[index].value;

  index = document.form1.hdr_type.selectedIndex;
  var hdr_type = document.form1.hdr_type.options[index].value;

  var server = document.form1.server.value;	
  var dn = document.form1.dn.value;
  var realm = document.form1.realm.value;
  var uid_filter = document.form1.uid_filter.value;
  var attr_name = document.form1.attr_name.value;
  var attr_val = document.form1.attr_val.value;
  var redirect_url = document.form1.redirect_url.value;
  var bind_dn = document.form1.bind_dn.value;
  var bind_pwd = document.form1.bind_pwd.value;
  var bind_pwd_file = ""; // dummy placeholder

  index = document.form1.mixt.selectedIndex;
  var mixt = document.form1.mixt.options[index].value;
 
  var rule = new Rule(rule_type, pd_type, pd_val, time, src_ip, prefix, suffix, port, method, scheme, hdr_type, server, dn, realm, uid_filter, attr_name, attr_val, redirect_url, bind_dn, bind_pwd, bind_pwd_file, mixt); 

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

  text = "Rule Type" + eq + rule.rule_type + delim + rule.pd_type + eq + rule.pd_val;
   
  if (rule.hdr_type != "") text += delim + "Header Type" + eq + rule.hdr_type;
  if (rule.server != "") text += delim + "Server Name" + eq + rule.server; 
  if (rule.dn != "") text += delim + "DN" + eq + rule.dn;
  if (rule.uid_filter != "") text += delim + "UID Filter" + eq + rule.uid_filter;
  if (rule.attr_name != "") text += delim + "Attribute Name" + eq + rule.attr_name;
  if (rule.attr_val != "") text += delim + "Attribute Value" + eq + rule.attr_val;
  if (rule.realm != "") text += delim + "Realm" + eq + rule.realm;
  if (rule.redirect_url != "") text += delim + "Redirect Url" + eq + rule.redirect_url; 
  if (rule.bind_dn != "") text += delim + "Bind DN" + eq + rule.bind_dn;
  if (rule.bind_pwd != "") text += delim + "Bind Password" + eq + rule.bind_pwd;

  if (rule.time != "") text += delim + "Time" + eq + rule.time; 
  if (rule.prefix != "") text += delim + "Prefix" + eq + rule.prefix;
  if (rule.suffix != "") text += delim + "Suffix" + eq + rule.suffix;
  if (rule.src_ip != "") text += delim + "Source IP" + eq + rule.src_ip;
  if (rule.port != "") text += delim + "Port" + eq + rule.port;
  if (rule.method != "") text += delim + "Method" + eq + rule.method;
  if (rule.scheme != "") text += delim + "Scheme" + eq + rule.scheme;
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
  var text = rule.rule_type + delim + rule.pd_type + delim + rule.pd_val + delim + rule.time + delim + rule.src_ip + delim + rule.prefix + delim + rule.suffix + delim + rule.port + delim + rule.method + delim + rule.scheme + delim + rule.hdr_type + delim + rule.server + delim + rule.dn + delim + rule.realm + delim + rule.uid_filter + delim + rule.attr_name + delim + rule.attr_val + delim + rule.redirect_url + delim + rule.bind_dn + delim + rule.bind_pwd + delim + rule.bind_pwd_file + delim + rule.mixt + delim ; 
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

  sel = document.form1.pd_type.selectedIndex;
  ruleList[index].pd_type = document.form1.pd_type.options[sel].value;

  ruleList[index].pd_val = document.form1.pd_val.value;
  ruleList[index].time = document.form1.time.value;
  ruleList[index].src_ip = document.form1.src_ip.value;
  ruleList[index].prefix = document.form1.prefix.value;
  ruleList[index].suffix = document.form1.suffix.value;
  ruleList[index].port = document.form1.port.value;

  sel = document.form1.method.selectedIndex;
  ruleList[index].method = document.form1.method.options[sel].value;

  sel = document.form1.scheme.selectedIndex; 
  ruleList[index].scheme = document.form1.scheme.options[sel].value;

  sel = document.form1.hdr_type.selectedIndex; 
  ruleList[index].hdr_type = document.form1.hdr_type.options[sel].value;

  ruleList[index].server = document.form1.server.value;	
  ruleList[index].dn = document.form1.dn.value;	
  ruleList[index].realm = document.form1.realm.value;	
  ruleList[index].uid_filter = document.form1.uid_filter.value;	
  ruleList[index].attr_name = document.form1.attr_name.value;	
  ruleList[index].attr_val = document.form1.attr_val.value;
  ruleList[index].redirect_url = document.form1.redirect_url.value;
  ruleList[index].bind_dn = document.form1.bind_dn.value;
  ruleList[index].bind_pwd = document.form1.bind_pwd.value;

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

  for (i=0; i < document.form1.pd_type.length; i++) {
    if (document.form1.pd_type.options[i].value == ruleList[index].pd_type)
       document.form1.pd_type.selectedIndex = i;
  }

  document.form1.pd_val.value = ruleList[index].pd_val;
  document.form1.time.value = ruleList[index].time;
  document.form1.src_ip.value = ruleList[index].src_ip;
  document.form1.prefix.value = ruleList[index].prefix;
  document.form1.suffix.value = ruleList[index].suffix;
  document.form1.port.value = ruleList[index].port;

  for (i=0; i < document.form1.method.length; i++) {
    if (document.form1.method.options[i].value == ruleList[index].method)
       document.form1.method.selectedIndex = i;
  }

  for (i=0; i < document.form1.scheme.length; i++) {
    if (document.form1.scheme.options[i].value == ruleList[index].scheme)
       document.form1.scheme.selectedIndex = i;
  }

  for (i=0; i < document.form1.hdr_type.length; i++) {
    if (document.form1.hdr_type.options[i].value == ruleList[index].hdr_type)
       document.form1.hdr_type.selectedIndex = i;
  }

  document.form1.server.value = ruleList[index].server;
  document.form1.dn.value = ruleList[index].dn;
  document.form1.realm.value = ruleList[index].realm;
  document.form1.uid_filter.value = ruleList[index].uid_filter;
  document.form1.attr_name.value = ruleList[index].attr_name;
  document.form1.attr_val.value = ruleList[index].attr_val;
  document.form1.redirect_url.value = ruleList[index].redirect_url;
  document.form1.bind_dn.value = ruleList[index].bind_dn;
  document.form1.bind_pwd.value = ruleList[index].bind_pwd;

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
  document.form1.rule_type.value = "allow";
  document.form1.pd_type.value = "dest_domain";
  document.form1.pd_val.value = "";
  document.form1.time.value = "";
  document.form1.src_ip.value = "";
  document.form1.prefix.value = "";
  document.form1.suffix.value = "";
  document.form1.port.value = "";
  document.form1.method.value = "";
  document.form1.scheme.value = "";
  document.form1.hdr_type.value = "";
  document.form1.server.value=""; 
  document.form1.dn.value=""; 
  document.form1.realm.value=""; 
  document.form1.uid_filter.value=""; 
  document.form1.attr_name.value=""; 
  document.form1.attr_val.value=""; 
  document.form1.redirect_url.value="";
  document.form1.bind_dn.value="";
  document.form1.bind_pwd.value="";
  document.form1.mixt.value=""; 

  document.form1.list1.selectedIndex = -1; 	
}

// 
// form validation - put detailed alert messages in this function
//
function validInput()
{
  var rule_index = document.form1.rule_type.selectedIndex;
  var pd_index = document.form1.pd_type.selectedIndex;
  var scheme_index = document.form1.scheme.selectedIndex;
  var mixt_index = document.form1.mixt.selectedIndex;

  if (document.form1.bind_pwd.value != "") {
	alert("To add a new rule with a password specified, use 'Apply Password'"); 
	return false;
  }

  if (rule_index < 0 || pd_index < 0) {
	alert("'Rule Type' and 'Primary Dest Type' must be specified");
	return false;
  }

  if (document.form1.rule_type.options[rule_index].value == "" || 
      document.form1.pd_type.options[pd_index].value == "" || 
      document.form1.pd_val.value == ""){
    alert("'Rule Type' and 'Primary Destination' must be specified");
    return false;
  }
	
  if (document.form1.rule_type.options[rule_index].value == "keep_hdr" ||
      document.form1.rule_type.options[rule_index].value == "strip_hdr"){
	if (document.form1.hdr_type.value == "") {
	   alert("'Header Type' must be specified for keep_hdr and strip_hdr rules"); 
	   return false;	
	}
  }

  if (document.form1.rule_type.options[rule_index].value != "keep_hdr" && 
      document.form1.rule_type.options[rule_index].value != "strip_hdr"){
	if (document.form1.hdr_type.value != "") {
	   alert("Only specify 'Header Type' for keep_hdr or strip_hdr rules"); 
	   return false;	
	}
  }

   // check validity of ldap rule 
   if (document.form1.rule_type.options[rule_index].value == "ldap"){
	if (document.form1.server.value != "" || 
            document.form1.dn.value != "" ||
            document.form1.uid_filter.value != "") {
		if (document.form1.server.value == "" ||
	    	    document.form1.dn.value == "" ||
                    document.form1.uid_filter.value == "") {
	               alert("'Server Name', 'Distinguished Name', and 'UID Filter' must be specified"); 
	               return false;
	        }
	}
	if (document.form1.attr_name.value != "" ||
	    document.form1.attr_val.value != "" ||
	    document.form1.realm.value != "" ||
	    document.form1.redirect_url.value != "" || 
            document.form1.bind_dn.value != ""||
            document.form1.bind_pwd.value != "" ) {
		if (document.form1.server.value == "" ||
	    	    document.form1.dn.value == "" ||
                    document.form1.uid_filter.value == "") {
	               alert("'Server Name', 'Distinguished Name', and 'UID Filter' must be specified if any ldap options are specified"); 
	               return false;
	        }	
	}
  } 	

   // check validity of ntlm or ntlm group auth rule
   if (document.form1.rule_type.options[rule_index].value == "ntlm"){
	if (document.form1.server.value != "" || 
            document.form1.dn.value != "" ||
            document.form1.uid_filter.value != "") {
		// THIS IS A NTLM GROUP AUTH RULE!!!
		if (document.form1.server.value == "" ||
	    	    document.form1.dn.value == "" ||
                    document.form1.uid_filter.value == "") {
	               alert("'Server Name', 'Distinguished Name', and 'UID Filter' must be specified for ntlm group authorization rules"); 
	               return false;
	        }
                if (document.form1.attr_name.value == "" ||
	            document.form1.attr_val.value == "" ||
                    document.form1.bind_dn.value == "" ||
                    document.form1.bind_pwd.value == "") {
		        alert("'Attribute Name', 'Attribute Value', 'Bind DN', and 'Bind Password' must be specified for ntlm group authorization rules"); 
		  	return false;	
                }
	} // otherwise just an ntlm rule
  }

   if (document.form1.rule_type.options[rule_index].value == "radius"){
     if (document.form1.hdr_type.value != "" ||
         document.form1.server.value != "" ||
         document.form1.dn.value != "" ||
         document.form1.uid_filter.value != "" ||
         document.form1.attr_name.value != "" ||
	 document.form1.attr_val.value != ""  ||
         document.form1.bind_dn.value != "" ||
         document.form1.bind_pwd.value != "" ) {
	        alert("Only the 'Realm' or 'Redirect URL' can be specified for radius rules"); 
	        return false;	
     }
  }
  
  if (document.form1.rule_type.options[rule_index].value != "ldap" &&
      document.form1.rule_type.options[rule_index].value != "ntlm"){
	if (document.form1.server.value != "" ||
	    document.form1.dn.value != "" ||
	    document.form1.uid_filter.value != "" ||
            document.form1.attr_name.value != "" ||
	    document.form1.attr_val.value != "" ||
            document.form1.bind_dn.value != "" ||
            document.form1.bind_pwd.value != "") {
	  alert("At least one of the authentication/authorization options specified can only be used for ntlm group authorization or ldap rules"); 
          return false;	
	}
  }

  if (document.form1.rule_type.options[rule_index].value != "ldap" &&
      document.form1.rule_type.options[rule_index].value != "ntlm" &&
      document.form1.rule_type.options[rule_index].value != "radius"){
	if (document.form1.realm.value != "" || document.form1.redirect_url.value != "") {
	   alert("'Realm' and 'Redirect URL' can only be specified for ldap, ntlm, or radius rules"); 
	   return false;	
        }
  }


  if (document.form1.bind_dn.value != "" || document.form1.bind_pwd.value != "") {
	if (document.form1.bind_dn.value == "" ||
            document.form1.bind_pwd.value == "") {
		alert("'Bind DN' and 'Bind Password' must be specified together");
		return false; 
	}    
  }

  if (document.form1.mixt.options[mixt_index].value != "") {
	if (document.form1.scheme.options[scheme_index].value != "rtsp") {
	   alert("'MIXT Scheme' can only be specified with 'rtsp' scheme");
	   return false;
        }
  }

  return true;   	
}

