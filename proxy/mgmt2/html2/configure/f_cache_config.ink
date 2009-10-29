/////////////////////////////////////////////////////////////////////
// cache.config form and rule contains following:
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
//    Time Period		time_period
//    Media-IXT tag		mixt
// ** NOTE: the input type names must match those created in 
//          writeCacheConfigForm
/////////////////////////////////////////////////////////////////////

// 
// creates a new Rule object; initializes with parameters passed in
//
function Rule(rule_type, pd_type, pd_val, time, src_ip, prefix, suffix, port, method, scheme, time_period, mixt) {
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
 this.time_period = time_period; 
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

  var time_period = document.form1.time_period.value;

  index = document.form1.mixt.selectedIndex;
  var mixt = document.form1.mixt.options[index].value;
 
  var rule = new Rule(rule_type, pd_type, pd_val, time, src_ip, prefix, suffix, port,  method, scheme, time_period, mixt); 

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

	
  if (rule.time_period != "") text += delim + "Time Period" + eq + rule.time_period;   
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

  var text = rule.rule_type + delim + rule.pd_type + delim + rule.pd_val + delim + rule.time + delim + rule.src_ip + delim + rule.prefix + delim + rule.suffix + delim + rule.port + delim + rule.method + delim + rule.scheme + delim + rule.time_period + delim + rule.mixt + delim; 
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

  ruleList[index].time_period = document.form1.time_period.value;

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

  //document.form1.rule_type.value = ruleList[index].rule_type; 
  for (i=0; i < document.form1.rule_type.length; i++) {
     if (document.form1.rule_type.options[i].value == ruleList[index].rule_type)
       document.form1.rule_type.selectedIndex = i;
  }

  //document.form1.pd_type.value = ruleList[index].pd_type;
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
 
 //document.form1.method.value = ruleList[index].method;
  for (i=0; i < document.form1.method.length; i++) {
    if (document.form1.method.options[i].value == ruleList[index].method)
       document.form1.method.selectedIndex = i;
  }

 //document.form1.scheme.value = ruleList[index].scheme;
  for (i=0; i < document.form1.scheme.length; i++) {
    if (document.form1.scheme.options[i].value == ruleList[index].scheme)
       document.form1.scheme.selectedIndex = i;
  }

  document.form1.time_period.value = ruleList[index].time_period;
  //document.form1.mixt.value = ruleList[index].mixt; 
  for (i=0; i < document.form1.mixt.length; i++) {
    if (document.form1.mixt.options[i].value == ruleList[index].mixt)
       document.form1.mixt.selectedIndex = i;
  }
}

// 
// clears all the fields in the form ro set to defaults
// 
function clearForm() 
{
  document.form1.rule_type.value = "never-cache";
  document.form1.pd_type.value = "dest_domain";
  document.form1.pd_val.value = "";
  document.form1.time.value = "";
  document.form1.src_ip.value = "";
  document.form1.prefix.value = "";
  document.form1.suffix.value = "";
  document.form1.port.value = "";
  document.form1.method.value = "";
  document.form1.scheme.value = "";
  document.form1.time_period.value = "";
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

  if (rule_index < 0 || pd_index < 0) {
	alert("Need to specify a 'Rule Type' and 'Primary Dest Type'");
	return false;
  }

  if (document.form1.rule_type.options[rule_index].value == "" || 
      document.form1.pd_type.options[pd_index].value == "" || 
      document.form1.pd_val.value == ""){
    alert("Need to specify a 'Rule Type', 'Primary Dest Type', and 'Primay Dest Value'");
    return false;
  }

  if (document.form1.rule_type.options[rule_index].value == "pin-in-cache" ||
      document.form1.rule_type.options[rule_index].value == "revalidate" ||
      document.form1.rule_type.options[rule_index].value == "ttl-in-cache") {
	if (document.form1.time_period.value == "") {
		alert("Need to specify the 'Time Period'"); 
	 	return false;
	}
  } else {
	if (document.form1.time_period.value != "") {
		alert("time period used only for pin-in-cache, ttl-in-cache or revalidate rules");
		return false;
	}
  }

  if (document.form1.rule_type.options[rule_index].value == "ttl-in-cache"){
	if (document.form1.scheme.value != "" && 
	    document.form1.scheme.value != "http") {
	   alert("ttl-in-cache rules only apply to HTTP scheme"); 
	   return false;	
	}
  }

  if (document.form1.mixt.options[mixt_index].value != "") {
	if (document.form1.scheme.options[scheme_index].value != "rtsp") {
	   alert("'MIXT Scheme' can only be specified with 'rtsp' scheme.");
	   return false;
        }
  }
	
  return true;   	
}
