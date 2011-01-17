/////////////////////////////////////////////////////////////////////
// icp.config form and rule contains following:
//    ICP Peer Hostname            hostname
//    ICP Peer IP                  host_ip
//    Peer Type (select)           peer_type
//    TCP Proxy Port               proxy_port
//    UDP ICP Port                 icp_port
//    Multicast on/off             mc_state
//    Multicast IP                 mc_ip
//    Multicast TTL                mc_ttl
// ** NOTE: the input type names must match those created in 
//          writeIcpConfigForm
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
function Rule(hostname, host_ip, peer_type, proxy_port, icp_port, mc_state, mc_ip, mc_ttl) {
 this.hostname = hostname;
 this.host_ip = host_ip;
 this.peer_type = peer_type;
 this.proxy_port = proxy_port;
 this.icp_port = icp_port;
 this.mc_state = mc_state;
 this.mc_ip = mc_ip;
 this.mc_ttl = mc_ttl;
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
  var host_ip= document.form1.host_ip.value;

  index = document.form1.peer_type.selectedIndex; 
  var peer_type = document.form1.peer_type.options[index].value;

  var proxy_port = document.form1.proxy_port.value;
  var icp_port = document.form1.icp_port.value;

  index = document.form1.mc_state.selectedIndex; 
  var mc_state = document.form1.mc_state.options[index].value;

  var mc_ip = document.form1.mc_ip.value;
  
  index = document.form1.mc_ttl.selectedIndex; 
  var mc_ttl = document.form1.mc_ttl.options[index].value;	

  var rule = new Rule(hostname, host_ip, peer_type, proxy_port, icp_port, mc_state, mc_ip, mc_ttl);

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

  if (rule.hostname == "") {
	text = "Peer IP" + eq + rule.host_ip;
  } else { 
	text = "Peer Hostname" + eq + rule.hostname;
	if(rule.host_ip != "") text += delim + "Peer IP" + eq + rule.host_ip;
  }

  text += delim + "Peer Type" + eq + rule.peer_type + delim + "Proxy Port" + eq + rule.proxy_port + delim + "ICP Port" + eq + rule.icp_port + delim + "Multlicast" + eq + rule.mc_state;
   
  if (rule.mc_ip != "") text += delim + "Multicast IP" + eq + rule.mc_ip;
  if (rule.mc_ttl != "") text += delim + "Multicast TTL" + eq + rule.mc_ttl;
  	
  return text;
}

//
// A Rule object also has a hidden format which will be used to help convert
// it into an Ele when user hits "Apply"
// 
function hiddenFormat(rule)
{
  var text = rule.hostname + "^" + rule.host_ip + "^" + rule.peer_type + "^" + rule.proxy_port + "^" + rule.icp_port + "^" + rule.mc_state + "^" + rule.mc_ip + "^" + rule.mc_ttl + "^"; 
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
  ruleList[index].host_ip = document.form1.host_ip.value;

  sel = document.form1.peer_type.selectedIndex; 
  ruleList[index].peer_type =  document.form1.peer_type.options[sel].value;

  ruleList[index].proxy_port = document.form1.proxy_port.value;
  ruleList[index].icp_port = document.form1.icp_port.value;

  sel = document.form1.mc_state.selectedIndex; 
  ruleList[index].mc_state =  document.form1.mc_state.options[sel].value;

  ruleList[index].mc_ip = document.form1.mc_ip.value;
 
  sel = document.form1.mc_ttl.selectedIndex; 
  ruleList[index].mc_ttl =  document.form1.mc_ttl.options[sel].value;
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

  for (i=0; i < document.form1.peer_type.length; i++) {
     if (document.form1.peer_type.options[i].value == ruleList[index].peer_type)
       document.form1.peer_type.selectedIndex = i;
  }

  for (i=0; i < document.form1.mc_state.length; i++) {
     if (document.form1.mc_state.options[i].value == ruleList[index].mc_state)
       document.form1.mc_state.selectedIndex = i;
  }

  for (i=0; i < document.form1.mc_ttl.length; i++) {
    if (document.form1.mc_ttl.options[i].value == ruleList[index].mc_ttl)
       document.form1.mc_ttl.selectedIndex = i;
  }

  document.form1.hostname.value = ruleList[index].hostname;
  document.form1.host_ip.value = ruleList[index].host_ip;	
  document.form1.proxy_port.value = ruleList[index].proxy_port;
  document.form1.icp_port.value = ruleList[index].icp_port;
  document.form1.mc_ip.value = ruleList[index].mc_ip;	
}

// 
// clears all the fields in the form
// 
function clearForm() 
{
  document.form1.hostname.value = "";
  document.form1.host_ip.value = "";
  document.form1.peer_type.value = "parent";
  document.form1.proxy_port.value = "";
  document.form1.icp_port.value = "";
  document.form1.mc_state.value = "off";
  document.form1.mc_ip.value = "";
  document.form1.mc_ttl.value = "single subnet";

  document.form1.list1.selectedIndex = -1; 
}

// 
// form validation - put detailed alert messages in this function
//
function validInput()
{
  var cache_index = document.form1.peer_type.selectedIndex;
  var mc_index = document.form1.mc_state.selectedIndex;
  var ttl_index = document.form1.mc_ttl.selectedIndex;

  if (cache_index < 0 || mc_index < 0) {
	alert("Need to specify a 'Peer Type' and if Multicast is on/off.");
	return false;
  }

  if ((document.form1.hostname.value == "" && document.form1.host_ip.value == "") ||
      document.form1.proxy_port.value == "" ||
      document.form1.icp_port.value == "" ||
      document.form1.mc_state.options[mc_index].value == "") {
        alert("Need to specify the 'Peer Hostname' and/or 'Peer IP', 'Proxy Port', 'Peer Type', 'ICP Port', and the Multicast on/off.");
	return false;
  } 

  if (document.form1.mc_state.options[mc_index].value == "on") {
	if (document.form1.mc_ip.value == "0.0.0.0" ||
	    document.form1.mc_ip.value == "" ||
	    document.form1.mc_ttl.options[ttl_index].value == "") {
		alert("Multicast is on. Need to specify Multicast IP and TTL.");
		return false;
	}
  } 
  else {
	if (document.form1.mc_ip.value != "0.0.0.0" &&
	    document.form1.mc_ip.value != "") {
		alert("Multicast is off. Cannot specify Multicast IP.");
		return false;
	}
  }

  return true;   	
}
