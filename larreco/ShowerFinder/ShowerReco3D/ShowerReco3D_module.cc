////////////////////////////////////////////////////////////////////////
// Class:       ShowerReco3D
// Module Type: producer
// File:        ShowerReco3D_module.cc
//
// Generated at Tue Jan 20 07:26:48 2015 by Kazuhiro Terao using artmod
// from cetpkgsupport v1_08_02.
////////////////////////////////////////////////////////////////////////

#include "art/Framework/Core/EDProducer.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Principal/Handle.h"
#include "art/Framework/Services/Registry/ServiceHandle.h"
#include "canvas/Persistency/Common/FindManyP.h"
#include "fhiclcpp/ParameterSet.h"

#include "larcore/CoreUtils/ServiceUtil.h"
#include "larcore/Geometry/Geometry.h"
#include "larcore/Geometry/WireReadout.h"
#include "lardata/DetectorInfoServices/DetectorClocksService.h"
#include "lardata/DetectorInfoServices/DetectorPropertiesService.h"
#include "lardata/Utilities/AssociationUtil.h"
#include "lardata/Utilities/GeometryUtilities.h"
#include "lardata/Utilities/PxHitConverter.h"
#include "lardataobj/RecoBase/Cluster.h"
#include "lardataobj/RecoBase/Hit.h"
#include "lardataobj/RecoBase/PFParticle.h"
#include "lardataobj/RecoBase/Shower.h"
#include "larreco/Calorimetry/CalorimetryAlg.h"
#include "larreco/RecoAlg/CMTool/CMTAlgMatch/CFAlgoTimeOverlap.h"
#include "larreco/RecoAlg/CMTool/CMTAlgPriority/CPAlgoArray.h"
#include "larreco/RecoAlg/CMTool/CMTAlgPriority/CPAlgoIgnoreTracks.h"
#include "larreco/RecoAlg/CMTool/CMTAlgPriority/CPAlgoNHits.h"
#include "larreco/RecoAlg/CMTool/CMToolBase/CMatchManager.h"

#include "larreco/ShowerFinder/ShowerReco3D/ShowerRecoAlg.h"
#include "larreco/ShowerFinder/ShowerReco3D/ShowerRecoManager.h"

#include <memory>
#include <string>

class ShowerReco3D : public art::EDProducer {
public:
  explicit ShowerReco3D(fhicl::ParameterSet const& p);
  // The destructor generated by the compiler is fine for classes
  // without bare pointers or other resource use.

  // Plugins should not be copied or assigned.
  ShowerReco3D(ShowerReco3D const&) = delete;
  ShowerReco3D(ShowerReco3D&&) = delete;
  ShowerReco3D& operator=(ShowerReco3D const&) = delete;
  ShowerReco3D& operator=(ShowerReco3D&&) = delete;

private:
  // Required functions.
  void produce(art::Event& e) override;

  // Declare member data here.
  bool fUsePFParticle;
  std::string fInputProducer;
  ::showerreco::ShowerRecoManager fManager;
  ::showerreco::ShowerRecoAlg* fShowerAlgo;
  ::calo::CalorimetryAlg* fCaloAlgo;
  ::cmtool::CPAlgoArray* fCPAlgoArray;
  ::cmtool::CPAlgoNHits* fCPAlgoNHits;
  ::cmtool::CPAlgoIgnoreTracks* fCPAlgoIgnoreTracks;
  ::cmtool::CFAlgoTimeOverlap* fCFAlgoTimeOverlap;
};

ShowerReco3D::ShowerReco3D(fhicl::ParameterSet const& p)
  : EDProducer{p}, fManager{art::ServiceHandle<geo::WireReadout>()->Get().Nplanes()}
{
  fInputProducer = p.get<std::string>("InputProducer");
  fUsePFParticle = p.get<bool>("UsePFParticle");
  produces<std::vector<recob::Shower>>();
  produces<art::Assns<recob::Shower, recob::Cluster>>();
  produces<art::Assns<recob::Shower, recob::Hit>>();
  if (fUsePFParticle) produces<art::Assns<recob::PFParticle, recob::Shower>>();

  // Instantiate algorithms
  fCaloAlgo = new ::calo::CalorimetryAlg(p.get<fhicl::ParameterSet>("CalorimetryAlg"));
  fShowerAlgo = new ::showerreco::ShowerRecoAlg;
  fCPAlgoArray = new ::cmtool::CPAlgoArray;
  fCPAlgoNHits = new ::cmtool::CPAlgoNHits;
  fCPAlgoIgnoreTracks = new ::cmtool::CPAlgoIgnoreTracks;
  fCFAlgoTimeOverlap = new ::cmtool::CFAlgoTimeOverlap;

  // Configure
  fCPAlgoNHits->SetMinHits(p.get<int>("MinHits"));

  fCPAlgoArray->AddAlgo(fCPAlgoNHits);
  fCPAlgoArray->AddAlgo(fCPAlgoIgnoreTracks);

  fManager.MatchManager().AddPriorityAlgo(fCPAlgoArray);
  fManager.MatchManager().AddMatchAlgo(fCFAlgoTimeOverlap);

  fShowerAlgo->Verbose(p.get<bool>("Verbosity"));
  fShowerAlgo->SetUseArea(p.get<bool>("UseArea"));
  fShowerAlgo->setEcorrection(p.get<bool>("ApplyMCEnergyCorrection"));
  fShowerAlgo->CaloAlgo(fCaloAlgo);

  fManager.Algo(fShowerAlgo);
}

void ShowerReco3D::produce(art::Event& e)
{
  auto const& geom = *lar::providerFrom<geo::Geometry>();
  auto const& wireReadoutGeom = art::ServiceHandle<geo::WireReadout const>()->Get();
  auto const clockData = art::ServiceHandle<detinfo::DetectorClocksService const>()->DataFor(e);
  auto const detProp =
    art::ServiceHandle<detinfo::DetectorPropertiesService const>()->DataFor(e, clockData);
  util::GeometryUtilities const gser{geom, wireReadoutGeom, clockData, detProp};

  // Create output data product containers
  std::unique_ptr<std::vector<recob::Shower>> out_shower_v(new std::vector<recob::Shower>);
  std::unique_ptr<art::Assns<recob::Shower, recob::Cluster>> sc_assn(
    new art::Assns<recob::Shower, recob::Cluster>);
  std::unique_ptr<art::Assns<recob::Shower, recob::Hit>> sh_assn(
    new art::Assns<recob::Shower, recob::Hit>);
  std::unique_ptr<art::Assns<recob::PFParticle, recob::Shower>> sp_assn(
    new art::Assns<recob::PFParticle, recob::Shower>);

  // Preparation

  // Reset ShowerRecoManager
  fManager.Reset();

  // Retrieve input clusters
  art::Handle<std::vector<recob::Cluster>> cHandle;
  e.getByLabel(fInputProducer, cHandle);

  if (!cHandle.isValid())
    throw cet::exception(__FUNCTION__) << "Invalid input cluster label!" << std::endl;

  // Cluster type conversion: recob::Hit => util::PxHit
  std::vector<std::vector<::util::PxHit>> local_clusters;
  art::FindManyP<recob::Hit> hit_m(cHandle, e, fInputProducer);
  ::util::PxHitConverter conv{gser};
  for (size_t i = 0; i < cHandle->size(); ++i) {
    local_clusters.push_back(std::vector<::util::PxHit>());

    const std::vector<art::Ptr<recob::Hit>>& hits = hit_m.at(i);

    conv.GeneratePxHit(hits, local_clusters.back());
  }

  //
  // Run shower reconstruction
  //
  // shower pfparticle index (later used to make an association)
  std::vector<size_t> shower_pfpart_index;
  // cluster combination in terms of cluster index (for ShowerRecoManager::Reconstruct)
  std::vector<std::vector<unsigned int>> matched_pairs;
  // shower vector container to receive from ShowerRecoManager::Reconstruct
  std::vector<recob::Shower> shower_v;

  if (!fUsePFParticle) {
    matched_pairs =
      fManager.Reconstruct(geom, wireReadoutGeom, clockData, detProp, local_clusters, shower_v);
  }
  else {

    // Retrieve PFParticle
    art::Handle<std::vector<recob::PFParticle>> pfHandle;
    e.getByLabel(fInputProducer, pfHandle);
    if (!pfHandle.isValid())
      throw cet::exception(__FUNCTION__) << "Invalid input PFParticle label!" << std::endl;

    // Make a cluster ptr => index map to fill matched_pairs
    std::map<art::Ptr<recob::Cluster>, size_t> cmap;
    for (size_t i = 0; i < cHandle->size(); ++i) {

      const art::Ptr<recob::Cluster> cptr(cHandle, i);

      cmap[cptr] = i;
    }
    // Now to fill matched_pairs retrieve association of pfpart => cluster(s)
    art::FindManyP<recob::Cluster> cluster_m(pfHandle, e, fInputProducer);

    for (size_t i = 0; i < pfHandle->size(); ++i) {

      const art::Ptr<recob::PFParticle> pf(pfHandle, i);

      if (pf->PdgCode() != 11) continue;

      const std::vector<art::Ptr<recob::Cluster>>& clusters = cluster_m.at(i);

      std::vector<unsigned int> one_pair;
      one_pair.reserve(clusters.size());

      for (auto const& cptr : clusters) {

        auto iter = cmap.find(cptr);
        if (iter == cmap.end())
          throw cet::exception(__FUNCTION__)
            << "PFParticle=>Cluster association not valid!" << std::endl;

        one_pair.push_back((*iter).second);
      }
      matched_pairs.push_back(one_pair);
      shower_pfpart_index.push_back(i);
    }
    // Run reconstruction
    fManager.Reconstruct(
      geom, wireReadoutGeom, clockData, detProp, local_clusters, matched_pairs, shower_v);
  }

  // Make sure output shower vector size is same as expected length
  if (shower_v.size() != matched_pairs.size())
    throw cet::exception(__FUNCTION__)
      << "Logic error: # of matched pairs != # of reco-ed showers!" << std::endl;

  // Fill output shower vector data container
  out_shower_v->reserve(shower_v.size());

  for (size_t i = 0; i < shower_v.size(); ++i) {

    // Set output shower ID
    shower_v[i].set_id(i);

    out_shower_v->push_back(shower_v[i]);

    // Create shower=>cluster association
    std::vector<art::Ptr<recob::Cluster>> ass_clusters;
    // Create shower=>hit association
    std::vector<art::Ptr<recob::Hit>> ass_hits;
    for (auto const& cindex : matched_pairs[i]) {

      ass_clusters.push_back(art::Ptr<recob::Cluster>(cHandle, cindex));

      const std::vector<art::Ptr<recob::Hit>>& hits = hit_m.at(cindex);

      for (auto const& ptr : hits)
        ass_hits.push_back(ptr);
    }
    // Shower=>Cluster
    util::CreateAssn(e, *(out_shower_v.get()), ass_clusters, *(sc_assn.get()));
    // Shower=>Hits
    util::CreateAssn(e, *(out_shower_v.get()), ass_hits, *(sh_assn.get()));

    if (fUsePFParticle) {

      art::Handle<std::vector<recob::PFParticle>> pfHandle;
      e.getByLabel(fInputProducer, pfHandle);

      art::Ptr<recob::PFParticle> pf_ptr(pfHandle, shower_pfpart_index[i]);

      util::CreateAssn(e, *(out_shower_v.get()), pf_ptr, *(sp_assn.get()));
    }
  }

  // Store in an event record
  e.put(std::move(out_shower_v));
  e.put(std::move(sh_assn));
  e.put(std::move(sc_assn));
  if (fUsePFParticle) e.put(std::move(sp_assn));
}

DEFINE_ART_MODULE(ShowerReco3D)
