/// @author: Roberto Preghenella
/// @email: preghenella@bo.infn.it

#include "TrackSmearer.hh"
#include "TrackUtils.hh"
#include "TRandom.h"
#include <iostream>
#include <fstream>

namespace o2
{
namespace delphes
{

/*****************************************************************/

bool
TrackSmearer::loadTable(int pdg, const char *filename, bool forceReload)
{
  auto ipdg = getIndexPDG(pdg);
  if (mLUTHeader[ipdg] && !forceReload) {
    std::cout << " --- LUT table for PDG " << pdg << " has been already loaded " << std::endl;
    return false;
  }
  mLUTHeader[ipdg] = new lutHeader_t;
  
  std::ifstream lutFile(filename, std::ifstream::binary);
  if (!lutFile.is_open()) {
    std::cout << " --- cannot open covariance matrix file for PDG " << pdg << ": " << filename << std::endl;
    delete mLUTHeader[ipdg];
    mLUTHeader[ipdg] = nullptr;
    return false;
  }
  lutFile.read(reinterpret_cast<char *>(mLUTHeader[ipdg]), sizeof(lutHeader_t));
  if (lutFile.gcount() != sizeof(lutHeader_t)) {
    std::cout << " --- troubles reading covariance matrix header for PDG " << pdg << ": " << filename << std::endl;
    delete mLUTHeader[ipdg];
    mLUTHeader[ipdg] = nullptr;
    return false;
  }
  if (mLUTHeader[ipdg]->version != LUTCOVM_VERSION) {
    std::cout << " --- LUT header version mismatch: expected/detected = " << LUTCOVM_VERSION << "/" << mLUTHeader[ipdg]->version << std::endl;
    delete mLUTHeader[ipdg];
    mLUTHeader[ipdg] = nullptr;
    return false;
  }
  if (mLUTHeader[ipdg]->pdg != pdg) {
    std::cout << " --- LUT header PDG mismatch: expected/detected = " << pdg << "/" << mLUTHeader[ipdg]->pdg << std::endl;
    delete mLUTHeader[ipdg];
    mLUTHeader[ipdg] = nullptr;
    return false;
  }
  const int nnch = mLUTHeader[ipdg]->nchmap.nbins;
  const int nrad = mLUTHeader[ipdg]->radmap.nbins;
  const int neta = mLUTHeader[ipdg]->etamap.nbins;
  const int npt = mLUTHeader[ipdg]->ptmap.nbins;
  mLUTEntry[ipdg] = new lutEntry_t****[nnch];
  for (int inch = 0; inch < nnch; ++inch) {
    mLUTEntry[ipdg][inch] = new lutEntry_t***[nrad];
    for (int irad = 0; irad < nrad; ++irad) {
      mLUTEntry[ipdg][inch][irad] = new lutEntry_t**[neta];
      for (int ieta = 0; ieta < neta; ++ieta) {
	mLUTEntry[ipdg][inch][irad][ieta] = new lutEntry_t*[npt];
	for (int ipt = 0; ipt < npt; ++ipt) {
	  mLUTEntry[ipdg][inch][irad][ieta][ipt] = new lutEntry_t;
	  lutFile.read(reinterpret_cast<char *>(mLUTEntry[ipdg][inch][irad][ieta][ipt]), sizeof(lutEntry_t));
	  if (lutFile.gcount() != sizeof(lutEntry_t)) {
	    std::cout << " --- troubles reading covariance matrix entry for PDG " << pdg << ": " << filename << std::endl;
	    return false;
	  }
	}}}}
  std::cout << " --- read covariance matrix table for PDG " << pdg << ": " << filename << std::endl;
  mLUTHeader[ipdg]->print();

  lutFile.close();
  return true;
}

/*****************************************************************/

lutEntry_t *
TrackSmearer::getLUTEntry(int pdg, float nch, float radius, float eta, float pt)
{
  auto ipdg = getIndexPDG(pdg);
  if (!mLUTHeader[ipdg]) return nullptr;
  auto inch = mLUTHeader[ipdg]->nchmap.find(nch);
  auto irad = mLUTHeader[ipdg]->radmap.find(radius);
  auto ieta = mLUTHeader[ipdg]->etamap.find(eta);
  auto ipt  = mLUTHeader[ipdg]->ptmap.find(pt);
  return mLUTEntry[ipdg][inch][irad][ieta][ipt];
};

/*****************************************************************/

bool
TrackSmearer::smearTrack(O2Track &o2track, lutEntry_t *lutEntry)
{
  // generate efficiency
  if (mUseEfficiency && (gRandom->Uniform() > lutEntry->eff))
    return false;
  // transform params vector and smear
  double params_[5];
  for (int i = 0; i < 5; ++i) {
    double val = 0.;
    for (int j = 0; j < 5; ++j)
      val += lutEntry->eigvec[j][i] * o2track.getParam(j);
    params_[i] = gRandom->Gaus(val, sqrt(lutEntry->eigval[i]));
  }  
  // transform back params vector
  for (int i = 0; i < 5; ++i) {
    double val = 0.;
    for (int j = 0; j < 5; ++j)
      val += lutEntry->eiginv[j][i] * params_[j];
    o2track.setParam(val, i);
  }
  // should make a sanity check that par[2] sin(phi) is in [-1, 1]
  if (fabs(o2track.getParam(2)) > 1.) {
    std::cout << " --- smearTrack failed sin(phi) sanity check: " << o2track.getParam(2) << std::endl;
  }
  // set covariance matrix
  for (int i = 0; i < 15; ++i) 
    o2track.setCov(lutEntry->covm[i], i);
  return true;
}  

/*****************************************************************/

bool
TrackSmearer::smearTrack(O2Track &o2track, int pid, float nch)
{

  auto pt = o2track.getPt();
  auto eta = o2track.getEta();
  auto lutEntry = getLUTEntry(pid, nch, 0., eta, pt);
  if (!lutEntry || !lutEntry->valid) return false;
  return smearTrack(o2track, lutEntry);
}
  
/*****************************************************************/

bool
TrackSmearer::smearTrack(Track &track, bool atDCA)
{

  O2Track o2track;
  TrackUtils::convertTrackToO2Track(track, o2track, atDCA);
  int pdg = track.PID;
  float nch = mdNdEta; // use locally stored dNch/deta for the time being
  if (!smearTrack(o2track, pdg, nch)) return false;
  TrackUtils::convertO2TrackToTrack(o2track, track, atDCA);
  return true;
  
#if 0
  auto lutEntry = getLUTEntry(track.PID, 0., 0., track.Eta, track.PT);
  if (!lutEntry) return;
  
  O2Track o2track;
  TrackUtils::convertTrackToO2Track(track, o2track, atDCA);
  smearTrack(o2track, lutEntry);
  TrackUtils::convertO2TrackToTrack(o2track, track, atDCA);
#endif

}
  
/*****************************************************************/

  
} /** namespace delphes **/
} /** namespace o2 **/
