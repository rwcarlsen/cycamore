#include "curve_inst.h"
#include "sim_init.h"

using cyclus::SimInit;
using cyclus::SqliteDb;
using cyclus::SqlStatement;
using cyclus::SqliteBack;

namespace cycamore {

CurveInst::CurveInst(cyclus::Context* ctx) : cyclus::Institution(ctx), rec_((unsigned int)2) {
  if (!am_ghost_) {
    ctx->CloneSim(); // guarantee we get clone for time zero
  }
}

CurveInst::~CurveInst() { }

bool CurveInst::am_ghost_ = false;

void CurveInst::Tock() {
  if (am_ghost_) {
    return;
  }

  int t = context()->time();
  if (OnDeploy(t + 2)) {
    context()->CloneSim();
    return;
  } else if (!OnDeploy(t + 1)) {
    return;
  }

  int deploy_t = t + 1;
  int period = PeriodOf(deploy_t);

  // calculate min req new capacity for growth and to replace retiring reactors
  std::map<int, double> growths // map<period, new_growth_cap>
  if (iter == 1) {
    SqliteBack memback(":memory:");
    RunSim(&memback, deploy_t);
    for (int i = period; i < PeriodOf(deploy_t + lookahead); i++ {
      growths[i] = CalcReqBuilds(memback.db(), i);
    }
  }

  int iter = 0;
  bool done = proto_priority.size() == 1;
  while (!done) {
    iter++;
    // deciding deploy ratios not necessary for only one reactor type
    if (proto_priority.size() == 1) {
      break;
    }

    double shortfall = CalcShortfall(deploy_t);

    // check for production shortfall and adjust deployments if necessary
    done = true;
    for (int look = 0; look < lookahead; look++) {
      int t_check = deploy_t + look;
      double power = PowerAt(memback.db(), t_check);
      double shortfall = WantCap(t_check) - power;
      if (shortfall > 1e-6) {
        done = UpdateNbuild(growth_cap, nbuild);
      }
      if (!done) {
        break;
      }
    }
  }

  for (int i = 0; i < nbuilds[period].size(); i++) {
    for (int k = 0; k < nbuilds[period][i]; k++) {
      context()->SchedBuild(this, proto_priority[i], deploy_t);
    }
  }

  context()->NewDatum("CurveInstIters")
    ->AddVal("Time", context()->time())
    ->AddVal("AgentId", id())
    ->AddVal("NSims", iter)
    ->Record();
}

// calculate integrated shortfall over the entire lookahead window
double CurveInst::CalcShortfall(int deploy_t) {
  SqliteBack memback(":memory:");
  RunSim(&memback, deploy_t);

  double shortfall = 0;
  for (int look = 0; look < lookahead; look++) {
    int t_check = deploy_t + look;
    double power = PowerAt(memback.db(), t_check);
    shortfall += std::max(0.0, WantCap(t_check) - power);
  }
  return shortfall;
}

// returns the new growth capacity built
void CurveInst::CalcReqBuilds(int deploy_t) {
  SqliteBack memback(":memory:");
  RunSim(&memback, deploy_t);

  // newcap tracks how much we've added in total capacity compared to the
  // simulation stored in memback. 
  double newcap = 0;

  for (int i = PeriodOf(deploy_t); i < PeriodOf(deploy_t + lookahead); i++) {
    if (i < nbuilds.size()) {
      // don't recompute already calculated builds from a previous iteration.
      continue;
    }

    int t = TimeOf(i);

    // we assume all new capacity we add in this for loop will be operating
    // for the duration of the lookahead window.  So we need to subtract off
    // the already just added newcap from the shortfall here in order to
    // calculate the correct new capacity to add for this deploy period.
    double growth_cap = WantCap(t) - PowerAt(db, t) - newcap;
    growth_cap = std::max(0.0, growth_cap);

    std::vector<int> nbuild;
    for (int i = 0; i < proto_avail.size(); i++) {
      nbuild.push_back(0);
      if (proto_avail[i] <= t) {
        int nadd = static_cast<int>(ceil(growth_cap / proto_cap[i]));
        nbuild[i] += nadd;
        newcap += nbuild * proto_cap[i];
        break;
      }
    }

    nbuilds[i].push_back(nbuild);
  }
}

void CurveInst::RunSim(SqliteBack* b, const std::vector<int>& nbuild, int deploy_t) {
  am_ghost_ = true;
  if (rec_.dump_count() < 500) {
    rec_.set_dump_count(500);
  }
  cyclus::LogLevel lev = cyclus::Logger::ReportLevel();
  cyclus::Logger::ReportLevel() = cyclus::LEV_ERROR;

  int lookdur = deploy_t + lookahead;

  SimInit si;
  si.Init(&rec_, context()->GetClone(), lookdur);
  rec_.RegisterBackend(b);
  for (int period = PeriodOf(deploy_t); period < PeriodOf(lookdur); period++) {
    std::vector<int> nbuild = nbuilds[period];
    for (int i = 0; i < nbuild.size(); i++) {
      for (int k = 0; k < nbuild[i]; k++) {
        // if we use 'this' as parent, then this will be owned and deallocated
        // by multiple conterxts - BAD
        // TODO: figure out a way to not have to use NULL for parent here.
        si.context()->SchedBuild(NULL, proto_priority[i], deploy_t);
      }
    }
  }
  si.timer()->RunSim();
  rec_.Flush();
  rec_.Close();

  cyclus::Logger::ReportLevel() = lev;
  am_ghost_ = false;
}

bool CurveInst::UpdateNbuild(double growth_cap, std::vector<int>& nbuild) {
  for (int i = 0; i < nbuild.size() - 1; i++) {
    if (nbuild[i] > 0) {
      // make an adjustment to a lower priority facility type
      nbuild[i] -= 1;
      int nadd = static_cast<int>(proto_cap[i] / proto_cap[i + 1]);
      nbuild[i + 1] += nadd;
      if (PowerOf(nbuild) < growth_cap) {
        nbuild[i + 1] += 1;
      }
      return false;
    }
  }
  return true; // no changes made to nbuild, we are done.
}

int CurveInst::TimeOf(int period) {
  return enter_time() + 1 + period * deploy_period;
};

int CurveInst::PeriodOf(int t) {
  return (t - enter_time() - 1) / deploy_period;
};

double CurveInst::WantCap(int t) {
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
  double val = 0;
  try {
    SqlStatement::Ptr stmt = db.Prepare("SELECT SUM(Value) FROM TimeSeries" + captable + " WHERE Time = ?");
    stmt->BindInt(1, t);
    stmt->Step();
    val = stmt->GetDouble(0);
  } catch(cyclus::IOError err) { }
  return val;
}

void CurveInst::EnterNotify() {
  cyclus::Institution::EnterNotify();
  int dur = context()->sim_info().duration;
  int nperiods = (dur - 2) / deploy_period + 1;
  if (curve.size() < nperiods) {
    std::stringstream ss;
    ss << "prototype '" << prototype() << "' has " << curve.size()
       << " curve capacity vals, expected >= " << nperiods;
    throw cyclus::ValidationError(ss.str());
  } else if (proto_priority.size() != proto_cap.size()) {
    std::stringstream ss;
    ss << "prototype '" << prototype() << "' has " << proto_cap.size()
       << " proto_cap vals, expected " << proto_priority.size();
    throw cyclus::ValidationError(ss.str());
  } else if (proto_avail.size() > 0 && proto_priority.size() != proto_avail.size()) {
    std::stringstream ss;
    ss << "prototype '" << prototype() << "' has " << proto_avail.size()
       << " proto_avail vals, expected " << proto_priority.size();
    throw cyclus::ValidationError(ss.str());
  }

  if (proto_avail.size() == 0) {
    for (int i = 0; i < proto_priority.size(); i++) {
      proto_avail.push_back(0);
    }
  }
}

extern "C" cyclus::Agent* ConstructCurveInst(cyclus::Context* ctx) {
  return new CurveInst(ctx);
}

}  // namespace cycamore
