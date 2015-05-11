#include "fleet_reactor.h"

using cyclus::Material;
using cyclus::Composition;
using cyclus::toolkit::ResBuf;
using cyclus::toolkit::MatVec;
using cyclus::KeyError;
using cyclus::ValueError;
using cyclus::Request;

namespace cycamore {

std::map<std::string, FleetReactor*> FleetReactor::masters_;

FleetReactor::FleetReactor(cyclus::Context* ctx)
    : cyclus::Facility(ctx),
      am_master_(false),
      core_size(0),
      batch_size(0),
      fleet_size(0),
      cycle_time(0),
      power_cap(0),
      power_name("power") {
  cyclus::Warn<cyclus::EXPERIMENTAL_WARNING>(
      "the FleetReactor archetype "
      "is experimental");
}

#pragma cyclus def clone cycamore::FleetReactor

#pragma cyclus def schema cycamore::FleetReactor

#pragma cyclus def annotations cycamore::FleetReactor

#pragma cyclus def infiletodb cycamore::FleetReactor

#pragma cyclus def snapshot cycamore::FleetReactor

#pragma cyclus def snapshotinv cycamore::FleetReactor

#pragma cyclus def initinv cycamore::FleetReactor

void FleetReactor::InitFrom(FleetReactor* m) {
  #pragma cyclus impl initfromcopy cycamore::FleetReactor
  cyclus::toolkit::CommodityProducer::Copy(m);
}

void FleetReactor::InitFrom(cyclus::QueryableBackend* b) {
  #pragma cyclus impl initfromdb cycamore::FleetReactor

  namespace tk = cyclus::toolkit;
  tk::CommodityProducer::Add(tk::Commodity(power_name),
                             tk::CommodInfo(power_cap, power_cap));
}

void FleetReactor::EnterNotify() {
  cyclus::Facility::EnterNotify();

  // force fleet to load/identify master before anything else starts - this is
  // important because if initting from a db, a non-master instance might see
  // that master_ == NULL and set itself as and thik it is (incorrectly) the
  // master - at least until the proper master sets itself finally.
  am_master();

  // If the user ommitted fuel_prefs, we set it to zeros for each fuel
  // type.  Without this segfaults could occur - yuck.
  if (fuel_prefs.size() == 0) {
    for (int i = 0; i < fuel_outcommods.size(); i++) {
      fuel_prefs.push_back(0);
    }
  }

  // input consistency checking:
  int n = recipe_change_times.size();
  std::stringstream ss;
  if (recipe_change_commods.size() != n) {
    ss << "prototype '" << prototype() << "' has "
       << recipe_change_commods.size()
       << " recipe_change_commods vals, expected " << n << "\n";
  }
  if (recipe_change_in.size() != n) {
    ss << "prototype '" << prototype() << "' has " << recipe_change_in.size()
       << " recipe_change_in vals, expected " << n << "\n";
  }
  if (recipe_change_out.size() != n) {
    ss << "prototype '" << prototype() << "' has " << recipe_change_out.size()
       << " recipe_change_out vals, expected " << n << "\n";
  }

  n = pref_change_times.size();
  if (pref_change_commods.size() != n) {
    ss << "prototype '" << prototype() << "' has " << pref_change_commods.size()
       << " pref_change_commods vals, expected " << n << "\n";
  }
  if (pref_change_values.size() != n) {
    ss << "prototype '" << prototype() << "' has " << pref_change_values.size()
       << " pref_change_values vals, expected " << n << "\n";
  }

  if (ss.str().size() > 0) {
    throw cyclus::ValueError(ss.str());
  }
}

void FleetReactor::Build(cyclus::Agent* parent) {
  cyclus::Facility::Build(parent);
  master()->Deploy(1);
}

void FleetReactor::Decommission() {
  master()->Retire(1);
  cyclus::Facility::Decommission();
}

void FleetReactor::Tick() {
  if (!am_master()) {
    return;
  }

  Discharge();

  int t = context()->time();

  // update preferences
  for (int i = 0; i < pref_change_times.size(); i++) {
    int change_t = pref_change_times[i];
    if (t != change_t) {
      continue;
    }

    std::string incommod = pref_change_commods[i];
    for (int j = 0; j < fuel_incommods.size(); j++) {
      if (fuel_incommods[j] == incommod) {
        fuel_prefs[j] = pref_change_values[i];
        break;
      }
    }
  }

  // update recipes
  for (int i = 0; i < recipe_change_times.size(); i++) {
    int change_t = recipe_change_times[i];
    if (t != change_t) {
      continue;
    }

    std::string incommod = recipe_change_commods[i];
    for (int j = 0; j < fuel_incommods.size(); j++) {
      if (fuel_incommods[j] == incommod) {
        fuel_inrecipes[j] = recipe_change_in[i];
        fuel_outrecipes[j] = recipe_change_out[i];
        break;
      }
    }
  }
}

std::set<cyclus::RequestPortfolio<Material>::Ptr> FleetReactor::GetMatlRequests() {
  using cyclus::RequestPortfolio;

  std::set<RequestPortfolio<Material>::Ptr> ports;

  if (!am_master()) {
    return ports;
  } else if (exit_time() != -1 && context()->time() >= exit_time()) {
    return ports;
  }

  double qty_order = core.space();
  if (qty_order < cyclus::eps()) {
    return ports;
  }

  RequestPortfolio<Material>::Ptr port(new RequestPortfolio<Material>());
  std::vector<Request<Material>*> mreqs;
  for (int j = 0; j < fuel_incommods.size(); j++) {
    std::string commod = fuel_incommods[j];
    double pref = fuel_prefs[j];
    Composition::Ptr recipe = context()->GetRecipe(fuel_inrecipes[j]);
    Material::Ptr m = Material::CreateUntracked(qty_order, recipe);
    bool exclusive = false;
    Request<Material>* r = port->AddRequest(m, this, commod, pref, exclusive);
    mreqs.push_back(r);
  }
  port->AddMutualReqs(mreqs);
  ports.insert(port);

  return ports;
}

void FleetReactor::GetMatlTrades(
    const std::vector<cyclus::Trade<Material> >& trades,
    std::vector<std::pair<cyclus::Trade<Material>, Material::Ptr> >&
        responses) {
  using cyclus::Trade;
  if (!am_master()) {
    throw ValueError("FleetReactor: non-master instance cannot supply material");
  }

  std::map<std::string, Material::Ptr> mats = PopSpent();
  for (int i = 0; i < trades.size(); i++) {
    std::string commod = trades[i].request->commodity();
    double amt = trades[i].amt;
    Material::Ptr m = mats[commod];
    if (amt >= m->quantity()) {
      responses.push_back(std::make_pair(trades[i], m));
      res_indexes.erase(m->obj_id());
    } else {
      responses.push_back(std::make_pair(trades[i], m->ExtractQty(amt)));
    }
  }
  PushSpent(mats);  // return leftovers back to spent buffer
}

void FleetReactor::AcceptMatlTrades(const std::vector<
    std::pair<cyclus::Trade<Material>, Material::Ptr> >& responses) {
  std::vector<std::pair<cyclus::Trade<cyclus::Material>,
                        cyclus::Material::Ptr> >::const_iterator trade;

  if (!am_master()) {
    throw ValueError("FleetReactor: non-master instance cannot accept material");
  }

  for (trade = responses.begin(); trade != responses.end(); ++trade) {
    std::string commod = trade->first.request->commodity();
    Material::Ptr m = trade->second;
    index_res(m, commod);
    core.Push(m);
  }
}

std::set<cyclus::BidPortfolio<Material>::Ptr> FleetReactor::GetMatlBids(
    cyclus::CommodMap<Material>::type& commod_requests) {
  using cyclus::BidPortfolio;

  std::set<BidPortfolio<Material>::Ptr> ports;
  if (!am_master()) {
    return ports;
  }

  bool gotmats = false;
  std::map<std::string, Material::Ptr> all_mats;

  if (uniq_outcommods_.empty()) {
    for (int i = 0; i < fuel_outcommods.size(); i++) {
      uniq_outcommods_.insert(fuel_outcommods[i]);
    }
  }

  std::set<std::string>::iterator it;
  for (it = uniq_outcommods_.begin(); it != uniq_outcommods_.end(); ++it) {
    std::string commod = *it;
    std::vector<Request<Material>*>& reqs = commod_requests[commod];
    if (reqs.size() == 0) {
      continue;
    } else if (!gotmats) {
      all_mats = PeekSpent();
    }

    if (all_mats.count(commod) == 0 || all_mats[commod]->quantity() < cyclus::eps()) {
      continue;
    }

    Material::Ptr m = all_mats[commod];
    BidPortfolio<Material>::Ptr port(new BidPortfolio<Material>());
    for (int j = 0; j < reqs.size(); j++) {
      Request<Material>* req = reqs[j];
      bool exclusive = false;
      port->AddBid(req, m, this, exclusive);
    }

    cyclus::CapacityConstraint<Material> cc(m->quantity());
    port->AddConstraint(cc);
    ports.insert(port);
  }

  return ports;
}

void FleetReactor::Tock() {
  if (!am_master()) {
    return;
  }
  double power = power_cap * fleet_size * core.quantity() / core.capacity();
  cyclus::toolkit::RecordTimeSeries<cyclus::toolkit::POWER>(this, power);
}

void FleetReactor::Discharge(double qty) {

  double qty_discharge = qty;
  if (qty < 0) {
    qty_discharge = (core.quantity() / core.capacity()) * batch_size / cycle_time * fleet_size;
    std::cout << prototype() << " is discharging default qty for " << fleet_size << " reactors\n";
  }
  std::cout << prototype() << " discharge qty is " << qty_discharge << "\n";

  qty_discharge = std::min(qty_discharge, core.quantity());
  if (qty_discharge < cyclus::eps()) {
    return;
  }

  double togo = qty_discharge; 

  MatVec mv = core.PopN(core.count());
  std::list<Material::Ptr> mats;
  for (int i = 0; i < mv.size(); i++) {
    mats.push_back(mv[i]);
  }

  while (togo >= cyclus::eps() && core.quantity() > cyclus::eps()) {
    Material::Ptr m = mats.front();
    Composition::Ptr c = context()->GetRecipe(fuel_outrecipe(m));

    if (togo >= m->quantity()) {
      m->Transmute(c);
      spent.Push(m);
    } else {
      Material::Ptr partial = m->ExtractQty(togo);
      partial->Transmute(c);
      spent.Push(partial);
      core.Push(m);
    }
    mats.pop_front();
  }
}

bool FleetReactor::CheckDecommissionCondition() {
  // never decommission the master
  return !am_master() || core.count() + spent.count() == 0;
}

void FleetReactor::Retire(double number_of_fleet) {
  double cap_lower = number_of_fleet * core_size;
  std::cout << "retireing " << cap_lower << " core size\n";
  std::cout << "spot0a core.qty=" << core.quantity() << "\n";
  Discharge(cap_lower);
  std::cout << "spot0b core.qty=" << core.quantity() << "\n";

  fleet_size = std::max(0.0, fleet_size - number_of_fleet);
  std::cout << "spot1 core.qty=" << core.quantity() << "\n";
  std::cout << "spot1 setting core.cap=" << fleet_size * core_size << "\n";
  core.capacity(fleet_size * core_size);

  // Being retired means this instance is being deallocated and can no longer
  // act as master for future instances of this prototype.
  if (am_master()) {
    masters_.erase(prototype());
  }
}

void FleetReactor::Deploy(double number_of_fleet) {
  std::cout << prototype() << " is deploying " << number_of_fleet << " reactors\n";
  fleet_size += number_of_fleet;
  core.capacity(core_size * fleet_size);
}

std::string FleetReactor::fuel_incommod(Material::Ptr m) {
  int i = res_indexes[m->obj_id()];
  if (i >= fuel_incommods.size()) {
    throw KeyError("cycamore::FleetReactor - no incommod for material object");
  }
  return fuel_incommods[i];
}

std::string FleetReactor::fuel_outcommod(Material::Ptr m) {
  int i = res_indexes[m->obj_id()];
  if (i >= fuel_outcommods.size()) {
    throw KeyError("cycamore::FleetReactor - no outcommod for material object");
  }
  return fuel_outcommods[i];
}

std::string FleetReactor::fuel_inrecipe(Material::Ptr m) {
  int i = res_indexes[m->obj_id()];
  if (i >= fuel_inrecipes.size()) {
    throw KeyError("cycamore::FleetReactor - no inrecipe for material object");
  }
  return fuel_inrecipes[i];
}

std::string FleetReactor::fuel_outrecipe(Material::Ptr m) {
  int i = res_indexes[m->obj_id()];
  if (i >= fuel_outrecipes.size()) {
    throw KeyError("cycamore::FleetReactor - no outrecipe for material object");
  }
  return fuel_outrecipes[i];
}

double FleetReactor::fuel_pref(Material::Ptr m) {
  int i = res_indexes[m->obj_id()];
  if (i >= fuel_prefs.size()) {
    return 0;
  }
  return fuel_prefs[i];
}

void FleetReactor::index_res(cyclus::Resource::Ptr m, std::string incommod) {
  for (int i = 0; i < fuel_incommods.size(); i++) {
    if (fuel_incommods[i] == incommod) {
      res_indexes[m->obj_id()] = i;
      return;
    }
  }
  throw ValueError(
      "cycamore::FleetReactor - received unsupported incommod material");
}

std::map<std::string, Material::Ptr> FleetReactor::PeekSpent() {
  std::map<std::string, Material::Ptr> mats = PopSpent();
  PushSpent(mats);
  return mats;
}

std::map<std::string, Material::Ptr> FleetReactor::PopSpent() {
  MatVec mats = spent.PopN(spent.count());
  std::map<std::string, Material::Ptr> mapped;
  for (int i = 0; i < mats.size(); i++) {
    std::string commod = fuel_outcommod(mats[i]);
    if (mapped.count(commod) == 0) {
      mapped[commod] = mats[i];
    } else {
      mapped[commod]->Absorb(mats[i]);
    }
  }

  return mapped;
}

void FleetReactor::PushSpent(std::map<std::string, Material::Ptr> leftover) {
  std::map<std::string, Material::Ptr>::iterator it;
  for (it = leftover.begin(); it != leftover.end(); ++it) {
    spent.Push(it->second);
  }
}

extern "C" cyclus::Agent* ConstructFleetReactor(cyclus::Context* ctx) {
  return new FleetReactor(ctx);
}

}  // namespace cycamore
