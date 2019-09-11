#pragma once

class QUICEventDriver
{
public:
  // QUICFrameGenerator should reenable write when necessary.
  virtual void reenable_write()                          = 0;
  virtual bool is_event_pending() const                  = 0;
  virtual void set_write_event_pending(bool pend = true) = 0;
};
