#ifndef CYCAMORE_SRC_STORAGE_H_
#define CYCAMORE_SRC_STORAGE_H_

#include "cyclus.h"

namespace cycamore {

class Storage : public cyclus::Facility {
 public:
  Storage(cyclus::Context* ctx);
  virtual ~Storage(){};

  virtual void Tick();

  virtual void Tock() {}

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

  #pragma cyclus

 private:
  #pragma cyclus var { \
    "doc": "Name for recipe to be used in requests." \
           " Empty string results in use of an empty dummy recipe.", \
    "uitype": "recipe", \
    "default": "", \
  }
  std::string inrecipe;

  #pragma cyclus var { \
  }
  std::string incommod;

  #pragma cyclus var { \
  }
  std::string outcommod;
  
  #pragma cyclus var { \
    "internal": True, \
    "default": {}, \
  }
  std::map<int, int> time_npop;

  #pragma cyclus var { \
  }
  int wait_time;

  #pragma cyclus var { \
    "default" : 1e299, \
  }
  double invsize;

  #pragma cyclus var { \
    "internal": True, \
    "default" : 1e299, \
  }
  double waitingbuf_size;

  #pragma cyclus var { \
    "capacity" : "invsize", \
  }
  cyclus::toolkit::ResBuf<cyclus::Material> ready;

  #pragma cyclus var { \
    "capacity" : "waitingbuf_size", \
  }
  cyclus::toolkit::ResBuf<cyclus::Material> waiting;
  std::map<std::string, cyclus::toolkit::ResBuf<cyclus::Material> > streambufs;
};

}  // namespace cycamore

#endif  // CYCAMORE_SRC_STORAGE_H_
