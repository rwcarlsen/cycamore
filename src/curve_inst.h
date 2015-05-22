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

  virtual void Build(cyclus::Agent* parent);

  virtual void EnterNotify();

 protected:
  int TimeOf(int period);

  #pragma cyclus var { \
  }
  std::vector<double> curve;

  #pragma cyclus var { \
  }
  int deploy_period;

  // std::map<prototype, std::pair<capacity, std::vector<frac> > >
  #pragma cyclus var { \
  }
  std::map<std::string, std::pair<double, std::vector<double> > > protos;

  // std::vector<std::pair<proto, std::pair<number, lifetime> > >
  std::vector<std::pair<std::string, std::pair<int, int> > > initial_builds;
};

}  // namespace cycamore

#endif  // CYCAMORE_SRC_CURVE_INST_H_
