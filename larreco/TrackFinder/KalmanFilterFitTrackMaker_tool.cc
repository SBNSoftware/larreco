#include "art/Utilities/ToolMacros.h"
#include "art/Utilities/ToolConfigTable.h"
#include "fhiclcpp/types/Atom.h"
#include "fhiclcpp/types/Sequence.h"

#include "art/Framework/Principal/Handle.h"
#include "messagefacility/MessageLogger/MessageLogger.h"

#include "larreco/TrackFinder/TrackMaker.h"
#include "larreco/RecoAlg/TrackKalmanFitter.h"
#include "larreco/RecoAlg/TrackCreationBookKeeper.h"

#include "larreco/RecoAlg/TrajectoryMCSFitter.h"
#include "lardataobj/RecoBase/MCSFitResult.h"

#include "lardataobj/AnalysisBase/CosmicTag.h"
#include "lardataobj/AnalysisBase/ParticleID.h"
#include "larreco/RecoAlg/TrackMomentumCalculator.h"

namespace trkmkr {

  /**
   * @file larreco/TrackFinder/KalmanFilterFitTrackMaker_tool.cc
   * @class trkmkr::KalmanFilterFitTrackMaker
   *
   * @brief Concrete implementation of a tool to fit tracks with TrackKalmanFitter.
   *
   * Concrete implementation of a tool to fit tracks with trkf::TrackKalmanFitter; inherits from abstract class TrackMaker.
   * It prepares the input needed by the fitter (momentum, particleId, direction), and returns a track with all outputs filled.
   * If the flag keepInputTrajectoryPoints is set to true, the tracjetory points from the input track are copied into the output,
   * so that only the covariance matrices, the chi2 and the ndof in the output track are resulting from the fit.
   *
   * For configuration options see KalmanFilterFitTrackMaker#Options and KalmanFilterFitTrackMaker#Config.
   *
   * @author  G. Cerati (FNAL, MicroBooNE)
   * @date    2017
   * @version 1.0
   */

  class KalmanFilterFitTrackMaker : public TrackMaker {

  public:

    struct Options {
      using Name = fhicl::Name;
      using Comment = fhicl::Comment;
      //
      fhicl::Atom<double> defaultMomInGeV {
	Name("defaultMomInGeV"),
	Comment("Default momentum estimate value (all other options are set to false, or if the estimate is not available)."),
	1.0
      };
      fhicl::Atom<bool> momFromMCS {
        Name("momFromMCS"),
	Comment("Flag used to get initial momentum estimate from MCSFitResult."),
	false
      };
      fhicl::Atom<art::InputTag> mcsInputTag {
        Name("mcsInputTag"),
        Comment("InputTag of MCSFitResult collection.")
      };
      fhicl::Atom<bool> momFromTagAndPid {
        Name("momFromTagAndPid"),
	Comment("Flag used to get initial momentum estimate from either range or mcs fit, based on particle id (from pidInputTag) and containement (from contInputTag)."),
	false
      };
      fhicl::Atom<art::InputTag> contInputTag {
        Name("contInputTag"),
        Comment("InputTag of CosmicTag collection for containement.")
      };
      fhicl::Atom<art::InputTag> pidInputTag {
        Name("pidInputTag"),
        Comment("InputTag of ParticleID collection.")
      };
      fhicl::Atom<int> defaultPdgId {
	Name("defaultPdgId"),
	Comment("Default particle id hypothesis (all other options are set to false, or if the estimate is not available)."),
	13
      };
      fhicl::Atom<bool> dirFromVec {
        Name("dirFromVec"),
	Comment("Assume track direction as the one giving positive dot product with vector specified by dirVec."),
	false
      };
      fhicl::Sequence<float,3u> dirVec {
        Name("dirVec"),
        Comment("Fhicl sequence defining the vector used when dirFromVec=true. It must have 3 elements.")
      };
      fhicl::Atom<bool> alwaysInvertDir {
	Name("alwaysInvertDir"),
	Comment("If true, fit all tracks from end to vertex assuming inverted direction."),
	false
      };
      fhicl::Atom<bool> keepInputTrajectoryPoints {
        Name("keepInputTrajectoryPoints"),
	Comment("Option to keep positions and directions from input trajectory/track. The fit will provide only covariance matrices, chi2, ndof, particle Id and absolute momentum. It may also modify the trajectory point flags. In order to avoid inconsistencies, it has to be used with the following fitter options all set to false: sortHitsByPlane, sortOutputHitsMinLength, skipNegProp."),
	false
      };
      //
    };

    struct Config {
      using Name = fhicl::Name;
      fhicl::Table<KalmanFilterFitTrackMaker::Options> options {
	Name("options")
      };
      fhicl::Table<trkf::TrackStatePropagator::Config> propagator {
	Name("propagator")
      };
      fhicl::Table<trkf::TrackKalmanFitter::Config> fitter {
	Name("fitter")
      };
      fhicl::Table<trkf::TrajectoryMCSFitter::Config> mcsfit {
	Name("mcsfit")
      };
    };
    using Parameters = art::ToolConfigTable<Config>;

    /// Constructor from Parameters
    explicit KalmanFilterFitTrackMaker(Parameters const& p) : p_(p)
    {
      prop = new trkf::TrackStatePropagator(p_().propagator);
      kalmanFitter = new trkf::TrackKalmanFitter(prop,p_().fitter);
      mcsfitter = new trkf::TrajectoryMCSFitter(p_().mcsfit);
      mom_def_ = p_().options().defaultMomInGeV();
      momFromMCS_ = p_().options().momFromMCS();
      if (momFromMCS_) {
	mcsInputTag_ = p_().options().mcsInputTag();
      }
      momFromTagAndPid_ = p_().options().momFromTagAndPid();
      if (momFromTagAndPid_) {
	contInputTag_ = p_().options().contInputTag();
	pidInputTag_ = p_().options().pidInputTag();
      }
      pid_def_ = p_().options().defaultPdgId();
      alwaysFlip_ = p_().options().alwaysInvertDir();
      //
      if ( p_().options().keepInputTrajectoryPoints() && (p_().fitter().sortHitsByPlane() || p_().fitter().sortOutputHitsMinLength() || p_().fitter().skipNegProp()) ) {
	throw cet::exception("KalmanFilterFitTrackMaker")
	  << "Incompatible configuration parameters: keepInputTrajectoryPoints needs the following fitter options all set to false: sortHitsByPlane, sortOutputHitsMinLength, skipNegProp." << "\n";
      }
      //
    }

    /// Destructor
    ~KalmanFilterFitTrackMaker() {
      delete prop;
      delete kalmanFitter;
      delete mcsfitter;
    }

    /// initialize event: get collection of recob::MCSFitResult
    virtual void initEvent(const art::Event& e) override {
      if (momFromMCS_) mcs = e.getValidHandle<std::vector<recob::MCSFitResult> >(mcsInputTag_).product();
      if (momFromTagAndPid_) {
	cont = e.getValidHandle<std::vector<anab::CosmicTag> >(contInputTag_).product();
	pid = e.getValidHandle<std::vector<anab::ParticleID> >(pidInputTag_).product();
      }
      return;
    }

    /// function that actually calls the fitter
    bool makeTrackImpl(const recob::TrackTrajectory& traj, const int tkID, const std::vector<art::Ptr<recob::Hit> >& inHits,
		       const SMatrixSym55& covVtx, const SMatrixSym55& covEnd,
		       recob::Track& outTrack, std::vector<art::Ptr<recob::Hit> >& outHits, OptionalOutputs& optionals) const;

    /// override of TrackMaker purely virtual function with recob::TrackTrajectory as argument
    bool makeTrack(const recob::TrackTrajectory& traj, const int tkID, const std::vector<art::Ptr<recob::Hit> >& inHits,
		   recob::Track& outTrack, std::vector<art::Ptr<recob::Hit> >& outHits, OptionalOutputs& optionals) const override {
      return makeTrackImpl(traj, tkID, inHits, trkf::SMatrixSym55(), trkf::SMatrixSym55(), outTrack, outHits, optionals);
    }

    /// override of TrackMaker virtual function with recob::Track as argument
    bool makeTrack(const recob::Track& track, const std::vector<art::Ptr<recob::Hit> >& inHits,
		   recob::Track& outTrack, std::vector<art::Ptr<recob::Hit> >& outHits, OptionalOutputs& optionals) const override {
      auto covs = track.Covariances();
      return makeTrackImpl(track.Trajectory(), track.ID(), inHits, covs.first, covs.second, outTrack, outHits, optionals);
    }

    /// set the initial momentum estimate
    double setMomentum  (const recob::TrackTrajectory& traj, const int tkID) const;
    /// set the particle ID hypothesis
    int    setParticleID(const recob::TrackTrajectory& traj, const int tkID) const;
    /// decide whether to flip the direction or not
    bool   isFlipDirection (const recob::TrackTrajectory& traj, const int tkID) const;

    /// restore the TrajectoryPoints in the Track to be the same as those in the input TrackTrajectory (but keep covariance matrices and chi2 from fit).
    void restoreInputPoints(const recob::TrackTrajectory& traj, const std::vector<art::Ptr<recob::Hit> >& inHits,
			    recob::Track& outTrack, std::vector<art::Ptr<recob::Hit> >& outHits, OptionalOutputs& optionals) const;

  private:
    Parameters p_;
    const trkf::TrackKalmanFitter* kalmanFitter;
    const trkf::TrackStatePropagator* prop;
    const trkf::TrajectoryMCSFitter* mcsfitter;
    double mom_def_;
    bool   momFromMCS_;
    art::InputTag mcsInputTag_;
    bool momFromTagAndPid_;
    art::InputTag contInputTag_;
    art::InputTag pidInputTag_;
    int    pid_def_;
    bool   alwaysFlip_;
    const std::vector<recob::MCSFitResult>* mcs = nullptr;
    const std::vector<anab::CosmicTag>* cont = nullptr;
    const std::vector<anab::ParticleID>* pid = nullptr;
    mutable trkf::TrackMomentumCalculator tmc; //fixme, get rid of the mutable (or of tmc!)
  };

}

bool trkmkr::KalmanFilterFitTrackMaker::makeTrackImpl(const recob::TrackTrajectory& traj, const int tkID, const std::vector<art::Ptr<recob::Hit> >& inHits,
						      const SMatrixSym55& covVtx, const SMatrixSym55& covEnd,
						      recob::Track& outTrack, std::vector<art::Ptr<recob::Hit> >& outHits, OptionalOutputs& optionals) const {
  //
  const double mom = setMomentum(traj, tkID);// what about uncertainty?
  const int    pid = setParticleID(traj, tkID);
  const bool   flipDirection = isFlipDirection(traj, tkID);
  std::cout << "fitting track with mom=" << mom << " pid=" << pid << " flip=" << flipDirection << " start pos=" << traj.Start() << " dir=" << traj.StartDirection() << std::endl;
  bool fitok = kalmanFitter->fitTrack(traj, tkID, covVtx, covEnd, inHits, mom, pid, flipDirection, outTrack, outHits, optionals);
  if (!fitok) return false;
  //
  std::cout << "fitted track with mom=" << outTrack.StartMomentum() << " pid=" << outTrack.ParticleId() << " flip=" << flipDirection << " start pos=" << outTrack.Start() << " dir=" << outTrack.StartDirection() << " nchi2=" << outTrack.Chi2PerNdof() << std::endl;
  //
  if (p_().options().keepInputTrajectoryPoints()) {
    restoreInputPoints(traj,inHits,outTrack,outHits,optionals);
  }
  //
  return true;
}

double trkmkr::KalmanFilterFitTrackMaker::setMomentum(const recob::TrackTrajectory& traj, const int tkID) const {
  double mom = mom_def_;
  if (momFromMCS_) {
    double mcsmom = mcs->at(tkID).fwdMomentum();
    //make sure the mcs fit converged
    if (mcsmom>0.01 && mcsmom<7.) mom = mcsmom;
  }
  if (momFromTagAndPid_) {
    bool isContained = cont->at(tkID).CosmicType()==anab::kNotTagged;
    int pid = setParticleID(traj, tkID);
    std::cout << "pid=" << pid << std::endl;
    // for now momentum from range implemented only for muons and protons
    // so treat pions as muons (~MIPs) and kaons as protons
    int pidtmp = pid;
    if (pidtmp==211 || pidtmp==13) pidtmp = 13;
    else pidtmp = 2212;
    mom = tmc.GetTrackMomentum(traj.Length(),pidtmp);
    std::cout << "range mom=" << mom << std::endl;
    if (isContained==false) {
      double mcsmom = mcsfitter->fitMcs(traj,pid).fwdMomentum();
      std::cout << "mcs mom=" << mcsmom << std::endl;
      //make sure the mcs fit converged, also the mcsmom should not be less than the range!
      if (mcsmom>0.01 && mcsmom<7. && mcsmom>mom) mom = mcsmom;
    }
  }
  std::cout << "resulting mom=" << mom << std::endl;
  return mom;
}

int trkmkr::KalmanFilterFitTrackMaker::setParticleID(const recob::TrackTrajectory& traj, const int tkID) const {
  if (momFromTagAndPid_) {
    int idfrompid = pid->at(tkID).Pdg();
    // std::cout << "pid=" << idfrompid << " pida=" << pid->at(tkID).PIDA() << std::endl;
    if (idfrompid!=0) return idfrompid;
  }
  return pid_def_;
}

bool trkmkr::KalmanFilterFitTrackMaker::isFlipDirection(const recob::TrackTrajectory& traj, const int tkID) const {
  if (alwaysFlip_) {
    return true;
  } else if (p_().options().dirFromVec()) {
    std::array<float, 3> dir = p_().options().dirVec();
    auto tdir =  traj.VertexDirection();
    if ( (dir[0]*tdir.X() + dir[1]*tdir.Y() + dir[2]*tdir.Z())<0. ) return true;
  }
  return false;
}

void trkmkr::KalmanFilterFitTrackMaker::restoreInputPoints(const recob::TrackTrajectory& traj, const std::vector<art::Ptr<recob::Hit> >& inHits,
							   recob::Track& outTrack, std::vector<art::Ptr<recob::Hit> >& outHits, OptionalOutputs& optionals) const {
  if (optionals.isTrackFitInfosInit()) {
    throw cet::exception("KalmanFilterFitTrackMaker")
      << "Option keepInputTrajectoryPoints not compatible with doTrackFitHitInfo, please set doTrackFitHitInfo to false in the track producer.\n";
  }
  const auto np = outTrack.NumberTrajectoryPoints();
  trkmkr::TrackCreationBookKeeper tcbk(outHits, optionals, outTrack.ID(), outTrack.ParticleId(), traj.HasMomentum());
  //
  std::vector<unsigned int> flagsmap(np,-1);
  for (unsigned int i=0; i<np; ++i) flagsmap[outTrack.FlagsAtPoint(i).fromHit()] = i;
  //
  for (unsigned int p=0; p<np; ++p) {
    auto mask = outTrack.FlagsAtPoint(flagsmap[p]).mask();
    if (mask.isSet(recob::TrajectoryPointFlagTraits::NoPoint)) mask.unset(recob::TrajectoryPointFlagTraits::NoPoint);
    tcbk.addPoint(traj.LocationAtPoint(p), traj.MomentumVectorAtPoint(p), inHits[p], recob::TrajectoryPointFlags(p,mask), 0);
  }
  auto covs = outTrack.Covariances();
  tcbk.setTotChi2(outTrack.Chi2());
  outTrack = tcbk.finalizeTrack(std::move(covs.first),std::move(covs.second));
  //
}

DEFINE_ART_CLASS_TOOL(trkmkr::KalmanFilterFitTrackMaker)
