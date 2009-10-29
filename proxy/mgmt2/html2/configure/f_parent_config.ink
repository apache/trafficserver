/////////////////////////////////////////////////////////////////////
// parent.config form and rule contains following:
//    Primary Dest Type   	pd_type
//    Primary Dest Value	pd_value	
//    Time                      time
//    Source IP                 src_ip
//    Prefix			prefix
//    Suffix			suffix
//    Port			port
//    Method			method
//    Scheme			scheme
//    Media-IXT tag		mixt
//    Parent List               parents
//    Round Robin Type          round_robin
//    Go Direct?                direct
// ** NOTE: the input type names must match those created in 
//          writeParentConfigForm
/////////////////////////////////////////////////////////////////////

// 
// creates a new Rule object; initializes with parameters passed in
//
function Rule(pd_type, pd_val, time, src_ip, prefix, suffix, port, method, scheme, mixt, parents, round_robin, direct) {
 this.pd_type = pd_type;
 this.pd_val = pd_val;
 this.time = time;
 this.src_ip = src_ip;
 this.prefix = prefix;
 this.suffix = suffix;
 this.port = port;
 this.method = method;
 this.scheme = scheme;
 this.mixt = mixt; 
 this.parents = parents;
 this.round_robin = round_robin;
 this.direct = direct;
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

  index = document.form1.mixt.selectedIndex;
  var mixt = document.form1.mixt.options[index].value;
 
  var parents = document.form1.parents.value;
  
  index = document.form1.round_robin.selectedIndex; 
  var round_robin = document.form1.round_robin.options[index].value;

  var direct = document.form1.direct.value;

  var rule = new Rule(pd_type, pd_val, time, src_ip, prefix, suffix, port, method, scheme, mixt, parents, round_robin, direct);

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

  if (rule.parents != "") text += delim + "Parents" + eq + rule.parents;
  if (rule.round_robin != "") text += delim + "Round Robin" + eq + rule.round_robin;
  if (rule.direct != "") text += delim + "Go Direct" + eq + rule.direct;   
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

  var text = rule.pd_type + delim + rule.pd_val + delim + rule.time + delim + rule.src_ip + delim + rule.prefix + delim + rule.suffix + delim + rule.port + delim + rule.method + delim + rule.scheme +  delim + rule.mixt + delim + rule.parents + delim + rule.round_robin + delim + rule.direct + delim; 
  return text; 
}

// 
// This function updates the selected Rule object with the values 
// entered on the form. 
// 
function updateRule(index) 
{
  var sel;

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

  sel = document.form1.mixt.selectedIndex; 
  ruleList[index].mixt = document.form1.mixt.options[sel].value;

  ruleList[index].parents = document.form1.parents.value;

  sel = document.form1.round_robin.selectedIndex; 
  ruleList[index].round_robin = document.form1.round_robin.options[sel].value;

  ruleList[index].direct = document.form1.direct.value;
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

  for (i=0; i < document.form1.mixt.length; i++) {
    if (document.form1.mixt.options[i].value == ruleList[index].mixt)
       document.form1.mixt.selectedIndex = i;
  }

  document.form1.parents.value = ruleList[index].parents;

  for (i=0; i < document.form1.round_robin.length; i++) {
    if (document.form1.round_robin.options[i].value == ruleList[index].round_robin)
       document.form1.round_robin.selectedIndex = i;
  }

  document.form1.direct.value = ruleList[index].direct;
}

// 
// clears all the fields in the form
// 
function clearForm() 
{
  document.form1.pd_type.value = "dest_domain";
  document.form1.pd_val.value = "";
  document.form1.time.value = "";
  document.form1.src_ip.value = "";
  document.form1.prefix.value = "";
  document.form1.suffix.value = "";
  document.form1.port.value = "";
  document.form1.method.value = "";
  document.form1.scheme.value = "";
  document.form1.mixt.value=""; 
  document.form1.parents.value= "";
  document.form1.round_robin.value= "";
  document.form1.direct.value= "false";

  document.form1.list1.selectedIndex = -1; 	
}

// 
// form validation - put detailed alert messages in this function
//
function validInput()
{
  var pd_index = document.form1.pd_type.selectedIndex;
  var scheme_index = document.form1.scheme.selectedIndex;
  var mixt_index = document.form1.mixt.selectedIndex;

  if (pd_index < 0) {
	alert("Need to specify a 'Primary Dest Type'");
	return false;
  }

  if (document.form1.parents.value == "" && document.form1.direct.value == "") {
	alert("Need to specify either 'Parents' or 'Go Direct'");
	return false;
  } 

  if (document.form1.mixt.options[mixt_index].value != "") {
	if (document.form1.scheme.options[scheme_index].value != "rtsp") {
	   alert("'MIXT Scheme' can only be specified with 'rtsp' scheme.");
	   return false;
        }
  }
	
  return true;   	
}
