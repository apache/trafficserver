#pragma once

#include "P_EventSystem.h"

#include <list>

class QUICFrameGenerator;

class QUICEventDriver
{
public:
  // QUICFrameGenerator should reenable write when necessary.
  virtual void reenable(QUICFrameGenerator *generator) = 0;
};

class QUICEventDriverImpl : public QUICEventDriver, public Continuation
{
public:
  explicit QUICEventDriverImpl(Continuation &c) : Continuation(nullptr), _parent(c) {}

  // QUICFrameGenerator should reenable write when necessary.
  void
  reenable(QUICFrameGenerator *generator) override
  {
    this->_generators.push_back(generator);
    if (this->_event == nullptr) {
      this->_event = this_ethread()->schedule_imm(this);
    }
  }

  int
  handleEvent(int event, void *data)
  {
    ink_assert(this->_event == data);
    this->_event = nullptr;
    this->_parent.handleEvent(event, data);
  }

  std::list<QUICFrameGenerator *>::iterator
  begin()
  {
    return this->_generators.begin();
  }
  std::list<QUICFrameGenerator *>::iterator
  end()
  {
    return this->_generators.end();
  }

  QUICFrameGenerator *
  dequeue()
  {
    if (this->_generators.empty()) {
      return nullptr;
    }

    auto generator = this->_generators.front();
    this->_generators.pop_front();
    return generator;
  }

  QUICFrameGenerator *
  front()
  {
    return this->_generators.front();
  }

private:
  std::list<QUICFrameGenerator *> _generators;

  Continuation &_parent;
  Event *_event = nullptr;
};
