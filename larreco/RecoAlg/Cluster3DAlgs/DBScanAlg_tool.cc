/**
 *  @file   Cluster3D_module.cc
 * 
 *  @brief  Producer module to create 3D clusters from input hits
 * 
 */

// Framework Includes
#include "art/Utilities/ToolMacros.h"
#include "cetlib/search_path.h"
#include "cetlib/cpu_timer.h"

#include "larreco/RecoAlg/Cluster3DAlgs/IClusterAlg.h"

// LArSoft includes
#include "larreco/RecoAlg/Cluster3DAlgs/ClusterParamsBuilder.h"
#include "larreco/RecoAlg/Cluster3DAlgs/kdTree.h"
#include "lardata/Utilities/AssociationUtil.h"
#include "lardataobj/RecoBase/Hit.h"
#include "lardata/RecoObjects/Cluster3D.h"

// std includes
#include <string>
#include <functional>
#include <iostream>
#include <memory>

//------------------------------------------------------------------------------------------------------------------------------------------
// implementation follows

namespace lar_cluster3d {

/**
 *  @brief  DBScanAlg class definiton
 */
class DBScanAlg : virtual public IClusterAlg
{
public:
    /**
     *  @brief  Constructor
     *
     *  @param  pset
     */
    explicit DBScanAlg(fhicl::ParameterSet const &pset);
    
    /**
     *  @brief  Destructor
     */
    ~DBScanAlg();
    
    void configure(const fhicl::ParameterSet&) override;
    
    /**
     *  @brief Given a set of recob hits, run DBscan to form 3D clusters
     *
     *  @param hitPairList           The input list of 3D hits to run clustering on
     *  @param clusterParametersList A list of cluster objects (parameters from associated hits)
     */
    void Cluster3DHits(reco::HitPairList&           hitPairList,
                       reco::ClusterParametersList& clusterParametersList) const override;
    
    /**
     *  @brief If monitoring, recover the time to execute a particular function
     */
    float getTimeToExecute(IClusterAlg::TimeValues index) const override {return m_timeVector.at(index);}
    
private:
    
    /**
     *  @brief the main routine for DBScan
     */
    void expandCluster(const kdTree::KdTreeNode&,
                       kdTree::CandPairList&,
                       reco::HitPairListPtr&,
                       size_t) const;
    
    /**
     *  @brief Data members to follow
     */
    bool                       m_enableMonitoring;      ///<
    size_t                     m_minPairPts;
    mutable std::vector<float> m_timeVector;            ///<
    
    ClusterParamsBuilder       m_clusterBuilder;        // Common cluster builder tool
    kdTree                     m_kdTree;                // For the kdTree
};

DBScanAlg::DBScanAlg(fhicl::ParameterSet const &pset) :
    m_clusterBuilder(pset.get<fhicl::ParameterSet>("ClusterParamsBuilder")),
    m_kdTree(pset.get<fhicl::ParameterSet>("kdTree"))
{
    this->configure(pset);
}

//------------------------------------------------------------------------------------------------------------------------------------------

DBScanAlg::~DBScanAlg()
{
}

//------------------------------------------------------------------------------------------------------------------------------------------

void DBScanAlg::configure(fhicl::ParameterSet const &pset)
{
    m_enableMonitoring         = pset.get<bool>  ("EnableMonitoring",  true  );
    m_minPairPts               = pset.get<size_t>("MinPairPts",        2     );
}
    
void DBScanAlg::Cluster3DHits(reco::HitPairList&           hitPairList,
                              reco::ClusterParametersList& clusterParametersList) const
{
    /**
     *  @brief Driver for processing input 2D hits, transforming to 3D hits and building lists
     *         of associated 3D hits (candidate 3D clusters)
     */
    cet::cpu_timer theClockDBScan;
    
    m_timeVector.resize(NUMTIMEVALUES, 0.);
    
    // DBScan is driven of its "epsilon neighborhood". Computing adjacency within DBScan can be time
    // consuming so the idea is the prebuild the adjaceny map and then run DBScan.
    // We'll employ a kdTree to implement this scheme
    kdTree::KdTreeNodeList kdTreeNodeContainer;
    kdTree::KdTreeNode     topNode = m_kdTree.BuildKdTree(hitPairList, kdTreeNodeContainer);
    
    if (m_enableMonitoring) m_timeVector.at(BUILDHITTOHITMAP) = m_kdTree.getTimeToExecute();
    
    if (m_enableMonitoring) theClockDBScan.start();
    
    // Ok, here we go!
    // The idea is to loop through all of the input 3D hits and do the clustering
    for(const auto& hit : hitPairList)
    {
        // Check if the hit has already been visited
        if (hit->getStatusBits() & reco::ClusterHit3D::CLUSTERVISITED) continue;
        
        // Mark as visited
        hit->setStatusBit(reco::ClusterHit3D::CLUSTERVISITED);
        
        // Find the neighborhood for this hit
        kdTree::CandPairList candPairList;
        float                bestDistance(std::numeric_limits<float>::max());
        
        m_kdTree.FindNearestNeighbors(hit.get(), topNode, candPairList, bestDistance);
        
        if (candPairList.size() < m_minPairPts)
        {
            hit->setStatusBit(reco::ClusterHit3D::CLUSTERNOISE);
        }
        else
        {
            // "Create" a new cluster and get a reference to it
            clusterParametersList.push_back(reco::ClusterParameters());
            
            reco::HitPairListPtr& curCluster = clusterParametersList.back().getHitPairListPtr();
            
            hit->setStatusBit(reco::ClusterHit3D::CLUSTERATTACHED);
            curCluster.push_back(hit.get());
            
            // expand the cluster
            expandCluster(topNode, candPairList, curCluster, m_minPairPts);
        }
    }

    if (m_enableMonitoring)
    {
        theClockDBScan.stop();
        
        m_timeVector[RUNDBSCAN] = theClockDBScan.accumulated_real_time();
    }
    
    // Initial clustering is done, now trim the list and get output parameters
    cet::cpu_timer theClockBuildClusters;
    
    // Start clocks if requested
    if (m_enableMonitoring) theClockBuildClusters.start();
    
    m_clusterBuilder.BuildClusterInfo(clusterParametersList);
    
    if (m_enableMonitoring)
    {
        theClockBuildClusters.stop();
        
        m_timeVector[BUILDCLUSTERINFO] = theClockBuildClusters.accumulated_real_time();
    }
    
    mf::LogDebug("Cluster3D") << ">>>>> DBScan done, found " << clusterParametersList.size() << " clusters" << std::endl;
    
    return;
}
    
void DBScanAlg::expandCluster(const kdTree::KdTreeNode& topNode,
                              kdTree::CandPairList&     candPairList,
                              reco::HitPairListPtr&     cluster,
                              size_t                    minPts) const
{
    // This is the main inside loop for the DBScan based clustering algorithm
    
    // Loop over added hits until list has been exhausted
    while(!candPairList.empty())
    {
        // Dereference the point so we can see in the debugger...
        const reco::ClusterHit3D* neighborHit = candPairList.front().second;
        
        // Process if we've not been here before
        if (!(neighborHit->getStatusBits() & reco::ClusterHit3D::CLUSTERVISITED))
        {
            // set as visited
            neighborHit->setStatusBit(reco::ClusterHit3D::CLUSTERVISITED);
            
            // get the neighborhood around this point
            kdTree::CandPairList neighborCandPairList;
            float                bestDistance(std::numeric_limits<float>::max());
            
            m_kdTree.FindNearestNeighbors(neighborHit, topNode, neighborCandPairList, bestDistance);

            // If the epsilon neighborhood of this point is large enough then add its points to our list
            if (neighborCandPairList.size() >= minPts)
            {
                std::copy(neighborCandPairList.begin(),neighborCandPairList.end(),std::back_inserter(candPairList));
            }
        }
        
        // If the point is not yet in a cluster then we now add
        if (!(neighborHit->getStatusBits() & reco::ClusterHit3D::CLUSTERATTACHED))
        {
            neighborHit->setStatusBit(reco::ClusterHit3D::CLUSTERATTACHED);
            cluster.push_back(neighborHit);
        }
        
        candPairList.pop_front();
    }
    
    return;
}
    
//------------------------------------------------------------------------------------------------------------------------------------------
    
DEFINE_ART_CLASS_TOOL(DBScanAlg)
} // namespace lar_cluster3d
