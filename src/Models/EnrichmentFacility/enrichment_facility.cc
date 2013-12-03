// enrichment_facility.cc
// Implements the EnrichmentFacility class
#include "enrichment_facility.h"

#include <sstream>
#include <limits>
#include <cmath>
#include <algorithm>

#include <boost/lexical_cast.hpp>

#include "query_engine.h"
#include "logger.h"
#include "error.h"
#include "context.h"
#include "cyc_limits.h"
#include "generic_resource.h"
#include "material.h"
#include "mat_query.h"
#include "timer.h"

namespace cycamore {

int EnrichmentFacility::entry_ = 0;

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
EnrichmentFacility::EnrichmentFacility(cyclus::Context* ctx)
  : cyclus::FacilityModel(ctx),
    cyclus::Model(ctx),
    commodity_price_(0),
    tails_assay_(0),
    feed_assay_(0),
    swu_capacity_(0),
    in_commod_(""),
    in_recipe_(""),
    out_commod_("") {}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
EnrichmentFacility::~EnrichmentFacility() {}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
std::string EnrichmentFacility::schema() {
  return
    "  <element name =\"input\">            \n"
    "    <ref name=\"incommodity\"/>        \n"
    "    <ref name=\"inrecipe\"/>           \n"
    "    <optional>                         \n"
    "      <ref name=\"inventorysize\"/>    \n"
    "    </optional>                        \n"
    "  </element>                           \n"
    "  <element name =\"output\">           \n"
    "    <ref name=\"outcommodity\"/>       \n"
    "     <element name =\"tails_assay\">   \n"
    "       <data type=\"double\"/>         \n"
    "     </element>                        \n"
    "    <optional>                         \n"
    "      <element name =\"swu_capacity\"> \n"
    "        <data type=\"double\"/>        \n"
    "      </element>                       \n"
    "    </optional>                        \n"
    "  </element>                           \n";
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void EnrichmentFacility::InitModuleMembers(cyclus::QueryEngine* qe) {
  using std::string;
  using std::numeric_limits;
  using boost::lexical_cast;
  using cyclus::Model;
  using cyclus::Material;

  string data;

  cyclus::QueryEngine* input = qe->QueryElement("input");
  in_commodity(input->GetElementContent("incommodity"));
  in_recipe(input->GetElementContent("inrecipe"));

  double limit =
    cyclus::GetOptionalQuery<double>(input,
                                     "inventorysize",
                                     numeric_limits<double>::max());
  SetMaxInventorySize(limit);

  cyclus::QueryEngine* output = qe->QueryElement("output");
  out_commodity(output->GetElementContent("outcommodity"));

  data = output->GetElementContent("tails_assay");
  tails_assay(lexical_cast<double>(data));

  Material::Ptr feed =
      Material::CreateUntracked(0, context()->GetRecipe(in_recipe_));
  feed_assay(cyclus::enrichment::UraniumAssay(feed));

  double cap = cyclus::GetOptionalQuery<double>(output,
                                                "swu_capacity",
                                                numeric_limits<double>::max());
  swu_capacity(cap);
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
std::string EnrichmentFacility::str() {
  std::stringstream ss;
  ss << cyclus::FacilityModel::str()
     << " with enrichment facility parameters:"
     << " * SWU capacity: " << swu_capacity()
     << " * Tails assay: " << tails_assay()
     << " * Feed assay: " << feed_assay()
     << " * Input cyclus::Commodity: " << in_commodity()
     << " * Output cyclus::Commodity: " << out_commodity();
  return ss.str();
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
cyclus::Model* EnrichmentFacility::Clone() {
  EnrichmentFacility* m = new EnrichmentFacility(*this);
  m->InitFrom(this);

  m->tails_assay(tails_assay());
  m->feed_assay(feed_assay());
  m->in_commodity(in_commodity());
  m->in_recipe(in_recipe());
  m->out_commodity(out_commodity());
  m->SetMaxInventorySize(MaxInventorySize());
  m->commodity_price(commodity_price());
  m->swu_capacity(swu_capacity());
  
  LOG(cyclus::LEV_DEBUG1, "EnrFac") << "Cloned - " << str();
  return m;
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void EnrichmentFacility::HandleTick(int time) {
  LOG(cyclus::LEV_INFO3, "EnrFac") << FacName() << " is ticking {";
  LOG(cyclus::LEV_INFO3, "EnrFac") << "}";
  current_swu_capacity_ = swu_capacity();
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void EnrichmentFacility::HandleTock(int time) {
  LOG(cyclus::LEV_INFO3, "EnrFac") << FacName() << " is tocking {";
  LOG(cyclus::LEV_INFO3, "EnrFac") << "}";
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
std::set<cyclus::RequestPortfolio<cyclus::Material>::Ptr>
EnrichmentFacility::AddMatlRequests() {
  using cyclus::CapacityConstraint;
  using cyclus::Material;
  using cyclus::RequestPortfolio;
  using cyclus::Request;
  
  std::set<RequestPortfolio<Material>::Ptr> ports;
  RequestPortfolio<Material>::Ptr port(new RequestPortfolio<Material>());
  Material::Ptr mat = Request_();
  double amt = mat->quantity();

  if (amt > cyclus::eps()) {
    CapacityConstraint<Material> cc(amt);
    port->AddConstraint(cc);
    
    Request<Material>::Ptr req(new Request<Material>(mat, this, in_commod_));
    port->AddRequest(req);
    
    ports.insert(port);
  } // if amt > eps

  return ports;
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 
void EnrichmentFacility::AcceptMatlTrades(
    const std::vector< std::pair<cyclus::Trade<cyclus::Material>,
                                 cyclus::Material::Ptr> >& responses) {
  // see
  // http://stackoverflow.com/questions/5181183/boostshared-ptr-and-inheritance
  std::vector< std::pair<cyclus::Trade<cyclus::Material>,
                         cyclus::Material::Ptr> >::const_iterator it;
  for (it = responses.begin(); it != responses.end(); ++it) {
    AddMat_(it->second);    
  }
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
std::set<cyclus::BidPortfolio<cyclus::Material>::Ptr>
EnrichmentFacility::AddMatlBids(cyclus::ExchangeContext<cyclus::Material>* ec) {
  using cyclus::Bid;
  using cyclus::BidPortfolio;
  using cyclus::CapacityConstraint;
  using cyclus::Converter;
  using cyclus::Material;
  using cyclus::Request;
  
  std::set<BidPortfolio<Material>::Ptr> ports;
  BidPortfolio<Material>::Ptr port(new BidPortfolio<Material>());
  
  const std::vector<Request<Material>::Ptr>& requests =
      ec->requests_by_commod[out_commod_];

  std::vector<Request<Material>::Ptr>::const_iterator it;
  for (it = requests.begin(); it != requests.end(); ++it) {
    const Request<Material>::Ptr req = *it;
    if (ValidReq(req->target())) { 
      Material::Ptr offer = Offer_(req->target());
      Bid<Material>::Ptr bid(new Bid<Material>(req, offer, this));
      port->AddBid(bid);
    }
  }

  Converter<Material>::Ptr sc(new SWUConverter(feed_assay_, tails_assay_));
  Converter<Material>::Ptr nc(new NatUConverter(feed_assay_, tails_assay_));
  CapacityConstraint<Material> swu(swu_capacity_, sc);
  CapacityConstraint<Material> natu(inventory_.quantity(), nc);
  port->AddConstraint(swu);
  port->AddConstraint(natu);
  ports.insert(port);
  return ports;
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool EnrichmentFacility::ValidReq(const cyclus::Material::Ptr mat) {
  cyclus::MatQuery q(mat);
  double u235 = q.atom_frac(92235);
  double u238 = q.atom_frac(92238);
  return (u238 > 0 && u235 / (u235 + u238) > tails_assay());
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void EnrichmentFacility::PopulateMatlTradeResponses(
    const std::vector< cyclus::Trade<cyclus::Material> >& trades,
    std::vector<std::pair<cyclus::Trade<cyclus::Material>,
                          cyclus::Material::Ptr> >& responses) {
  using cyclus::Material;
  using cyclus::StateError;
  using cyclus::Trade;

  std::vector< Trade<Material> >::const_iterator it;
  for (it = trades.begin(); it != trades.end(); ++it) {
    Material::Ptr response = Enrich_(it->bid->offer(), it->amt);
    responses.push_back(std::make_pair(*it, response));
    LOG(cyclus::LEV_INFO5, "EnrFac") << name()
                                     << " just received an order"
                                     << " for " << it->amt
                                     << " of " << out_commod_;
  }
  
  if (cyclus::IsNegative(current_swu_capacity_)) { 
    throw StateError(
        "EnrFac " + name()
        + " is being asked to provide more than its SWU capacity.");
  }
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void EnrichmentFacility::AddMat_(cyclus::Material::Ptr mat) {
  if (mat->comp() != context()->GetRecipe(in_recipe_)) {
    throw cyclus::StateError(
        "EnrichmentFacility recipe and material composition not the same.");
  } 

  inventory_.Push(mat);    
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
cyclus::Material::Ptr EnrichmentFacility::Request_() {
  double qty = std::max(0.0, MaxInventorySize() - InventorySize());
  return cyclus::Material::CreateUntracked(qty,
                                           context()->GetRecipe(in_recipe_));
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
cyclus::Material::Ptr EnrichmentFacility::Offer_(cyclus::Material::Ptr mat) {
  cyclus::MatQuery q(mat);
  cyclus::CompMap comp;
  comp[92235] = q.atom_frac(92235);
  comp[92238] = q.atom_frac(92238);
  return cyclus::Material::CreateUntracked(
      mat->quantity(), cyclus::Composition::CreateFromAtom(comp));
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
cyclus::Material::Ptr EnrichmentFacility::Enrich_(
    cyclus::Material::Ptr mat,
    double qty) {
  using cyclus::Material;
  using cyclus::ResCast;
  using cyclus::enrichment::Assays;
  using cyclus::enrichment::UraniumAssay;
  using cyclus::enrichment::SwuRequired;
  using cyclus::enrichment::FeedQty;
  using cyclus::enrichment::TailsQty;

  // get enrichment parameters
  Assays assays(feed_assay(), UraniumAssay(mat), tails_assay());
  double swu_req = SwuRequired(qty, assays);
  double natu_req = FeedQty(qty, assays);

  // pop amount from inventory and blob it into one material
  std::vector<Material::Ptr> manifest =
      ResCast<Material>(inventory_.PopQty(natu_req));  
  Material::Ptr r = manifest[0];
  for (int i = 1; i < manifest.size(); ++i) {
    r->Absorb(manifest[i]);
  }

  // "enrich" it, but pull out the composition and quantity we require from the
  // blob
  Material::Ptr response = r->ExtractComp(qty, mat->comp());
  
  current_swu_capacity_ -= swu_req;
  
  RecordEnrichment_(natu_req, swu_req);

  LOG(cyclus::LEV_INFO5, "EnrFac") << name() << " has performed an enrichment: ";
  LOG(cyclus::LEV_INFO5, "EnrFac") << "   * Feed Qty: "
                                   << natu_req;
  LOG(cyclus::LEV_INFO5, "EnrFac") << "   * Feed Assay: "
                                   << assays.Feed() * 100;
  LOG(cyclus::LEV_INFO5, "EnrFac") << "   * Product Qty: "
                                   << qty;
  LOG(cyclus::LEV_INFO5, "EnrFac") << "   * Product Assay: "
                                   << assays.Product() * 100;
  LOG(cyclus::LEV_INFO5, "EnrFac") << "   * Tails Qty: "
                                   << TailsQty(qty, assays);
  LOG(cyclus::LEV_INFO5, "EnrFac") << "   * Tails Assay: "
                                   << assays.Tails() * 100;
  LOG(cyclus::LEV_INFO5, "EnrFac") << "   * SWU: "
                                   << swu_req;

  return response;
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void EnrichmentFacility::RecordEnrichment_(double natural_u, double swu) {
  using cyclus::Context;
  using cyclus::Model;

  LOG(cyclus::LEV_DEBUG1, "EnrFac") << name() << " has enriched a material:";
  LOG(cyclus::LEV_DEBUG1, "EnrFac") << "  * Amount: " << natural_u;
  LOG(cyclus::LEV_DEBUG1, "EnrFac") << "  *    SWU: " << swu;

  Context* ctx = Model::context();
  ctx->NewEvent("Enrichments")
  ->AddVal("ENTRY", ++entry_)
  ->AddVal("ID", id())
  ->AddVal("Time", ctx->time())
  ->AddVal("Natural_Uranium", natural_u)
  ->AddVal("SWU", swu)
  ->Record();
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
extern "C" cyclus::Model* ConstructEnrichmentFacility(cyclus::Context* ctx) {
  return new EnrichmentFacility(ctx);
}

} // namespace cycamore
