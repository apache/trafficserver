#include "QLog.h"

namespace QLog
{
void
Trace::encode(YAML::Node &node)
{
  node["title"]       = _title;
  node["description"] = _desc;

  // common fields
  {
    YAML::Node cf;
    cf["ODCID"]           = _odcid;
    cf["reference_time"]  = std::to_string(this->_reference_time / HRTIME_MSECOND);
    node["common_fields"] = cf;
  }

  {
    node["event_fields"].push_back("relative_time");
    node["event_fields"].push_back("category");
    node["event_fields"].push_back("event");
    node["event_fields"].push_back("data");

    if (_vp.name != "") {
      node["vantage_point"]["name"] = _vp.name;
    }

    if (vantage_point_type_name(_vp.type)) {
      node["vantage_point"]["type"] = vantage_point_type_name(_vp.type);
    }

    if (vantage_point_type_name(_vp.flow)) {
      node["vantage_point"]["flow"] = vantage_point_type_name(_vp.flow);
    }
  }

  // events
  for (auto &&it : _events) {
    YAML::Node sub(YAML::NodeType::value::Sequence);
    sub.push_back((it->get_time() - this->_reference_time) / HRTIME_MSECOND);
    sub.push_back(it->category());
    sub.push_back(it->event());
    YAML::Node event;
    it->encode(event);
    sub.push_back(event);
    node["events"].push_back(sub);
  }
}

void
QLog::dump(std::string filename)
{
  YAML::Node root;
  root["qlog_version"] = this->_ver;
  root["title"]        = this->_title;
  root["description"]  = this->_desc;
  for (auto &&it : this->_traces) {
    YAML::Node node;
    it->encode(node);
    root["traces"].push_back(node);
  }

  std::ofstream ofs;
  ofs.open(filename, std::ofstream::in | std::ofstream::trunc);

  YAML::Emitter emitter(ofs);
  emitter << YAML::DoubleQuoted << YAML::Flow << root;
  ofs << "\n";
  ofs.close();
}

} // namespace QLog
