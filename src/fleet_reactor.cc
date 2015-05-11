#include "reactor.h"

using cyclus::Material;
using cyclus::Composition;
using cyclus::toolkit::ResBuf;
using cyclus::toolkit::MatVec;
using cyclus::KeyError;
using cyclus::ValueError;
using cyclus::Request;

namespace cycamore {

FleetReactor::FleetReactor(cyclus::Context* ctx)
    : cyclus::Facility(ctx),
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
  Deploy(1);
}

void FleetReactor::Decommission() {
  Retire(1);
  cyclus::Facility::Decommission();
}

void FleetReactor::Tick() {
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
  Material::Ptr m;

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
    m = Material::CreateUntracked(assem_size, recipe);
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

  bool gotmats = false;
  std::map<std::string, MatVec> all_mats;

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

    MatVec mats = all_mats[commod];
    if (mats.size() == 0) {
      continue;
    }

    BidPortfolio<Material>::Ptr port(new BidPortfolio<Material>());

    for (int j = 0; j < reqs.size(); j++) {
      Request<Material>* req = reqs[j];
      double tot_bid = 0;
      for (int k = 0; k < mats.size(); k++) {
        Material::Ptr m = mats[k];
        tot_bid += m->quantity();
        port->AddBid(req, m, this, true);
        if (tot_bid >= req->target()->quantity()) {
          break;
        }
      }
    }

    double tot_qty = 0;
    for (int j = 0; j < mats.size(); j++) {
      tot_qty += mats[j]->quantity();
    }
    cyclus::CapacityConstraint<Material> cc(tot_qty);
    port->AddConstraint(cc);
    ports.insert(port);
  }

  return ports;
}

void FleetReactor::Tock() {
  double power = power_cap * fleet_size * core.quantity() / core.capacity();
  cyclus::toolkit::RecordTimeSeries<cyclus::toolkit::POWER>(this, power);
}

std::map<std::string, MatVec> FleetReactor::PeekSpent() {
  std::map<std::string, MatVec> mapped;
  MatVec mats = spent.PopN(spent.count());
  spent.Push(mats);
  for (int i = 0; i < mats.size(); i++) {
    std::string commod = fuel_outcommod(mats[i]);
    mapped[commod].push_back(mats[i]);
  }
  return mapped;
}

void FleetReactor::Discharge() {
  double qty_discharge = (core.quantity() / core.capacity()) * batch_size / cycle_time;
  qty_discharge = std::min(qty_discharge, core.quantity());

  Material::Ptr m = core.Pop(qty_discharge);
  m->Transmute(context()->GetRecipe(fuel_outrecipe(old[i])));
  spent.Push(m);
}

void FleetReactor::Retire(double number_of_fleet) {
  double qty_discharge = number_of_fleet * core_size;
  qty_discharge = std::min(qty_discharge, core.quantity());

  Material::Ptr m = core.Pop(qty_discharge);
  m->Transmute(context()->GetRecipe(fuel_outrecipe(old[i])));
  spent.Push(m);

  core.capacity(core.capacity() - qty_discharge);
  fleet_size -= number;
}

void FleetReactor::Deploy(double number_of_fleet) {
  double qty_add = number_of_fleet * core_size;
  core.capacity(core.capacity() + qty_add);
  fleet_size += number;
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
