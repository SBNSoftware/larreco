////////////////////////////////////////////////////////////////////////
// Class:       TrackCalorimetry
// Module Type: producer
// File:        TrackCalorimetry_module.cc
//
// Title:   Track Calorimetry Algorithim Class
// Author:  Wes Ketchum (wketchum@lanl.gov), based on code the Calorimetry_module
//
// Description: Algorithm that produces a calorimetry object given a track
// Input:       recob::Track, Assn<recob::Spacepoint,recob::Track>, Assn<recob::Hit,recob::Track>
// Output:      anab::Calorimetry, (and Assn<anab::Calorimetry,recob::Track>)
//
// Generated at Tue Oct 21 15:54:15 2014 by Wesley Ketchum using artmod
// from cetpkgsupport v1_07_01.
////////////////////////////////////////////////////////////////////////

#include "art/Framework/Core/EDProducer.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Principal/Handle.h"
#include "art/Framework/Services/Registry/ServiceHandle.h"
#include "fhiclcpp/ParameterSet.h"

#include <memory>

#include "TrackCalorimetryAlg.h"
#include "larcore/Geometry/WireReadout.h"
#include "lardata/DetectorInfoServices/DetectorClocksService.h"
#include "lardata/DetectorInfoServices/DetectorPropertiesService.h"
#include "lardata/Utilities/AssociationUtil.h"
#include "lardataobj/AnalysisBase/Calorimetry.h"
#include "lardataobj/RecoBase/Track.h"

namespace calo {
  class TrackCalorimetry;
}

class calo::TrackCalorimetry : public art::EDProducer {
public:
  explicit TrackCalorimetry(fhicl::ParameterSet const& p);

  // Plugins should not be copied or assigned.
  TrackCalorimetry(TrackCalorimetry const&) = delete;
  TrackCalorimetry(TrackCalorimetry&&) = delete;
  TrackCalorimetry& operator=(TrackCalorimetry const&) = delete;
  TrackCalorimetry& operator=(TrackCalorimetry&&) = delete;

private:
  // Required functions.
  void produce(art::Event& e) override;

  std::string fTrackModuleLabel;
  std::string fHitModuleLabel;

  TrackCalorimetryAlg fTrackCaloAlg;
};

calo::TrackCalorimetry::TrackCalorimetry(fhicl::ParameterSet const& p)
  : EDProducer{p}
  , fTrackModuleLabel(p.get<std::string>("TrackModuleLabel"))
  , fHitModuleLabel(p.get<std::string>("HitModuleLabel"))
  , fTrackCaloAlg(p.get<fhicl::ParameterSet>("TrackCalorimetryAlg"))
{
  fTrackModuleLabel = p.get<std::string>("TrackModuleLabel");
  fHitModuleLabel = p.get<std::string>("HitModuleLabel");

  produces<std::vector<anab::Calorimetry>>();
  produces<art::Assns<recob::Track, anab::Calorimetry>>();
}

void calo::TrackCalorimetry::produce(art::Event& e)
{
  art::Handle<std::vector<recob::Track>> trackHandle;
  e.getByLabel(fTrackModuleLabel, trackHandle);
  std::vector<recob::Track> const& trackVector(*trackHandle);

  // Get Hits from event.
  art::Handle<std::vector<recob::Hit>> hitHandle;
  e.getByLabel(fHitModuleLabel, hitHandle);
  std::vector<recob::Hit> const& hitVector(*hitHandle);

  // Get track<-->hit associations
  art::Handle<art::Assns<recob::Track, recob::Hit>> assnTrackHitHandle;
  e.getByLabel(fTrackModuleLabel, assnTrackHitHandle);
  std::vector<std::vector<size_t>> hit_indices_per_track =
    util::GetAssociatedVectorManyI(assnTrackHitHandle, trackHandle);

  // Make the container for the calo product to put onto the event.
  std::unique_ptr<std::vector<anab::Calorimetry>> caloPtr(new std::vector<anab::Calorimetry>);
  std::vector<anab::Calorimetry>& caloVector(*caloPtr);

  // Make a container for the track<-->calo associations.
  // One entry per track, with entry equal to index in calorimetry collection of
  // associated object.
  std::vector<size_t> assnTrackCaloVector;
  std::unique_ptr<art::Assns<recob::Track, anab::Calorimetry>> assnTrackCaloPtr(
    new art::Assns<recob::Track, anab::Calorimetry>);

  auto const clock_data = art::ServiceHandle<detinfo::DetectorClocksService const>()->DataFor(e);
  auto const det_prop =
    art::ServiceHandle<detinfo::DetectorPropertiesService const>()->DataFor(e, clock_data);
  fTrackCaloAlg.ExtractCalorimetry(clock_data,
                                   det_prop,
                                   trackVector,
                                   hitVector,
                                   hit_indices_per_track,
                                   caloVector,
                                   assnTrackCaloVector,
                                   art::ServiceHandle<geo::WireReadout>()->Get());

  //Make the associations for ART
  for (size_t calo_iter = 0; calo_iter < assnTrackCaloVector.size(); calo_iter++) {
    if (assnTrackCaloVector[calo_iter] == std::numeric_limits<size_t>::max()) continue;
    art::Ptr<recob::Track> trk_ptr(trackHandle, assnTrackCaloVector[calo_iter]);
    util::CreateAssn(e, caloVector, trk_ptr, *assnTrackCaloPtr, calo_iter);
  }

  e.put(std::move(caloPtr));
  e.put(std::move(assnTrackCaloPtr));
}

DEFINE_ART_MODULE(calo::TrackCalorimetry)
