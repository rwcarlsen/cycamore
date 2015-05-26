#include "curve_inst.h"
#include "sim_init.h"

using cyclus::SimInit;
using cyclus::SqliteDb;
using cyclus::SqlStatement;
using cyclus::SqliteBack;

namespace cycamore {

CurveInst::CurveInst(cyclus::Context* ctx) : cyclus::Institution(ctx) {}

CurveInst::~CurveInst() {}

void CurveInst::Tock() {
  int t = context()->time();
  int deploy_t = t + 1;
  if (OnDeploy(t + 2)) {
    context()->Snapshot();
  } else if (!OnDeploy(t + 1)) {
    return;
  }

  bool done = false;
  int iter = 0;
  std::vector<int> nbuild;
  for (int i = 0; i < proto_priority.size(); i++) {
    nbuild.push_back(0);
  }
  double growth_cap = 0;
  int lookdur = deploy_t + lookahead;
  while (!done) {
    SqliteBack memback(":memory:");
    SimInit si;
    si.Restart(context()->db(), context()->sim_id(), t, lookdur);
    si.recorder()->RegisterBackend(&memback);
    for (int i = 0; i < nbuild.size(); i++) {
      // if we use 'this' as parent, then this will be owned and deallocated
      // by multiple conterxts - BAD
      // TODO: figure out a way to not have to use NULL for parent here.
      si.context()->SchedBuild(NULL, proto_priority[i], deploy_t);
    }
    // TODO: figure out way to only run to current time + lookahead
    si.timer()->RunSim();

    iter++;
    // build min req capacity 
    if (iter == 1) {
      growth_cap = WantCap(deploy_t) - PowerAt(memback.db(), deploy_t);
      growth_cap = std::max(0.0, growth_cap);
      nbuild[0] += static_cast<int>(growth_cap / proto_cap[0]);
      continue;
    }

    done = true;
    for (int look = 0; look < lookahead; look++) {
      int t_check = deploy_t + look;
      double power = PowerAt(memback.db(), t_check);
      double shortfall = WantCap(t_check) - power;
      if (shortfall > 1e-6) {
        for (int i = 0; i < nbuild.size() - 1; i++) {
          // make an adjustment to a lower priority facility type if required.
          if (nbuild[i] > 0) {
            done = false;
            nbuild[i] -= 1;
            int nadd = static_cast<int>(proto_cap[i] / proto_cap[i+1]);
            nbuild[i+1] += nadd;
            if (PowerOf(nbuild) < growth_cap) {
              nbuild[i+1] += 1;
            }
            break;
          }
        }
      }
    }
  }

  for (int i = 0; i < nbuild.size(); i++) {
    context()->SchedBuild(this, proto_priority[i], deploy_t);
  }
}

int CurveInst::TimeOf(int period) {
  return enter_time() + 1 + period * deploy_period;
};

int CurveInst::PeriodOf(int t) {
  return (t - enter_time() - 1) / deploy_period;
};

bool CurveInst::WantCap(int t) {
  return curve[PeriodOf(t)];
}

bool CurveInst::OnDeploy(int t) {
  return (double)(t - enter_time() - 1) / (double)deploy_period ==
         PeriodOf(t);
};

double CurveInst::PowerOf(std::vector<int> nbuild) {
  double totcap = 0;
  for (int i = 0; i < nbuild.size(); i++) {
    totcap += nbuild[i] * proto_cap[i];
  }
  return totcap;
}

double CurveInst::PowerAt(SqliteDb& db, int t) {
  SqlStatement::Ptr stmt = db.Prepare("SELECT SUM(Value) FROM TimeSeries" + captable + " WHERE Time = ?");
  stmt->BindInt(1, t);
  stmt->Step();
  return stmt->GetDouble(0);
}

void CurveInst::EnterNotify() {
  cyclus::Institution::EnterNotify();
  int dur = context()->sim_info().duration;
  int nperiods = (dur - 2) / deploy_period + 1;
  if (curve.size() != nperiods) {
    std::stringstream ss;
    ss << "prototype '" << prototype() << "' has " << curve.size()
       << " curve capacity vals, expected " << nperiods;
    throw cyclus::ValidationError(ss.str());
  }
}

extern "C" cyclus::Agent* ConstructCurveInst(cyclus::Context* ctx) {
  return new CurveInst(ctx);
}

}  // namespace cycamore
