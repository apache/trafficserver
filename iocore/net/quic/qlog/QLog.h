

#pragma once

#include <fstream>
#include <yaml-cpp/yaml.h>

#include "QLogEvent.h"

namespace QLog
{
class Trace
{
public:
  enum class VantagePointType : uint8_t {
    client,
    server,
    network,
    unknown,
  };

  struct VantagePoint {
    std::string name;
    VantagePointType type;
    VantagePointType flow = VantagePointType::unknown;
  };

  Trace(std::string odcid, std::string title = "", std::string desc = "") : _reference_time(Thread::get_hrtime()) {}

  Trace(const VantagePoint &vp, std::string odcid, std::string title = "", std::string desc = "") : Trace(odcid, title, desc)
  {
    set_vantage_point(vp);
  }

  static const char *
  vantage_point_type_name(VantagePointType ty)
  {
    switch (ty) {
    case VantagePointType::client:
      return "client";
    case VantagePointType::server:
      return "server";
    case VantagePointType::network:
      return "network";
    case VantagePointType::unknown:
      return "unknown";
    default:
      return nullptr;
    }
  }

  void
  set_vantage_point(const VantagePoint vp)
  {
    this->_vp = vp;
  }

  Trace &
  push_event(QLogEventUPtr e)
  {
    this->_events.push_back(std::move(e));
    return *this;
  }

  void encode(YAML::Node &node);

private:
  int64_t _reference_time = Thread::get_hrtime();
  std::string _odcid;
  std::string _title;
  std::string _desc;

  VantagePoint _vp;

  std::vector<QLogEventUPtr> _events;
};

class QLog
{
public:
  static constexpr char QLOG_VERSION[] = "draft-01";
  // FIXME configurable
  static constexpr char FILENAME[] = "ats.qlog";
  QLog(std::string title = "", std::string desc = "", std::string ver = QLOG_VERSION) : _title(title), _desc(desc), _ver(ver) {}

  Trace &
  new_trace(Trace::VantagePoint vp, std::string odcid, std::string title = "", std::string desc = "")
  {
    this->_traces.push_back(std::make_unique<Trace>(vp, odcid, title, desc));
    return *this->_traces.back().get();
  }

  Trace &
  new_trace(std::string odcid, std::string title = "", std::string desc = "")
  {
    this->_traces.push_back(std::make_unique<Trace>(odcid, title, desc));
    return *this->_traces.back().get();
  }

  Trace &
  last_trace()
  {
    if (this->_traces.empty()) {
      throw std::invalid_argument("traces is empty");
    }

    return *this->_traces.back().get();
  }

  void dump(std::string filename = FILENAME);

private:
  std::string _title;
  std::string _desc;
  std::string _ver;
  std::vector<std::unique_ptr<Trace>> _traces;
};

} // namespace QLog
