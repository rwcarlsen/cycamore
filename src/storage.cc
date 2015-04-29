#include "storage.h"

using cyclus::Material;
using cyclus::Composition;
using cyclus::toolkit::ResBuf;
using cyclus::toolkit::MatVec;
using cyclus::KeyError;
using cyclus::ValueError;
using cyclus::Request;
using cyclus::CompMap;

namespace cycamore {

Storage::Storage(cyclus::Context* ctx)
    : cyclus::Facility(ctx), readyqty_(0) {
  cyclus::Warn<cyclus::EXPERIMENTAL_WARNING>("the Storage archetype "
                                             "is experimental");
}

cyclus::Inventories Storage::SnapshotInv() {
  #pragma cyclus impl snapshotinv cycamore::Storage

  std::map<int, Material::Ptr>::iterator it;
  std::vector<cyclus::Resource::Ptr> res;
  for (it = ready_.begin(); it != ready_.end(); ++it) {
    res.push_back(it->second);
  }
  invs["readybuf"] = res;

  return invs;
}

void Storage::InitInv(cyclus::Inventories& inv) {
  #pragma cyclus impl initinv cycamore::Storage

  std::vector<cyclus::Resource::Ptr> res = inv["readybuf"];
  for (int i = 0; i < res.size(); i++) {
    Material::Ptr m = boost::dynamic_pointer_cast<Material>(res[i]);
    ready_[m->obj_id()] = m;
    readyqty_ += m->quantity();
  }
}

void Storage::ReadyPush(std::vector<Material::Ptr> mats) {
  for (int i = 0; i < mats.size(); i++) {
    Material::Ptr m = mats[i];
    ready_[m->obj_id()] = m;
    readyqty_ += m->quantity();
  }
}

Material::Ptr Storage::ReadyPop(int obj_id) {
  Material::Ptr m = ready_[obj_id];
  ready_.erase(obj_id);
  readyqty_ -= m->quantity();
  return m;
}

void Storage::Tick() {
  int t = context()->time();
  if (waiting.count() == 0 || time_npop.count(t) == 0) {
    return;
  }

  int npop = time_npop[t];
  ReadyPush(waiting.PopN(npop));
}

std::set<cyclus::RequestPortfolio<Material>::Ptr>
Storage::GetMatlRequests() {
  using cyclus::RequestPortfolio;
  std::set<RequestPortfolio<Material>::Ptr> ports;

  double space = invsize - readyqty_ + waiting.quantity();
  if (space < cyclus::eps()) {
    return ports;
  }

  RequestPortfolio<Material>::Ptr port(new RequestPortfolio<Material>());

  Material::Ptr m = cyclus::NewBlankMaterial(space);
  if (!inrecipe.empty()) {
    Composition::Ptr c = context()->GetRecipe(inrecipe);
    m = Material::CreateUntracked(space, c);
  }

  bool exclusive = false;
  double pref = 0;
  port->AddRequest(m, this, incommod, pref, exclusive);
  ports.insert(port);
  return ports;
}

void Storage::GetMatlTrades(
    const std::vector< cyclus::Trade<Material> >& trades,
    std::vector<std::pair<cyclus::Trade<Material>,
    Material::Ptr> >& responses) {
  using cyclus::Trade;

  for (int i = 0; i < trades.size(); i++) {
    Material::Ptr m;
    int id = trades[i].bid->offer()->obj_id();
    if (ready_.count(id) > 0) {
      m = ReadyPop(id);
    } else {
      // pop from front - smaller obj_id's are older
      m = ReadyPop(ready_.begin()->first);
    }
    responses.push_back(std::make_pair(trades[i], m));
  }
}

void Storage::AcceptMatlTrades(
    const std::vector< std::pair<cyclus::Trade<Material>,
    Material::Ptr> >& responses) {

  std::vector< std::pair<cyclus::Trade<cyclus::Material>,
                         cyclus::Material::Ptr> >::const_iterator trade;

  int t = context()->time() + wait_time;
  for (trade = responses.begin(); trade != responses.end(); ++trade) {
    waiting.Push(trade->second);
    if (time_npop.count(t) == 0) {
      time_npop[t] = 0;
    }
    time_npop[t]++;
  }
}

std::set<cyclus::BidPortfolio<Material>::Ptr>
Storage::GetMatlBids(cyclus::CommodMap<Material>::type&
                          commod_requests) {
  using cyclus::BidPortfolio;

  bool exclusive = true;
  std::set<BidPortfolio<Material>::Ptr> ports;

  std::vector<Request<Material>*>& reqs = commod_requests[outcommod];
  if (reqs.size() == 0 || readyqty_ < cyclus::eps() || ready_.size() == 0) {
    return ports;
  }


  MatVec mats;
  mats.reserve(ready_.size());
  std::map<int, Material::Ptr>::iterator it;
  for (it = ready_.begin(); it != ready_.end(); ++it) {
    mats.push_back(it->second);
  }

  BidPortfolio<Material>::Ptr port(new BidPortfolio<Material>());

  // TODO: This really needs mutual bids functionality to guarantee that each
  // material object is only matched once.
  for (int j = 0; j < reqs.size(); j++) {
    Request<Material>* req = reqs[j];
    double tot_bid = 0;
    for (int k = 0; k < mats.size(); k++) {
      Material::Ptr m = mats[k];
      tot_bid += m->quantity();
      port->AddBid(req, m, this, exclusive);
      if (tot_bid >= req->target()->quantity()) {
        break;
      }
    }
  }

  cyclus::CapacityConstraint<Material> cc(readyqty_);
  port->AddConstraint(cc);
  ports.insert(port);
  return ports;
}

extern "C" cyclus::Agent* ConstructStorage(cyclus::Context* ctx) {
  return new Storage(ctx);
}

} // namespace cycamore

