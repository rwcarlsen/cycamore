#ifndef CYCAMORE_SRC_CURVE_INST_H_
#define CYCAMORE_SRC_CURVE_INST_H_

#include <utility>
#include <set>
#include <map>

#include "cyclus.h"

namespace cycamore {

typedef std::map<int, std::vector<std::string> > BuildSched;

class CurveInst : public cyclus::Institution {
 public:
  CurveInst(cyclus::Context* ctx);

  virtual ~CurveInst();

  #pragma cyclus

  virtual void EnterNotify();

 protected:
  int TimeOf(int period);

  #pragma cyclus var { \
  }
  std::vector<double> curve;

  #pragma cyclus var { \
  }
  int deploy_period;

  #pragma cyclus var { \
  }
  int lookahead;

  // name of timeseries table to get caps from (e.g. Power, etc.)
  std::string captable;

  #pragma cyclus var { \
  }
  std::vector<std::string> proto_priority;
  #pragma cyclus var { \
  }
  std::vector<std::string> proto_cap;
  #pragma cyclus var { \
  }
  std::vector<int> proto_avail;
};

}  // namespace cycamore

#endif  // CYCAMORE_SRC_CURVE_INST_H_
