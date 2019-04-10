/**
 * @file   fuzzyCluster_module.cc
 * @brief  Cluster based on Hough transform, with pre-clustering and post-merge
 * @author kinga.partyka@yale.edu , bcarls@fnal.gov
 */

#include <vector>
#include <cmath>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include "TH1.h"

// Framework includes
#include "art/Framework/Core/EDProducer.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Principal/Handle.h"
#include "art/Framework/Services/Registry/ServiceHandle.h"
#include "art/Framework/Services/Optional/TFileService.h"
#include "art/Framework/Services/Optional/TFileDirectory.h"
#include "canvas/Persistency/Common/Ptr.h"
#include "canvas/Persistency/Common/PtrVector.h"
#include "fhiclcpp/ParameterSet.h"
#include "messagefacility/MessageLogger/MessageLogger.h"

// nutools
#include "nutools/RandomUtils/NuRandomService.h"

// LArSoft includes
#include "lardataobj/RawData/raw.h"
#include "lardataobj/RawData/RawDigit.h"
#include "larreco/RecoAlg/fuzzyClusterAlg.h"
#include "larevt/CalibrationDBI/Interface/ChannelStatusService.h"
#include "larevt/CalibrationDBI/Interface/ChannelStatusProvider.h"
#include "larcore/Geometry/Geometry.h"
#include "larcorealg/Geometry/CryostatGeo.h"
#include "larcorealg/Geometry/TPCGeo.h"
#include "larcorealg/Geometry/PlaneGeo.h"
#include "lardataobj/RecoBase/Cluster.h"
#include "lardataobj/RecoBase/Hit.h"
#include "lardata/Utilities/AssociationUtil.h"
#include "larreco/RecoAlg/ClusterRecoUtil/StandardClusterParamsAlg.h"
#include "larreco/RecoAlg/ClusterParamsImportWrapper.h"
#include "larreco/ClusterFinder/ClusterCreator.h"


class TH1F;

namespace cluster{
   
  //--------------------------------------------------------------- 
  class fuzzyCluster : public art::EDProducer
  {
  public:
    explicit fuzzyCluster(fhicl::ParameterSet const& pset); 
    void produce(art::Event& evt) override;
    void beginJob() override;
    
  private:
       
    TH1F *fhitwidth{nullptr};
    TH1F *fhitwidth_ind_test{nullptr};
    TH1F *fhitwidth_coll_test{nullptr};
  
    std::string fhitsModuleLabel;
    unsigned int fHoughSeed;
   
    fuzzyClusterAlg ffuzzyCluster; ///< object that implements the fuzzy cluster algorithm
    CLHEP::HepRandomEngine& fEngine;
  };

}

namespace cluster{

  //-------------------------------------------------
  fuzzyCluster::fuzzyCluster(fhicl::ParameterSet const& pset) :
    EDProducer{pset},
    fhitsModuleLabel{pset.get< std::string >("HitsModuleLabel")},
    fHoughSeed{pset.get< unsigned int >("HoughSeed")},
    ffuzzyCluster{pset.get< fhicl::ParameterSet >("fuzzyClusterAlg")},
    // create a default random engine; obtain the random seed from NuRandomService,
    // unless overridden in configuration with key "Seed"
    fEngine(art::ServiceHandle<rndm::NuRandomService>{}->createEngine(*this, pset, "Seed"))
  {  
    ffuzzyCluster.reconfigure(pset.get< fhicl::ParameterSet >("fuzzyClusterAlg"));
    produces< std::vector<recob::Cluster> >();  
    produces< art::Assns<recob::Cluster, recob::Hit> >();
  }
  
  //-------------------------------------------------
  void fuzzyCluster::beginJob()
  {
    // get access to the TFile service
    art::ServiceHandle<art::TFileService const> tfs;
  
    fhitwidth= tfs->make<TH1F>(" fhitwidth","width of hits in cm", 50000,0 ,5  );
    fhitwidth_ind_test= tfs->make<TH1F>("fhitwidth_ind_test","width of hits in cm", 50000,0 ,5  );
    fhitwidth_coll_test= tfs->make<TH1F>("fhitwidth_coll_test","width of hits in cm", 50000,0 ,5  );
  }
  
  //-----------------------------------------------------------------
  void fuzzyCluster::produce(art::Event& evt)
  {
    auto ccol = std::make_unique<std::vector<recob::Cluster>>();
    auto assn = std::make_unique<art::Assns<recob::Cluster, recob::Hit>>();
   
    // loop over all hits in the event and look for clusters (for each plane)
    std::vector<art::Ptr<recob::Hit> > allhits;

    // If a nonzero random number seed has been provided, 
    // overwrite the seed already initialized
    if(fHoughSeed != 0){
      fEngine.setSeed(fHoughSeed,0);
    } 

    // get the ChannelFilter
    // get channel quality service:
    lariov::ChannelStatusProvider const& channelStatus
      = art::ServiceHandle<lariov::ChannelStatusService const>()->GetProvider();
    
    lariov::ChannelStatusProvider::ChannelSet_t const BadChannels
      = channelStatus.BadChannels();
    
    // prepare the algorithm to compute the cluster characteristics;
    // we use the "standard" one here; configuration would happen here,
    // but we are using the default configuration for that algorithm
    ClusterParamsImportWrapper<StandardClusterParamsAlg> ClusterParamAlgo;
    
    auto const hitcol = evt.getValidHandle<std::vector<recob::Hit>>(fhitsModuleLabel);

    // make a map of the geo::PlaneID to vectors of art::Ptr<recob::Hit>
    std::map<geo::PlaneID, std::vector< art::Ptr<recob::Hit> > > planeIDToHits;
    for(size_t i = 0; i < hitcol->size(); ++i) {
      planeIDToHits[hitcol->at(i).WireID().planeID()].emplace_back(hitcol, i);
    }

    art::ServiceHandle<geo::Geometry const> geom;

    for(auto & itr : planeIDToHits){
      
      geo::SigType_t sigType = geom->SignalType(itr.first);
      allhits.resize(itr.second.size());
      allhits.swap(itr.second);
      
      //Begin clustering with fuzzy
      
      ffuzzyCluster.InitFuzzy(allhits, BadChannels);
      
      //----------------------------------------------------------------
      for(unsigned int j = 0; j < ffuzzyCluster.fps.size(); ++j){
  	
	if(allhits.size() != ffuzzyCluster.fps.size()) break;
  	
	fhitwidth->Fill(ffuzzyCluster.fps[j][2]);
  	
	if(sigType == geo::kInduction)  fhitwidth_ind_test->Fill(ffuzzyCluster.fps[j][2]);
	if(sigType == geo::kCollection) fhitwidth_coll_test->Fill(ffuzzyCluster.fps[j][2]);
      }
      
      //*******************************************************************
      ffuzzyCluster.run_fuzzy_cluster(allhits, fEngine);
      
      //End clustering with fuzzy
      
      
      for(size_t i = 0; i < ffuzzyCluster.fclusters.size(); ++i){
	std::vector<art::Ptr<recob::Hit> > clusterHits;
  	
	for(size_t j = 0; j < ffuzzyCluster.fpointId_to_clusterId.size(); ++j){
	  if(ffuzzyCluster.fpointId_to_clusterId[j]==i){ 
	    clusterHits.push_back(allhits[j]);
	  }
	} 
	
	
	////////
	if (!clusterHits.empty()){
	  /// \todo: need to define start and end positions for this cluster and slopes for dTdW, dQdW
	  const geo::WireID& wireID = clusterHits.front()->WireID();
	  unsigned int sw = wireID.Wire;
	  unsigned int ew = clusterHits.back()->WireID().Wire;
	  
	  // feed the algorithm with all the cluster hits
	  ClusterParamAlgo.ImportHits(clusterHits);
	  
	  // create the recob::Cluster directly in the vector
	  ClusterCreator cluster(
	    ClusterParamAlgo,                     // algo
	    float(sw),                            // start_wire
	    0.,                                   // sigma_start_wire
	    clusterHits.front()->PeakTime(),      // start_tick
	    clusterHits.front()->SigmaPeakTime(), // sigma_start_tick
	    float(ew),                            // end_wire
	    0.,                                   // sigma_end_wire,
	    clusterHits.back()->PeakTime(),       // end_tick
	    clusterHits.back()->SigmaPeakTime(),  // sigma_end_tick
	    ccol->size(),                         // ID
	    clusterHits.front()->View(),          // view
	    wireID.planeID(),                     // plane
	    recob::Cluster::Sentry                // sentry
	    );
	  
	  ccol->emplace_back(cluster.move());
	  
	  // associate the hits to this cluster
	  util::CreateAssn(*this, evt, *ccol, clusterHits, *assn);
  	  
	  clusterHits.clear();
  	  
	}//end if clusterHits has at least one hit
     
      }//end loop over fclusters
  	
      allhits.clear();
    } // end loop over map
  
    mf::LogVerbatim("Summary") << std::setfill('-') << std::setw(175) << "-" << std::setfill(' ');
    mf::LogVerbatim("Summary") << "fuzzyCluster Summary:";
    for(size_t i = 0; i<ccol->size(); ++i) mf::LogVerbatim("Summary") << ccol->at(i) ;
  
    evt.put(move(ccol));
    evt.put(move(assn));
  } // end produce
  
} // end namespace

DEFINE_ART_MODULE(cluster::fuzzyCluster)
