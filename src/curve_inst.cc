#include "curve_inst.h"

namespace cycamore {

CurveInst::CurveInst(cyclus::Context* ctx) : cyclus::Institution(ctx) {}

CurveInst::~CurveInst() {}

int Curve::TimeOf(int period) {
  int dur = context()->sim_info().duration;
  int nperiods = (dur - 2) / build_period + 1;
  return context()->time() + 1 + period * build_period;
};

void CurveInst::Build(cyclus::Agent* parent) {
  cyclus::Institution::Build(parent);

  int dur = context()->sim_info().duration;
  int nperiods = (dur - 2) / build_period + 1;
  std::vector<double> totcap;
  totcap.resize(dur);

  std::map<std::string, std::vector<double> > totcapproto;
  std::map<std::string, std::pair<double, std::vector<double> > >::iterator it;
  for (it = protos.begin(); it != protos.end(); ++it) {
    std::vector<double> tmp;
    tmp.resize(dur);
    totcapproto[it->first] = tmp;
  }

  // handle initial builds
  std::set<std::string> pprotos;
  for (int i = 0; i < initial_builds.size(); i++) {
    std::string proto = initial_builds[i].first;
    int n_build = initial_builds[i].second.first;
    int life = initial_builds[i].second.second;

    std::stringstream ss;
    ss << proto;

    // generate prototypes with custom lifetimes
    cyclus::Agent* a = context()->CreateAgent<Agent>(proto);
    if (a->lifetime() != life) {
      a->lifetime(life);

      if (lifetimes[i] == -1) {
        ss << "_life_forever";
      } else {
        ss << "_life_" << life;
      }
      proto = ss.str();
      if (pprotos.count(proto) == 0) {
        pprotos.insert(proto);
        context()->AddPrototype(proto, a);
      }
    }

    // schedule initial builds
    int built_time = TimeOf(0);
    for (int t = built_time; t < built_time + life; t++) {
      double cap = protos[proto].first;
      totcap[t] += n_build * cap;
      totcapproto[proto][t] += n_build * cap;
    }
    for (int j = 0; j < n_build; j++) {
      context()->SchedBuild(this, proto, build_time);
    }
  }


  // calculate deployment schedule
  std::map<std::string, std::pair<double, std::vector<double> > >::iterator it;
  for (it = protos.begin(); it != protos.end(); ++it) {
    std::string proto = it->first;
    double cap = it->second.first;
    std::vector<double> fracs = it->second.second;
    cyclus::Agent* a = context()->CreateAgent<Agent>(proto);
    int life = a->lifetime();

    for (int i = 0; i < fracs.size(); i++) {
      int build_time = TimeOf(i);
      double frac = fracs[i];
      double curr_cap = totcap[build_time] / ;
      double want_cap = curve[i] * frac;
      double add_cap = want_cap - curr_cap;
      int nbuild = floor(add_cap / cap + 0.5) * ;

      for (int t = build_time; t < build_time + life; t++) {
        totcap[t] += cap * n_build;
      }
    }
  }
}

void CurveInst::EnterNotify() {
  cyclus::Institution::EnterNotify();
  int dur = context()->sim_info().duration;
  int nperiods = (dur - 2) / build_period + 1;
  if (curve.size() != nperiods) {
    std::stringstream ss;
    ss << "prototype '" << prototype() << "' has " << curve.size()
       << " curve capacity vals, expected " << nperiods;
    throw cyclus::ValidationError(ss.str());
  }

  std::map<std::string, std::pair<double, std::vector<double> > >::iterator it;
  for (it = protos.begin(); it != protos.end(); ++it) {
    if (it->second.second.size() != nperiods) {
      std::stringstream ss;
      ss << "prototype '" << prototype() << "' has " << it->second.second.size()
         << " capacity fraction vals, expected " << nperiods;
      throw cyclus::ValidationError(ss.str());
    }
  }
}

extern "C" cyclus::Agent* ConstructCurveInst(cyclus::Context* ctx) {
  return new CurveInst(ctx);
}

}  // namespace cycamore
