/////////////////////////////////////////////////////////////////////
// splitdns.config form and rule contains following:
//                              (Input Type Names)
//    Primary Dest Type   	pd_type
//    Primary Dest Value	pd_value	
//    DNS server names 		dns_server
//    Domain Name 		def_domain
//    Domain Search List 	search_list
// ** NOTE: the input type names must match those created in 
//          writeSplitDnsConfigForm
/////////////////////////////////////////////////////////////////////

// 
// creates a new Rule object; initializes with parameters passed in
//
function Rule(pd_type, pd_val, dns_server, def_domain, search_list) { 
 this.pd_type = pd_type;
 this.pd_val = pd_val;
 this.dns_server = dns_server;
 this.def_domain = def_domain;
 this.search_list = search_list;
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
  var dns_server = document.form1.dns_server.value;
  var def_domain = document.form1.def_domain.value;
  var search_list = document.form1.search_list.value;
 
  var rule = new Rule(pd_type, pd_val, dns_server, def_domain, search_list); 

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
   
  if (rule.dns_server != "") text += delim + "DNS Server IP(s)" + eq + rule.dns_server; 
  if (rule.def_domain != "") text += delim + "Default Domain Name" + eq + rule.def_domain;
  if (rule.search_list != "") text += delim + "Domain Search List" + eq + rule.search_list;
  
  return text;
}

//
// A Rule object also has a hidden format which will be used to help convert
// it into an Ele when user hits "Apply"
// 
function hiddenFormat(rule)
{
  var text = rule.pd_type + "^" + rule.pd_val + "^" + rule.dns_server + "^" + rule.def_domain + "^" + rule.search_list + "^"; 
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
  ruleList[index].dns_server = document.form1.dns_server.value;
  ruleList[index].def_domain = document.form1.def_domain.value;
  ruleList[index].search_list = document.form1.search_list.value;
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

  //document.form1.pd_type.value = ruleList[index].pd_type;
  for (i=0; i < document.form1.pd_type.length; i++) {
    if (document.form1.pd_type.options[i].value == ruleList[index].pd_type)
       document.form1.pd_type.selectedIndex = i;
  }

  document.form1.pd_val.value = ruleList[index].pd_val;
  document.form1.dns_server.value = ruleList[index].dns_server;
  document.form1.def_domain.value = ruleList[index].def_domain;
  document.form1.search_list.value = ruleList[index].search_list;
}

// 
// clears all the fields in the form
// 
function clearForm() 
{
  document.form1.pd_type.value = "dest_domain";
  document.form1.pd_val.value = "";
  document.form1.dns_server.value = "";
  document.form1.def_domain.value = "";
  document.form1.search_list.value = "";

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
      document.form1.dns_server.value == ""){
    alert("Need to specify 'Primary Dest Type', 'Primary Dest Value', and 'DNS Server IP'");
    return false;
  }
	
  return true;   	
}
