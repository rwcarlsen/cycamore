#include "curve_inst.h"

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
  while (!done) {
    SqliteBack memback(":memory:");
    SimInit si;
    si.Restart(context()->db(), context()->sim_id(), t);
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
      double need_cap = WantCap(deploy_t) - PowerAt(deploy_t);
      need_cap = std::max(0.0, need_cap);
      nbuild[0] += static_cast<int>(need_cap / proto_cap[0]);
      continue;
    }

    done = true;
    for (int look = 0; look < lookahead; look++) {
      int t_check = deploy_t + look;
      double power = PowerAt(memback.db(), t_check);
      if (power < WantCap(t_check)) {
        for (int i = 0; i < nbuild.size(); i++) {
          if (nbuild[i] > 0) {
            nbuild[i] -= 1;
            double addcap = WantCap(deploy_t) - PowerOf(nbuild);
            int nadd = static_cast<int>(addcap / proto_cap[i+1]);
            nbuild[i+1] += nadd;
            break;
          }
        }
        done = false;
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
  stmt->BindInt(t);
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
