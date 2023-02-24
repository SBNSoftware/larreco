////////////////////////////////////////////////////////////////////////
// Class:       MCBTDemo
// Module Type: analyzer
// File:        MCBTDemo_module.cc
//
// Generated at Thu Jan  8 08:16:24 2015 by Kazuhiro Terao using artmod
// from cetpkgsupport v1_08_02.
////////////////////////////////////////////////////////////////////////

#include "art/Framework/Core/EDAnalyzer.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Principal/Handle.h"
#include "art/Framework/Services/Registry/ServiceHandle.h"
#include "canvas/Persistency/Common/FindManyP.h"

#include "larcore/Geometry/WireReadout.h"
#include "lardata/DetectorInfoServices/DetectorClocksService.h"
#include "lardataobj/MCBase/MCTrack.h"
#include "lardataobj/RecoBase/Hit.h"
#include "lardataobj/RecoBase/Track.h"
#include "lardataobj/Simulation/SimChannel.h"
#include "larreco/MCComp/MCBTAlg.h"
#include <iostream>

class MCBTDemo : public art::EDAnalyzer {
public:
  explicit MCBTDemo(fhicl::ParameterSet const& p);

  // Plugins should not be copied or assigned.
  MCBTDemo(MCBTDemo const&) = delete;
  MCBTDemo(MCBTDemo&&) = delete;
  MCBTDemo& operator=(MCBTDemo const&) = delete;
  MCBTDemo& operator=(MCBTDemo&&) = delete;

private:
  // Required functions.
  void analyze(art::Event const& e) override;
};

MCBTDemo::MCBTDemo(fhicl::ParameterSet const& p) : EDAnalyzer(p) {}

void MCBTDemo::analyze(art::Event const& e)
{
  // Implementation of required member function here.
  art::Handle<std::vector<sim::MCTrack>> mctHandle;
  e.getByLabel("mcreco", mctHandle);

  art::Handle<std::vector<sim::SimChannel>> schHandle;
  e.getByLabel("largeant", schHandle);

  art::Handle<std::vector<recob::Track>> trkHandle;
  e.getByLabel("trackkalmanhit", trkHandle);

  if (!mctHandle.isValid() || !schHandle.isValid() || !trkHandle.isValid()) return;

  // Collect G4 track ID from MCTrack whose energy loss > 100 MeV inside the detector
  std::vector<unsigned int> g4_track_id;
  for (auto const& mct : *mctHandle) {

    if (!mct.size()) continue;

    double dE = (*mct.begin()).Momentum().E() - (*mct.rbegin()).Momentum().E();
    if (dE > 100) g4_track_id.push_back(mct.TrackID());
  }

  if (g4_track_id.size()) {

    auto const& wireReadoutGeom = art::ServiceHandle<geo::WireReadout const>()->Get();
    btutil::MCBTAlg alg_mct(g4_track_id, *schHandle);

    auto sum_mcq_v = alg_mct.MCQSum(2);
    std::cout << "Total charge contents on W plane:" << std::endl;
    for (size_t i = 0; i < sum_mcq_v.size() - 1; ++i)
      std::cout << " MCTrack " << i << " => " << sum_mcq_v[i] << std::endl;
    std::cout << " Others => " << (*sum_mcq_v.rbegin()) << std::endl;

    // Loop over reconstructed tracks and find charge fraction
    art::FindManyP<recob::Hit> hit_coll_v(trkHandle, e, "trackkalmanhit");
    auto const clockData = art::ServiceHandle<detinfo::DetectorClocksService const>()->DataFor(e);

    for (size_t i = 0; i < trkHandle->size(); ++i) {

      const std::vector<art::Ptr<recob::Hit>> hit_coll = hit_coll_v.at(i);

      std::vector<btutil::WireRange_t> hits;

      for (auto const& h_ptr : hit_coll) {

        if (wireReadoutGeom.ChannelToWire(h_ptr->Channel())[0].Plane != ::geo::kW) continue;

        hits.emplace_back(h_ptr->Channel(), h_ptr->StartTick(), h_ptr->EndTick());
      }

      auto mcq_v = alg_mct.MCQ(clockData, hits);
      auto mcq_frac_v = alg_mct.MCQFrac(clockData, hits);

      std::cout << "Track " << i << " "
                << "Y plane Charge from first MCTrack: " << mcq_v[0]
                << " ... Purity: " << mcq_frac_v[0]
                << " ... Efficiency: " << mcq_v[0] / sum_mcq_v[0] << std::endl;
    }
  }
}

DEFINE_ART_MODULE(MCBTDemo)
