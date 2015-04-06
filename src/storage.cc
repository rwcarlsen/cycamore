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
    : cyclus::Facility(ctx) {
  cyclus::Warn<cyclus::EXPERIMENTAL_WARNING>("the Storage archetype "
                                             "is experimental");
}

void Storage::Tick() {
  int t = context()->time();
  if (waiting.count() == 0 || time_npop.count(t) == 0) {
    return;
  }

  int npop = time_npop[t];
  ready.Push(waiting.PopN(npop));
}

std::set<cyclus::RequestPortfolio<Material>::Ptr>
Storage::GetMatlRequests() {
  using cyclus::RequestPortfolio;
  std::set<RequestPortfolio<Material>::Ptr> ports;

  double space = invsize - ready.quantity() + waiting.quantity();
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


  std::map<int, cyclus::Trade<Material> > tradesbyid;
  for (int i = 0; i < trades.size(); i++) {
    // use the state_id because we want errors to occur if the material's
    // state has changed.
    tradesbyid[trades[i].bid->offer()->state_id()] = trades[i];
  }

  MatVec mats = ready.PopN(ready.count());
  for (int i = 0; i < mats.size(); i++) {
    int id = mats[i]->state_id();
    if (tradesbyid.count(id) > 0) {
      responses.push_back(std::make_pair(tradesbyid[id], mats[i]));
      tradesbyid.erase(id);
    } else {
      ready.Push(mats[i]);
    }
  }

  // TODO: Remove the loop below when mutual bids functionality becomes
  // available and implemented.
  std::map<int, cyclus::Trade<Material> >::iterator it;
  for (it = tradesbyid.begin(); it != tradesbyid.end(); ++it) {
    Material::Ptr m = ready.Pop();
    if (std::abs(m->quantity() - it->second.amt) > cyclus::eps()) {
      throw ValueError("prototype " + prototype() + " had the same material object matched to multiple requesters");
    }
    responses.push_back(std::make_pair(it->second, ready.Pop()));
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
  if (reqs.size() == 0 || ready.quantity() < cyclus::eps()) {
    return ports;
  }

  MatVec mats = ready.PopN(ready.count());
  ready.Push(mats);

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

  cyclus::CapacityConstraint<Material> cc(ready.quantity());
  port->AddConstraint(cc);
  ports.insert(port);
  return ports;
}

extern "C" cyclus::Agent* ConstructStorage(cyclus::Context* ctx) {
  return new Storage(ctx);
}

} // namespace cycamore

