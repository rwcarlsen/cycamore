#ifndef CYCAMORE_SRC_FLEET_REACTOR_H_
#define CYCAMORE_SRC_FLEET_REACTOR_H_

#include "cyclus.h"

namespace cycamore {

class FleetReactor : public cyclus::Facility,
  public cyclus::toolkit::CommodityProducer {

 public:
  FleetReactor(cyclus::Context* ctx);
  virtual ~FleetReactor(){};

  virtual void Tick();
  virtual void Tock();
  virtual void EnterNotify();

  virtual void AcceptMatlTrades(const std::vector<std::pair<
      cyclus::Trade<cyclus::Material>, cyclus::Material::Ptr> >& responses);

  virtual std::set<cyclus::RequestPortfolio<cyclus::Material>::Ptr>
  GetMatlRequests();

  virtual std::set<cyclus::BidPortfolio<cyclus::Material>::Ptr> GetMatlBids(
      cyclus::CommodMap<cyclus::Material>::type& commod_requests);

  virtual void GetMatlTrades(
      const std::vector<cyclus::Trade<cyclus::Material> >& trades,
      std::vector<std::pair<cyclus::Trade<cyclus::Material>,
                            cyclus::Material::Ptr> >& responses);

  #pragma cyclus decl

  virtual void Build(Agent* parent);
  virtual void Decommission();
  virtual bool CheckDecommissionCondition();

 private:
  FleetReactor* master() {
    am_master();
    return masters_[prototype()];
  }

  bool am_master() {
    if (am_master_) {
      if (masters_.count(prototype()) > 0) {
        // in case another agent incorrectly set itself to master already.
        masters_[prototype()]->am_master_ = false;
      }
      masters_[prototype()] = this;
      am_master_ = true; // in case we set it to false above
    } else if (masters_.count(prototype()) == 0) {
      masters_[prototype()] = this;
      am_master_ = true;
    }
    return am_master_;
  }

  static std::map<std::string, FleetReactor*> masters_;

  // this is necessary for cross snapshot-init to remember which agent is the
  // master because the master's state evolves differently and so must be the
  // same instance from init to init.
  #pragma cyclus var {"default": False, "internal": True}
  bool am_master_;

  std::string fuel_incommod(cyclus::Material::Ptr m);
  std::string fuel_outcommod(cyclus::Material::Ptr m);
  std::string fuel_inrecipe(cyclus::Material::Ptr m);
  std::string fuel_outrecipe(cyclus::Material::Ptr m);
  double fuel_pref(cyclus::Material::Ptr m);

  /// Store fuel info index for the given resource received on incommod.
  void index_res(cyclus::Resource::Ptr m, std::string incommod);

  /// Discharge some quantity of material from the core.  If qty is less than
  /// zero (default), a time step worth of burning/transmuting/discharging is
  /// performed for all material.
  void Discharge(double qty=-1);

  void Retire(double number_of_fleet);
  void Deploy(double number_of_fleet);

  /// Complement of PopSpent - must be called with all materials passed that
  /// were not traded away to other agents.
  void PushSpent(std::map<std::string, cyclus::Material::Ptr> leftover);

  /// Returns all spent assemblies indexed by outcommod - removing them from
  /// the spent fuel buffer.
  std::map<std::string, cyclus::Material::Ptr> PopSpent();

  /// Returns all spent assemblies indexed by outcommod without removing them
  /// from the spent fuel buffer.
  std::map<std::string, cyclus::Material::Ptr> PeekSpent();

  //////////// power params ////////////
  #pragma cyclus var { \
    "default": 0, \
    "doc": "Amount of electrical power the facility produces when operating normally.", \
    "units": "MWe", \
  }
  double power_cap;

  #pragma cyclus var { \
    "default": "power", \
    "doc": "The name of the 'power' commodity used in conjunction with a deployment curve.", \
  }
  std::string power_name;
  
  //////////// inventory and core params ////////////
  #pragma cyclus var { \
    "doc": "Mass of a full core.", \
    "units": "kg", \
  }
  double core_size;
  #pragma cyclus var { \
    "doc": "Mass of a full batch or the amount of material discharged per cycle.", \
    "units": "kg", \
  }
  int batch_size;
  #pragma cyclus var { \
    "doc": "number of reactors in the entire fleet", \
    "units": "kg", \
    "internal": True, \
    "default": 0, \
  }
  double fleet_size;

  ///////// cycle params ///////////
  #pragma cyclus var { \
    "doc": "The duration of a full operational cycle (including refueling time) in time steps.", \
    "units": "time steps", \
  }
  int cycle_time;

  /////// fuel specifications /////////
  #pragma cyclus var { \
    "uitype": ["oneormore", "incommodity"], \
    "doc": "Ordered list of input commodities on which to requesting fuel.", \
  }
  std::vector<std::string> fuel_incommods;
  #pragma cyclus var { \
    "uitype": ["oneormore", "recipe"], \
    "doc": "Fresh fuel recipes to request for each of the given fuel input commodities (same order).", \
  }
  std::vector<std::string> fuel_inrecipes;
  #pragma cyclus var { \
    "uitype": ["oneormore", "recipe"], \
    "doc": "Spent fuel recipes corresponding to the given fuel input commodities (same order)." \
           " Fuel received via a particular input commodity is transmuted to the recipe specified" \
           " here after being burned during a cycle.", \
  }
  std::vector<std::string> fuel_outrecipes;
  #pragma cyclus var { \
    "uitype": ["oneormore", "incommodity"], \
    "doc": "Output commodities on which to offer spent fuel originally received as each particular " \
           " input commodity (same order)." \
  }
  std::vector<std::string> fuel_outcommods;
  #pragma cyclus var { \
    "default": [], \
    "doc": "The preference for each type of fresh fuel requested corresponding to each input" \
           " commodity (same order).  If no preferences are specified, zero is" \
           " used for all fuel requests (default).", \
  }
  std::vector<double> fuel_prefs;

  // Resource inventories - these must be defined AFTER/BELOW the member vars
  // referenced (e.g. n_batch_fresh, assem_size, etc.).
  #pragma cyclus var {"capacity": "core_size * fleet_size"}
  cyclus::toolkit::ResBuf<cyclus::Material> core;
  #pragma cyclus var {"capacity": 1e299}
  cyclus::toolkit::ResBuf<cyclus::Material> spent;

  /////////// preference changes ///////////
  #pragma cyclus var { \
    "default": [], \
    "doc": "A time step on which to change the request preference for a " \
           "particular fresh fuel type.", \
  }
  std::vector<int> pref_change_times;
  #pragma cyclus var { \
    "default": [], \
    "doc": "The input commodity for a particular fuel preference change." \
           " Same order as and direct correspondence to the specified preference change times.", \
    "uitype": ["oneormore", "incommodity"], \
  }
  std::vector<std::string> pref_change_commods;
  #pragma cyclus var { \
    "default": [], \
    "doc": "The new/changed request preference for a particular fresh fuel." \
           " Same order as and direct correspondence to the specified preference change times.", \
  }
  std::vector<double> pref_change_values;

  ///////////// recipe changes ///////////
  #pragma cyclus var { \
    "default": [], \
    "doc": "A time step on which to change the input-output recipe pair for a requested fresh fuel.", \
  }
  std::vector<int> recipe_change_times;
  #pragma cyclus var { \
    "default": [], \
    "doc": "The input commodity indicating fresh fuel for which recipes will be changed." \
           " Same order as and direct correspondence to the specified recipe change times.", \
    "uitype": ["oneormore", "incommodity"], \
  }
  std::vector<std::string> recipe_change_commods;
  #pragma cyclus var { \
    "default": [], \
    "doc": "The new input recipe to use for this recipe change." \
           " Same order as and direct correspondence to the specified recipe change times.", \
    "uitype": ["oneormore", "recipe"], \
  }
  std::vector<std::string> recipe_change_in;
  #pragma cyclus var { \
    "default": [], \
    "doc": "The new output recipe to use for this recipe change." \
           " Same order as and direct correspondence to the specified recipe change times.", \
    "uitype": ["oneormore", "recipe"], \
  }
  std::vector<std::string> recipe_change_out;

  // This variable should be hidden/unavailable in ui.  Maps resource object
  // id's to the index for the incommod through which they were received.
  #pragma cyclus var {"default": {}, "doc": "This should NEVER be set manually."}
  std::map<int, int> res_indexes;


  // populated lazily and no need to persist.  In case we have the same out
  // commod for separate in commods of fuel, we don't want to double bid.  So
  // this is used to loop over unique out commods only.
  std::set<std::string> uniq_outcommods_;

  double newfleet_;
};

} // namespace cycamore

#endif  // CYCAMORE_SRC_FLEET_REACTOR_H_
