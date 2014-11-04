// Implements the BatchReactor class
#include "batch_reactor.h"

#include <sstream>
#include <cmath>

using cyclus::CapacityConstraint;
using cyclus::Material;
using cyclus::RequestPortfolio;
using cyclus::Trade;
using cyclus::Bid;
using cyclus::Request;
using cyclus::PrefMap;
using cyclus::Composition;

namespace kitlus {

#define LG(X) LOG(cyclus::LEV_##X, "kitlus")
#define LGH(X) LOG(cyclus::LEV_##X, "kitlus") << "policy " << name_ << " (agent " << manager()->id() << "): "

BuyPolicy& BuyPolicy::Init(cyclus::toolkit::ResourceBuff* buf, std::string name, double quantize) {
  buf_ = buf;
  quantize_ = quantize;
  name_ = name;
  return *this;
}

BuyPolicy& BuyPolicy::Set(std::string commod, cyclus::Composition::Ptr c, double pref) {
  CommodDetail d;
  d.comp = c;
  d.pref = pref;
  commods_[commod] = d;
  return *this;
}

std::map<cyclus::Material::Ptr, std::string> BuyPolicy::Commods() {
  return rsrc_commod_;
};

std::set<RequestPortfolio<Material>::Ptr>
BuyPolicy::GetMatlRequests() {
  rsrc_commod_.clear();
  std::set<RequestPortfolio<Material>::Ptr> ports;
  double amt = buf_->space();
  if (amt < cyclus::eps()) {
    return ports;
  }

  double quanta = quantize_;
  bool exclusive = true;
  if (quantize_ < 0) {
    exclusive = false;
    quanta = amt;
  }

  LGH(INFO2) << "requesting " << amt << " kg";

  RequestPortfolio<Material>::Ptr port(new RequestPortfolio<Material>());
  std::map<std::string, CommodDetail>::iterator it;

  std::map<int, std::vector<Request<Material>*> > grps;
  for (it = commods_.begin(); it != commods_.end(); ++it) {
    std::string commod = it->first;
    CommodDetail d = it->second;
    for (int i = 0; i < amt / quanta; i++) {
      LG(INFO3) << "  - one " << amt << " kg request of " << commod;
      Material::Ptr m = Material::CreateUntracked(quanta, d.comp);
      grps[i].push_back(port->AddRequest(m, this, commod, exclusive));
    }
  }

  std::map<int, std::vector<Request<Material>*> >::iterator grpit;
  for (grpit = grps.begin(); grpit != grps.end(); ++grpit) {
    port->AddMutualReqs(grpit->second);
  }
  ports.insert(port);

  return ports;
}

void BuyPolicy::AcceptMatlTrades(
  const std::vector< std::pair<Trade<Material>,
  Material::Ptr> >& resps) {
  std::vector< std::pair<Trade<Material>, Material::Ptr> >::const_iterator it;
  rsrc_commod_.clear();
  for (it = resps.begin(); it != resps.end(); ++it) {
    rsrc_commod_[it->second] = it->first.request->commodity();
    LGH(INFO2) << "got " << it->second->quantity() << " kg of "
               << it->first.request->commodity();
    buf_->Push(it->second);
  }
}

void BuyPolicy::AdjustMatlPrefs(PrefMap<Material>::type& prefs) {
  PrefMap<Material>::type::iterator it;
  for (it = prefs.begin(); it != prefs.end(); ++it) {
    Request<Material>* r = it->first;
    double pref = commods_[r->commodity()].pref;
    LGH(INFO4) << "setting prefs for " << r->target()->quantity() << " kg bid for "
               << r->commodity() << " to " << pref;
    std::map<Bid<Material>*, double>::iterator it2;
    std::map<Bid<Material>*, double> bids = it->second;
    for (it2 = bids.begin(); it2 != bids.end(); ++it2) {
      Bid<Material>* b = it2->first;
      prefs[r][b] = pref;
    }
  }
}

} // namespace kitlus
namespace cycamore {

// static members
std::map<BatchReactor::Phase, std::string> BatchReactor::phase_names_ =
    std::map<Phase, std::string>();

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BatchReactor::BatchReactor(cyclus::Context* ctx)
    : cyclus::Facility(ctx),
      process_time_(1),
      preorder_time_(0),
      refuel_time_(0),
      start_time_(-1),
      to_begin_time_(std::numeric_limits<int>::max()),
      n_batches_(1),
      n_load_(1),
      n_reserves_(1),
      batch_size_(1),
      invpolicy_(this),
      corepolicy_(this),
      phase_(INITIAL) {
  cyclus::Warn<cyclus::EXPERIMENTAL_WARNING>("the BatchReactor agent "
                                             "is considered experimental.");
  if (phase_names_.empty()) {
    SetUpPhaseNames_();
  }
  spillover_ = cyclus::NewBlankMaterial(0);
}

void BatchReactor::EnterNotify() {
  cyclus::Facility::EnterNotify();
  invpolicy_.Init(&reserves_, "reserves");//, batch_size_);
  corepolicy_.Init(&core_, "core", batch_size_);
  UpdatePolicy();
  context()->RegisterTrader(&invpolicy_);
  context()->RegisterTrader(&corepolicy_);
}

void BatchReactor::UpdatePolicy() {
  std::set<std::string> commods = crctx_.in_commods();
  std::set<std::string>::iterator it;
  for (it = commods.begin(); it != commods.end(); ++it) {
    std::string commod = *it;
    Composition::Ptr c = context()->GetRecipe(crctx_.in_recipe(commod));
    double pref = commod_prefs_[commod];
    invpolicy_.Set(*it, c, pref);
    corepolicy_.Set(*it, c, pref);
  }
}

void BatchReactor::Decommission() {
  context()->UnregisterTrader(&invpolicy_);
  context()->UnregisterTrader(&corepolicy_);
  cyclus::Facility::Decommission();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BatchReactor::~BatchReactor() {}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
std::string BatchReactor::schema() {
  return
      "  <!-- cyclus::Material In/Out  -->           \n"
      + crctx_.schema() +
      "  <!-- Facility Parameters -->                \n"
      "  <interleave>                                \n"
      "  <element name=\"processtime\">              \n"
      "    <data type=\"nonNegativeInteger\"/>       \n"
      "  </element>                                  \n"
      "  <element name=\"nbatches\">                 \n"
      "    <data type=\"nonNegativeInteger\"/>       \n"
      "  </element>                                  \n"
      "  <element name =\"batchsize\">               \n"
      "    <data type=\"double\"/>                   \n"
      "  </element>                                  \n"
      "  <optional>                                  \n"
      "    <element name =\"refueltime\">            \n"
      "      <data type=\"nonNegativeInteger\"/>     \n"
      "    </element>                                \n"
      "  </optional>                                 \n"
      "  <optional>                                  \n"
      "    <element name =\"orderlookahead\">        \n"
      "      <data type=\"nonNegativeInteger\"/>     \n"
      "    </element>                                \n"
      "  </optional>                                 \n"
      "  <optional>                                  \n"
      "    <element name =\"norder\">                \n"
      "      <data type=\"nonNegativeInteger\"/>     \n"
      "    </element>                                \n"
      "  </optional>                                 \n"
      "  <optional>                                  \n"
      "    <element name =\"nreload\">               \n"
      "      <data type=\"nonNegativeInteger\"/>     \n"
      "    </element>                                \n"
      "  </optional>                                 \n"
      "  <optional>                                  \n"
      "    <element name =\"initial_condition\">     \n"
      "      <optional>                              \n"
      "        <element name =\"reserves\">          \n"
      "         <element name =\"nbatches\">         \n"
      "          <data type=\"nonNegativeInteger\"/> \n"
      "         </element>                           \n"
      "         <element name =\"commodity\">        \n"
      "          <data type=\"string\"/>             \n"
      "         </element>                           \n"
      "         <element name =\"recipe\">           \n"
      "          <data type=\"string\"/>             \n"
      "         </element>                           \n"
      "        </element>                            \n"
      "      </optional>                             \n"
      "      <optional>                              \n"
      "        <element name =\"core\">              \n"
      "        <element name =\"nbatches\">          \n"
      "          <data type=\"nonNegativeInteger\"/> \n"
      "        </element>                            \n"
      "        <element name =\"commodity\">         \n"
      "          <data type=\"string\"/>             \n"
      "        </element>                            \n"
      "        <element name =\"recipe\">            \n"
      "          <data type=\"string\"/>             \n"
      "        </element>                            \n"
      "        </element>                            \n"
      "      </optional>                             \n"
      "      <optional>                              \n"
      "        <element name =\"storage\">           \n"
      "        <element name =\"nbatches\">          \n"
      "          <data type=\"nonNegativeInteger\"/> \n"
      "        </element>                            \n"
      "        <element name =\"commodity\">         \n"
      "          <data type=\"string\"/>             \n"
      "        </element>                            \n"
      "        <element name =\"recipe\">            \n"
      "          <data type=\"string\"/>             \n"
      "        </element>                            \n"
      "        </element>                            \n"
      "      </optional>                             \n"
      "    </element>                                \n"
      "  </optional>                                 \n"
      "                                              \n"
      "  <!-- Recipe Changes  -->                    \n"
      "  <optional>                                  \n"
      "  <oneOrMore>                                 \n"
      "  <element name=\"recipe_change\">            \n"
      "   <element name=\"incommodity\">             \n"
      "     <data type=\"string\"/>                  \n"
      "   </element>                                 \n"
      "   <element name=\"new_recipe\">              \n"
      "     <data type=\"string\"/>                  \n"
      "   </element>                                 \n"
      "   <element name=\"time\">                    \n"
      "     <data type=\"nonNegativeInteger\"/>      \n"
      "   </element>                                 \n"
      "  </element>                                  \n"
      "  </oneOrMore>                                \n"
      "  </optional>                                 \n"
      "  </interleave>                               \n"
      "                                              \n"
      "  <!-- Power Production  -->                  \n"
      "  <element name=\"commodity_production\">     \n"
      "   <element name=\"commodity\">               \n"
      "     <data type=\"string\"/>                  \n"
      "   </element>                                 \n"
      "   <element name=\"capacity\">                \n"
      "     <data type=\"double\"/>                  \n"
      "   </element>                                 \n"
      "   <element name=\"cost\">                    \n"
      "     <data type=\"double\"/>                  \n"
      "   </element>                                 \n"
      "  </element>                                  \n"
      "                                              \n"
      "  <!-- Trade Preferences  -->                 \n"
      "  <optional>                                  \n"
      "  <oneOrMore>                                 \n"
      "  <element name=\"commod_pref\">              \n"
      "   <element name=\"incommodity\">             \n"
      "     <data type=\"string\"/>                  \n"
      "   </element>                                 \n"
      "   <element name=\"preference\">              \n"
      "     <data type=\"double\"/>                  \n"
      "   </element>                                 \n"
      "  </element>                                  \n"
      "  </oneOrMore>                                \n"
      "  </optional>                                 \n"
      "                                              \n"
      "  <!-- Trade Preference Changes  -->          \n"
      "  <optional>                                  \n"
      "  <oneOrMore>                                 \n"
      "  <element name=\"pref_change\">              \n"
      "   <element name=\"incommodity\">             \n"
      "     <data type=\"string\"/>                  \n"
      "   </element>                                 \n"
      "   <element name=\"new_pref\">                \n"
      "     <data type=\"double\"/>                  \n"
      "   </element>                                 \n"
      "   <element name=\"time\">                    \n"
      "     <data type=\"nonNegativeInteger\"/>      \n"
      "   </element>                                 \n"
      "  </element>                                  \n"
      "  </oneOrMore>                                \n"
      "  </optional>                                 \n";
}

void BatchReactor::InitFrom(cyclus::QueryableBackend* b) {
  cyclus::Facility::InitFrom(b);

  crctx_.InitFrom(b);

  // facility info
  cyclus::QueryResult qr = b->Query("Info", NULL);
  process_time_ = qr.GetVal<int>("processtime");
  preorder_time_ = qr.GetVal<int>("preorder_t");
  refuel_time_ = qr.GetVal<int>("refueltime");
  start_time_ = qr.GetVal<int>("starttime");
  to_begin_time_ = qr.GetVal<int>("tobegintime");
  n_batches_ = qr.GetVal<int>("nbatches");
  n_load_ = qr.GetVal<int>("nreload");
  n_reserves_ = qr.GetVal<int>("norder");
  batch_size_ = qr.GetVal<double>("batchsize");
  phase_ = static_cast<Phase>(qr.GetVal<int>("phase"));
  reserves_.set_capacity(batch_size_*n_reserves_);
  core_.set_capacity(batch_size_*n_batches_);

  std::string out_commod = qr.GetVal<std::string>("out_commod");
  cyclus::toolkit::CommodityProducer::Add(out_commod);
  cyclus::toolkit::CommodityProducer::
      SetCapacity(out_commod, qr.GetVal<double>("out_commod_cap"));
  cyclus::toolkit::CommodityProducer::
      SetCost(out_commod, qr.GetVal<double>("out_commod_cap"));

  // initial condition inventories
  std::vector<cyclus::Cond> conds;
  conds.push_back(cyclus::Cond("inventory", "==", std::string("reserves")));
  qr = b->Query("InitialInv", &conds);
  ics_.AddReserves(
    qr.GetVal<int>("nbatches"),
    qr.GetVal<std::string>("recipe"),
    qr.GetVal<std::string>("commod")
    );
  conds[0] = cyclus::Cond("inventory", "==", std::string("core"));
  qr = b->Query("InitialInv", &conds);
  ics_.AddCore(
    qr.GetVal<int>("nbatches"),
    qr.GetVal<std::string>("recipe"),
    qr.GetVal<std::string>("commod")
    );
  conds[0] = cyclus::Cond("inventory", "==", std::string("storage"));
  qr = b->Query("InitialInv", &conds);
  ics_.AddStorage(
    qr.GetVal<int>("nbatches"),
    qr.GetVal<std::string>("recipe"),
    qr.GetVal<std::string>("commod")
    );

  // trade preferences
  try {
    qr.Reset();
    qr = b->Query("CommodPrefs", NULL);
  } catch(std::exception err) {}  // table doesn't exist (okay)
  for (int i = 0; i < qr.rows.size(); ++i) {
    std::string c = qr.GetVal<std::string>("incommodity", i);
    commod_prefs_[c] = qr.GetVal<double>("preference", i);
  }

  // pref changes
  try {
    qr.Reset();
    qr = b->Query("PrefChanges", NULL);
  } catch(std::exception err) {}  // table doesn't exist (okay)
  for (int i = 0; i < qr.rows.size(); ++i) {
    std::string c = qr.GetVal<std::string>("incommodity", i);
    int t = qr.GetVal<int>("time", i);
    double new_pref = qr.GetVal<double>("new_pref", i);
    pref_changes_[t].push_back(std::make_pair(c, new_pref));
  }

  // recipe changes
  try {
    qr.Reset();
    qr = b->Query("RecipeChanges", NULL);
  } catch(std::exception err) {}  // table doesn't exist (okay)
  for (int i = 0; i < qr.rows.size(); ++i) {
    std::string c = qr.GetVal<std::string>("incommodity", i);
    int t = qr.GetVal<int>("time", i);
    std::string new_recipe = qr.GetVal<std::string>("new_recipe", i);
    recipe_changes_[t].push_back(std::make_pair(c, new_recipe));
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BatchReactor::InfileToDb(cyclus::InfileTree* qe, cyclus::DbInit di) {
  cyclus::Facility::InfileToDb(qe, di);
  qe = qe->SubTree("config/*");

  using cyclus::toolkit::Commodity;
  using cyclus::toolkit::CommodityProducer;
  using cyclus::OptionalQuery;
  using cyclus::Query;
  using cyclus::InfileTree;
  using std::string;

  crctx_.InfileToDb(qe, di);

  // facility data
  int processtime = Query<int>(qe, "processtime");
  int nbatches = Query<int>(qe, "nbatches");
  double batchsize = Query<double>(qe, "batchsize");
  int refuel_t = OptionalQuery<int>(qe, "refueltime", refuel_time());
  int preorder_t = OptionalQuery<int>(qe, "orderlookahead", preorder_time());
  int nreload = OptionalQuery<int>(qe, "nreload", n_load());
  int norder = OptionalQuery<int>(qe, "norder", n_reserves());

  InfileTree* commodity = qe->SubTree("commodity_production");
  std::string out_commod = commodity->GetString("commodity");
  double commod_cap = Query<double>(commodity, "capacity");
  double commod_cost = Query<double>(commodity, "cost");

  di.NewDatum("Info")
      ->AddVal("processtime", processtime)
      ->AddVal("nbatches", nbatches)
      ->AddVal("batchsize", batchsize)
      ->AddVal("refueltime", refuel_t)
      ->AddVal("preorder_t", preorder_t)
      ->AddVal("nreload", nreload)
      ->AddVal("norder", norder)
      ->AddVal("starttime", -1)
      ->AddVal("tobegintime", std::numeric_limits<int>::max())
      ->AddVal("phase", static_cast<int>(INITIAL))
      ->AddVal("out_commod", out_commod)
      ->AddVal("out_commod_cap", commod_cap)
      ->AddVal("out_commod_cost", commod_cost)
      ->Record();

  // initial condition inventories
  std::vector<std::string> inv_names;
  inv_names.push_back("reserves");
  inv_names.push_back("core");
  inv_names.push_back("storage");
  for (int i = 0; i < inv_names.size(); ++i) {
    int n = 0;
    std::string recipe;
    std::string commod;
    if (qe->NMatches("initial_condition") > 0) {
      InfileTree* ic = qe->SubTree("initial_condition");
      if (ic->NMatches(inv_names[i]) > 0) {
        InfileTree* reserves = ic->SubTree(inv_names[i]);
        n = Query<int>(reserves, "nbatches");
        recipe = reserves->GetString("recipe");
        commod = reserves->GetString("commodity");
      }
    }
    di.NewDatum("InitialInv")
        ->AddVal("inventory", inv_names[i])
        ->AddVal("nbatches", n)
        ->AddVal("recipe", recipe)
        ->AddVal("commod", commod)
        ->Record();
  }

  // trade preferences
  int nprefs = qe->NMatches("commod_pref");
  std::string c;
  for (int i = 0; i < nprefs; i++) {
    InfileTree* cp = qe->SubTree("commod_pref", i);
    di.NewDatum("CommodPrefs")
        ->AddVal("incommodity", cp->GetString("incommodity"))
        ->AddVal("preference", Query<double>(cp, "preference"))
        ->Record();
  }

  // pref changes
  int nchanges = qe->NMatches("pref_change");
  for (int i = 0; i < nchanges; i++) {
    InfileTree* cp = qe->SubTree("pref_change", i);
    di.NewDatum("PrefChanges")
        ->AddVal("incommodity", cp->GetString("incommodity"))
        ->AddVal("new_pref", Query<double>(cp, "new_pref"))
        ->AddVal("time", Query<int>(cp, "time"))
        ->Record();
  }

  // recipe changes
  nchanges = qe->NMatches("recipe_change");
  for (int i = 0; i < nchanges; i++) {
    InfileTree* cp = qe->SubTree("recipe_change", i);
    di.NewDatum("RecipeChanges")
        ->AddVal("incommodity", cp->GetString("incommodity"))
        ->AddVal("new_recipe", cp->GetString("new_recipe"))
        ->AddVal("time", Query<int>(cp, "time"))
        ->Record();
  }
}

void BatchReactor::Snapshot(cyclus::DbInit di) {
  cyclus::Facility::Snapshot(di);
  crctx_.Snapshot(di);

  std::set<cyclus::toolkit::Commodity, cyclus::toolkit::CommodityCompare>::
      iterator it;
  it = cyclus::toolkit::CommodityProducer::ProducedCommodities().begin();
  std::string out_commod = it->name();
  double cost = cyclus::toolkit::CommodityProducer::Cost(out_commod);
  double cap = cyclus::toolkit::CommodityProducer::Capacity(out_commod);
  di.NewDatum("Info")
      ->AddVal("processtime", process_time_)
      ->AddVal("nbatches", n_batches_)
      ->AddVal("batchsize", batch_size_)
      ->AddVal("refueltime", refuel_time_)
      ->AddVal("preorder_t", preorder_time_)
      ->AddVal("nreload", n_load_)
      ->AddVal("norder", n_reserves_)
      ->AddVal("starttime", start_time_)
      ->AddVal("tobegintime", to_begin_time_)
      ->AddVal("phase", static_cast<int>(phase_))
      ->AddVal("out_commod", out_commod)
      ->AddVal("out_commod_cap", cap)
      ->AddVal("out_commod_cost", cost)
      ->Record();

  // initial condition inventories
  di.NewDatum("InitialInv")
      ->AddVal("inventory", std::string("reserves"))
      ->AddVal("nbatches", ics_.n_reserves)
      ->AddVal("recipe", ics_.reserves_rec)
      ->AddVal("commod", ics_.reserves_commod)
      ->Record();
  di.NewDatum("InitialInv")
      ->AddVal("inventory", std::string("core"))
      ->AddVal("nbatches", ics_.n_core)
      ->AddVal("recipe", ics_.core_rec)
      ->AddVal("commod", ics_.core_commod)
      ->Record();
  di.NewDatum("InitialInv")
      ->AddVal("inventory", std::string("storage"))
      ->AddVal("nbatches", ics_.n_storage)
      ->AddVal("recipe", ics_.storage_rec)
      ->AddVal("commod", ics_.storage_commod)
      ->Record();

  // trade preferences
  std::map<std::string, double>::iterator it2 = commod_prefs_.begin();
  for (; it2 != commod_prefs_.end(); ++it2) {
    di.NewDatum("CommodPrefs")
        ->AddVal("incommodity", it2->first)
        ->AddVal("preference", it2->second)
        ->Record();
  }

  // pref changes
  std::map<int, std::vector< std::pair< std::string, double > > >::iterator it3;
  for (it3 = pref_changes_.begin(); it3 != pref_changes_.end(); ++it3) {
    int t = it3->first;
    for (int i = 0; i < it3->second.size(); ++i) {
      std::string commod = it3->second[i].first;
      double pref = it3->second[i].second;
      di.NewDatum("PrefChanges")
          ->AddVal("incommodity", commod)
          ->AddVal("new_pref", pref)
          ->AddVal("time", t)
          ->Record();
    }
  }

  // recipe changes
  std::map<int, std::vector< std::pair< std::string, std::string > > >::
      iterator it4;
  for (it4 = recipe_changes_.begin(); it4 != recipe_changes_.end(); ++it4) {
    int t = it4->first;
    for (int i = 0; i < it4->second.size(); ++i) {
      std::string commod = it4->second[i].first;
      std::string recipe = it4->second[i].second;
      di.NewDatum("RecipeChanges")
          ->AddVal("incommodity", commod)
          ->AddVal("new_recipe", recipe)
          ->AddVal("time", t)
          ->Record();
    }
  }
}

void BatchReactor::InitInv(cyclus::Inventories& invs) {
  reserves_.PushAll(invs["reserves"]);
  core_.PushAll(invs["core"]);
  spillover_ = cyclus::ResCast<cyclus::Material>(invs["spillover"][0]);

  cyclus::Inventories::iterator it;
  for (it = invs.begin(); it != invs.end(); ++it) {
    std::string name = it->first;
    if (name.find("storage-") == 0) {
      storage_[name].PushAll(it->second);
    }
  }
}

cyclus::Inventories BatchReactor::SnapshotInv() {
  cyclus::Inventories invs;
  invs["reserves"] = reserves_.PopN(reserves_.count());
  reserves_.PushAll(invs["reserves"]);
  invs["core"] = core_.PopN(core_.count());
  core_.PushAll(invs["core"]);
  std::vector<cyclus::Resource::Ptr> v;
  v.push_back(spillover_);
  invs["spillover"] = v;
  std::map<std::string, cyclus::toolkit::ResourceBuff>::iterator it;
  for (it = storage_.begin(); it != storage_.end(); ++it) {
    std::string name = it->first;
    invs["storage-" + name] = it->second.PopN(it->second.count());
    it->second.PushAll(invs["storage-" + name]);
  }
  return invs;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
cyclus::Agent* BatchReactor::Clone() {
  BatchReactor* m = new BatchReactor(context());
  m->InitFrom(this);
  return m;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BatchReactor::InitFrom(BatchReactor* m) {
  Facility::InitFrom(m);

  // in/out
  crctx_ = m->crctx_;

  // facility params
  process_time(m->process_time());
  preorder_time(m->preorder_time());
  refuel_time(m->refuel_time());
  n_batches(m->n_batches());
  n_load(m->n_load());
  n_reserves(m->n_reserves());
  batch_size(m->batch_size());
  reserves_.set_capacity(m->reserves_.capacity());
  core_.set_capacity(m->core_.capacity());

  // commodity production
  cyclus::toolkit::CommodityProducer::Copy(m);

  // ics
  ics(m->ics());

  // trade preferences
  commod_prefs(m->commod_prefs());
  pref_changes_ = m->pref_changes_;

  // recipe changes
  recipe_changes_ = m->recipe_changes_;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
std::string BatchReactor::str() {
  std::stringstream ss;
  ss << cyclus::Facility::str();
  ss << " has facility parameters {" << "\n"
     << "     Process Time = " << process_time() << ",\n"
     << "     Refuel Time = " << refuel_time() << ",\n"
     << "     Preorder Time = " << preorder_time() << ",\n"
     << "     Core Loading = " << n_batches() * batch_size() << ",\n"
     << "     Batches Per Core = " << n_batches() << ",\n"
     << "     Batches Per Load = " << n_load() << ",\n"
     << "     Batches To Reserve = " << n_reserves() << ",\n"
     << "'}";
  return ss.str();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BatchReactor::Build(cyclus::Agent* parent) {
  using cyclus::Material;

  Facility::Build(parent);
  phase(INITIAL);
  std::string rec = crctx_.in_recipe(*crctx_.in_commods().begin());
  spillover_ = Material::Create(this, 0.0, context()->GetRecipe(rec));

  Material::Ptr mat;
  for (int i = 0; i < ics_.n_reserves; ++i) {
    mat = Material::Create(this,
                           batch_size(),
                           context()->GetRecipe(ics_.reserves_rec));
    assert(ics_.reserves_commod != "");
    crctx_.AddRsrc(ics_.reserves_commod, mat);
    std::cout << "spot5\n";
    reserves_.Push(mat);
  }
  for (int i = 0; i < ics_.n_core; ++i) {
    mat = Material::Create(this,
                           batch_size(),
                           context()->GetRecipe(ics_.core_rec));
    assert(ics_.core_commod != "");
    crctx_.AddRsrc(ics_.core_commod, mat);
    std::cout << "spot6\n";
    core_.Push(mat);
  }
  for (int i = 0; i < ics_.n_storage; ++i) {
    mat = Material::Create(this,
                           batch_size(),
                           context()->GetRecipe(ics_.storage_rec));
    assert(ics_.storage_commod != "");
    crctx_.AddRsrc(ics_.storage_commod, mat);
    std::cout << "spot7\n";
    storage_[ics_.storage_commod].Push(mat);
  }

  LOG(cyclus::LEV_DEBUG2, "BReact") << "Batch Reactor entering the simuluation";
  LOG(cyclus::LEV_DEBUG2, "BReact") << str();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BatchReactor::Tick() {
  int time = context()->time();
  LOG(cyclus::LEV_INFO3, "BReact") << prototype() << " is ticking at time "
                                   << time << " {";

  LOG(cyclus::LEV_DEBUG4, "BReact") << "Current facility parameters for "
                                    << prototype()
                                    << " at the beginning of the tick are:";
  LOG(cyclus::LEV_DEBUG4, "BReact") << "    Phase: " << phase_names_[phase_];
  LOG(cyclus::LEV_DEBUG4, "BReact") << "    Start time: " << start_time_;
  LOG(cyclus::LEV_DEBUG4, "BReact") << "    End time: " << end_time();
  LOG(cyclus::LEV_DEBUG4, "BReact") << "    Order time: " << order_time();
  LOG(cyclus::LEV_DEBUG4, "BReact") << "    NReserves: " << reserves_.count();
  LOG(cyclus::LEV_DEBUG4, "BReact") << "    NCore: " << core_.count();
  LOG(cyclus::LEV_DEBUG4, "BReact") << "    NStorage: " << StorageCount();
  LOG(cyclus::LEV_DEBUG4, "BReact") << "    Spillover Qty: "
                                    << spillover_->quantity();

  if (lifetime() != -1 && context()->time() >= enter_time() + lifetime()) {
    int ncore = core_.count();
    LOG(cyclus::LEV_DEBUG1, "BReact") << "lifetime reached, moving out:"
                                      << ncore << " batches.";
    for (int i = 0; i < ncore; ++i) {
      MoveBatchOut_();  // unload
    }
  } else {
    switch (phase()) {
      case WAITING:
        if (n_core() == n_batches() &&
            to_begin_time() <= context()->time()) {
          phase(PROCESS);
        }
        break;

      case INITIAL:
        // special case for a core primed to go
        if (n_core() == n_batches()) {
          phase(PROCESS);
        }
        break;
    }
  }

  // change preferences if its time
  if (pref_changes_.count(time)) {
    std::vector< std::pair< std::string, double> >&
        changes = pref_changes_[time];
    for (int i = 0; i < changes.size(); i++) {
      commod_prefs_[changes[i].first] = changes[i].second;
    }
    UpdatePolicy();
  }

  // change recipes if its time
  if (recipe_changes_.count(time)) {
    std::vector< std::pair< std::string, std::string> >&
        changes = recipe_changes_[time];
    for (int i = 0; i < changes.size(); i++) {
      assert(changes[i].first != "");
      assert(changes[i].second != "");
      crctx_.UpdateInRec(changes[i].first, changes[i].second);
    }
    UpdatePolicy();
  }

  LOG(cyclus::LEV_DEBUG3, "BReact") << "Current facility parameters for "
                                    << prototype()
                                    << " at the end of the tick are:";
  LOG(cyclus::LEV_DEBUG3, "BReact") << "    Phase: " << phase_names_[phase_];
  LOG(cyclus::LEV_DEBUG3, "BReact") << "    Start time: " << start_time_;
  LOG(cyclus::LEV_DEBUG3, "BReact") << "    End time: " << end_time();
  LOG(cyclus::LEV_DEBUG3, "BReact") << "    Order time: " << order_time();
  LOG(cyclus::LEV_DEBUG3, "BReact") << "    NReserves: " << reserves_.count();
  LOG(cyclus::LEV_DEBUG3, "BReact") << "    NCore: " << core_.count();
  LOG(cyclus::LEV_DEBUG3, "BReact") << "    NStorage: " << StorageCount();
  LOG(cyclus::LEV_DEBUG3, "BReact") << "    Spillover Qty: "
                                    << spillover_->quantity();
  LOG(cyclus::LEV_INFO3, "BReact") << "}";
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BatchReactor::Tock() {
  int time = context()->time();
  LOG(cyclus::LEV_INFO3, "BReact") << prototype() << " is tocking {";
  LOG(cyclus::LEV_DEBUG4, "BReact") << "Current facility parameters for "
                                    << prototype()
                                    << " at the beginning of the tock are:";
  LOG(cyclus::LEV_DEBUG4, "BReact") << "    Phase: " << phase_names_[phase_];
  LOG(cyclus::LEV_DEBUG4, "BReact") << "    Start time: " << start_time_;
  LOG(cyclus::LEV_DEBUG4, "BReact") << "    End time: " << end_time();
  LOG(cyclus::LEV_DEBUG4, "BReact") << "    Order time: " << order_time();
  LOG(cyclus::LEV_DEBUG4, "BReact") << "    NReserves: " << reserves_.count();
  LOG(cyclus::LEV_DEBUG4, "BReact") << "    NCore: " << core_.count();
  LOG(cyclus::LEV_DEBUG4, "BReact") << "    NStorage: " << StorageCount();
  LOG(cyclus::LEV_DEBUG4, "BReact") << "    Spillover Qty: "
                                    << spillover_->quantity();

  switch (phase()) {
    case PROCESS:
      if (time == end_time()) {
        for (int i = 0; i < std::min(n_load(), core_.count()); ++i) {
          MoveBatchOut_();  // unload
        }
        Refuel_();  // reload
        phase(WAITING);
      }
      break;
    default:
      Refuel_();  // always try to reload if possible
      break;
  }

  // update commod recipe context with mats received via inv policies
  std::map<Material::Ptr, std::string> recv = invpolicy_.Commods();
  std::map<Material::Ptr, std::string>::iterator it;
  for (it = recv.begin(); it != recv.end(); ++it) {
    crctx_.AddRsrc(it->second, it->first);
  }
  recv = corepolicy_.Commods();
  for (it = recv.begin(); it != recv.end(); ++it) {
    crctx_.AddRsrc(it->second, it->first);
  }

  LOG(cyclus::LEV_DEBUG3, "BReact") << "Current facility parameters for "
                                    << prototype()
                                    << " at the end of the tock are:";
  LOG(cyclus::LEV_DEBUG3, "BReact") << "    Phase: " << phase_names_[phase_];
  LOG(cyclus::LEV_DEBUG3, "BReact") << "    Start time: " << start_time_;
  LOG(cyclus::LEV_DEBUG3, "BReact") << "    End time: " << end_time();
  LOG(cyclus::LEV_DEBUG3, "BReact") << "    Order time: " << order_time();
  LOG(cyclus::LEV_DEBUG3, "BReact") << "    NReserves: " << reserves_.count();
  LOG(cyclus::LEV_DEBUG3, "BReact") << "    NCore: " << core_.count();
  LOG(cyclus::LEV_DEBUG3, "BReact") << "    NStorage: " << StorageCount();
  LOG(cyclus::LEV_DEBUG3, "BReact") << "    Spillover Qty: "
                                    << spillover_->quantity();
  LOG(cyclus::LEV_INFO3, "BReact") << "}";
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
std::set<cyclus::BidPortfolio<cyclus::Material>::Ptr>
BatchReactor::GetMatlBids(cyclus::CommodMap<cyclus::Material>::type&
                          commod_requests) {
  using cyclus::BidPortfolio;
  using cyclus::Material;

  std::set<BidPortfolio<Material>::Ptr> ports;

  const std::set<std::string>& commods = crctx_.out_commods();
  std::set<std::string>::const_iterator it;
  for (it = commods.begin(); it != commods.end(); ++it) {
    BidPortfolio<Material>::Ptr port = GetBids_(commod_requests,
                                                *it,
                                                &storage_[*it]);
    if (!port->bids().empty()) {
      ports.insert(port);
    }
  }

  return ports;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BatchReactor::GetMatlTrades(
    const std::vector< cyclus::Trade<cyclus::Material> >& trades,
    std::vector<std::pair<cyclus::Trade<cyclus::Material>,
    cyclus::Material::Ptr> >& responses) {
  using cyclus::Material;
  using cyclus::Trade;

  std::vector< cyclus::Trade<cyclus::Material> >::const_iterator it;
  for (it = trades.begin(); it != trades.end(); ++it) {
    LOG(cyclus::LEV_INFO5, "BReact") << prototype()
                                     << " just received an order.";

    std::string commodity = it->request->commodity();
    double qty = it->amt;
    Material::Ptr response = TradeResponse_(qty, &storage_[commodity]);

    responses.push_back(std::make_pair(*it, response));
    LOG(cyclus::LEV_INFO5, "BatchReactor") << prototype()
                                           << " just received an order"
                                           << " for " << qty
                                           << " of " << commodity;
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
int BatchReactor::StorageCount() {
  int count = 0;
  std::map<std::string, cyclus::toolkit::ResourceBuff>::const_iterator it;
  for (it = storage_.begin(); it != storage_.end(); ++it) {
    count += it->second.count();
  }
  return count;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BatchReactor::phase(BatchReactor::Phase p) {
  LOG(cyclus::LEV_DEBUG2, "BReact") << "BatchReactor " << prototype()
                                    << " is changing phases -";
  LOG(cyclus::LEV_DEBUG2, "BReact") << "  * from phase: " << phase_names_[phase_];
  LOG(cyclus::LEV_DEBUG2, "BReact") << "  * to phase: " << phase_names_[p];

  switch (p) {
    case PROCESS:
      start_time(context()->time());
  }
  phase_ = p;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BatchReactor::Refuel_() {
  while (n_core() < n_batches() && reserves_.count() > 0) {
    MoveBatchIn_();
    if (n_core() == n_batches()) {
      to_begin_time_ = start_time_ + process_time_ + refuel_time_;
    }
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BatchReactor::MoveBatchIn_() {
  LOG(cyclus::LEV_DEBUG2, "BReact") << "BatchReactor " << prototype()
                                    << " added a batch to its core.";
  using cyclus::toolkit::Manifest;
  using cyclus::ResCast;
  try {
    Material::Ptr batch = reserves_.Pop<Material>();
    if (batch->quantity() + cyclus::eps() < batch_size_) {
      double qty = std::min(reserves_.quantity(),
          batch_size_ - batch->quantity());
      Manifest mats = reserves_.PopQty(qty);
      for (int i = 0; i < mats.size(); i++) {
        batch->Absorb(ResCast<Material>(mats[i]));
      }
    }
    core_.Push(batch);
  } catch (cyclus::Error& e) {
    e.msg(Agent::InformErrorMsg(e.msg()));
    throw e;
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BatchReactor::MoveBatchOut_() {
  using cyclus::Material;
  using cyclus::ResCast;

  LOG(cyclus::LEV_DEBUG2, "BReact") << "BatchReactor " << prototype()
                                    << " removed a batch from its core.";
  try {
    Material::Ptr mat = ResCast<Material>(core_.Pop());
    std::string incommod = crctx_.commod(mat);
    assert(incommod != "");
    std::string outcommod = crctx_.out_commod(incommod);
    assert(outcommod != "");
    std::string outrecipe = crctx_.out_recipe(crctx_.in_recipe(incommod));
    assert(outrecipe != "");
    mat->Transmute(context()->GetRecipe(outrecipe));
    crctx_.UpdateRsrc(outcommod, mat);
    std::cout << "spot2\n";
    storage_[outcommod].Push(mat);
  } catch (cyclus::Error& e) {
    e.msg(Agent::InformErrorMsg(e.msg()));
    throw e;
  }
}

cyclus::BidPortfolio<cyclus::Material>::Ptr BatchReactor::GetBids_(
    cyclus::CommodMap<cyclus::Material>::type& commod_requests,
    std::string commod,
    cyclus::toolkit::ResourceBuff* buffer) {
  using cyclus::Bid;
  using cyclus::BidPortfolio;
  using cyclus::CapacityConstraint;
  using cyclus::Composition;
  using cyclus::Converter;
  using cyclus::Material;
  using cyclus::Request;
  using cyclus::ResCast;
  using cyclus::toolkit::ResourceBuff;

  BidPortfolio<Material>::Ptr port(new BidPortfolio<Material>());

  if (commod_requests.count(commod) > 0 && buffer->quantity() > 0) {
    std::vector<Request<Material>*>& requests = commod_requests[commod];

    // get offer composition
    Material::Ptr back = ResCast<Material>(buffer->Pop(ResourceBuff::BACK));
    Composition::Ptr comp = back->comp();
    std::cout << "spot3\n";
    buffer->Push(back);

    std::vector<Request<Material>*>::iterator it;
    for (it = requests.begin(); it != requests.end(); ++it) {
      Request<Material>* req = *it;
      double qty = std::min(req->target()->quantity(), buffer->quantity());
      Material::Ptr offer = Material::CreateUntracked(qty, comp);
      port->AddBid(req, offer, this);
    }

    CapacityConstraint<Material> cc(buffer->quantity());
    port->AddConstraint(cc);
  }

  return port;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
cyclus::Material::Ptr BatchReactor::TradeResponse_(
    double qty,
    cyclus::toolkit::ResourceBuff* buffer) {
  using cyclus::Material;
  using cyclus::ResCast;

  std::vector<Material::Ptr> manifest;
  try {
    // pop amount from inventory and blob it into one material
    manifest = ResCast<Material>(buffer->PopQty(qty));
  } catch (cyclus::Error& e) {
    e.msg(Agent::InformErrorMsg(e.msg()));
    throw e;
  }

  Material::Ptr response = manifest[0];
  crctx_.RemoveRsrc(response);
  for (int i = 1; i < manifest.size(); i++) {
    crctx_.RemoveRsrc(manifest[i]);
    response->Absorb(manifest[i]);
  }
  return response;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BatchReactor::SetUpPhaseNames_() {
  phase_names_.insert(std::make_pair(INITIAL, "initialization"));
  phase_names_.insert(std::make_pair(PROCESS, "processing batch(es)"));
  phase_names_.insert(std::make_pair(WAITING, "waiting for fuel"));
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
extern "C" cyclus::Agent* ConstructBatchReactor(cyclus::Context* ctx) {
  return new BatchReactor(ctx);
}

}  // namespace cycamore
