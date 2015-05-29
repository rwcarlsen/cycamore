#ifndef CYCAMORE_SRC_CURVE_INST_H_
#define CYCAMORE_SRC_CURVE_INST_H_

#include <utility>
#include <set>
#include <map>

#include "cyclus.h"

namespace cycamore {

struct Short {
  int start_period;
  double shortfall;
};

class CurveInst : public cyclus::Institution {
 public:
  CurveInst(cyclus::Context* ctx);

  virtual ~CurveInst();

  #pragma cyclus

  virtual void EnterNotify();

  virtual void Tock();

 private:
  static bool am_ghost_;

  void RunSim(cyclus::SqliteBack* b, int deploy_t);

  bool UpdateNbuild(int deploy_t, Short fall);

  Short CalcShortfall(int deploy_t);

  void CalcReqBuilds(int deploy_t);

  int TimeOf(int period);

  int PeriodOf(int t);

  double WantCap(int t);

  bool OnDeploy(int t);

  double PowerOf(std::vector<int> nbuild);

  double PowerAt(cyclus::SqliteDb& db, int t);

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
  #pragma cyclus var { \
    "default": "Power", \
  }
  std::string captable;

  #pragma cyclus var { \
  }
  std::vector<std::string> proto_priority;
  #pragma cyclus var { \
  }
  std::vector<double> proto_cap;
  #pragma cyclus var { \
    "default": [], \
  }
  std::vector<int> proto_avail;

  #pragma cyclus var { \
    "default": [], \
    "internal": True, \
  }
  std::vector<std::vector<int> > nbuilds;

  #pragma cyclus var { \
    "default": {}, \
    "internal": True, \
  }
  std::map<int, double> growths;

  cyclus::Recorder rec_;
};

}  // namespace cycamore

#endif  // CYCAMORE_SRC_CURVE_INST_H_
