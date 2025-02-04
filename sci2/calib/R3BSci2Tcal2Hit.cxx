/******************************************************************************
 *   Copyright (C) 2019 GSI Helmholtzzentrum für Schwerionenforschung GmbH    *
 *   Copyright (C) 2019 Members of R3B Collaboration                          *
 *                                                                            *
 *             This software is distributed under the terms of the            *
 *                 GNU General Public Licence (GPL) version 3,                *
 *                    copied verbatim in the file "LICENSE".                  *
 *                                                                            *
 * In applying this license GSI does not waive the privileges and immunities  *
 * granted to it by virtue of its status as an Intergovernmental Organization *
 * or submit itself to any jurisdiction.                                      *
 ******************************************************************************/

// -------------------------------------------------------------
// -----         R3BSci2Tcal2Hit source file               -----
// -----    Created 25/10/21  by J.L. Rodriguez-Sanchez    -----
// -------------------------------------------------------------

#include "R3BSci2Tcal2Hit.h"
#include "FairLogger.h"
#include "FairRuntimeDb.h"
#include "R3BSci2HitData.h"
#include "R3BSci2HitPar.h"
#include "R3BSci2Mapped2Cal.h"
#include "R3BSci2MappedData.h"
#include "R3BSci2TcalData.h"
#include "R3BTCalEngine.h"
#include "R3BTCalPar.h"

#include "TClonesArray.h"

R3BSci2Tcal2Hit::R3BSci2Tcal2Hit()
    : R3BSci2Tcal2Hit("Sci2Cal2Hit", 1)
{
}

R3BSci2Tcal2Hit::R3BSci2Tcal2Hit(const char* name, Int_t iVerbose)
    : FairTask(name, iVerbose)
    , fCalItems(NULL)
    , fHitItems(NULL)
    , fsci2VeffX(1.)
    , fsci2OffsetX(0.)
    , fsci2VeffXT(1.)
    , fsci2OffsetXT(0.)
    , fClockFreq(1. / VFTX_CLOCK_MHZ * 1000.)
    , fOnline(kFALSE)
{
}

R3BSci2Tcal2Hit::~R3BSci2Tcal2Hit()
{
    LOG(DEBUG) << "R3BSci2Tcal2Hit::Destructor";
    if (fHitItems)
    {
        delete fHitItems;
        fHitItems = NULL;
    }
}

void R3BSci2Tcal2Hit::SetParContainers()
{
    LOG(INFO) << "R3BSci2Tcal2Hit::SetParContainers()";
    // Parameter Container
    // Reading IncomingIDPar from FairRuntimeDb
    FairRuntimeDb* rtdb = FairRuntimeDb::instance();
    if (!rtdb)
    {
        LOG(ERROR) << "FairRuntimeDb not opened!";
    }
    fSci2Hit_Par = (R3BSci2HitPar*)rtdb->getContainer("Sci2HitPar");
    if (!fSci2Hit_Par)
    {
        LOG(ERROR) << "R3BSci2Tcal2Hit:: Couldn't get handle on R3BSci2HitPar container";
    }
    else
    {
        LOG(INFO) << "R3BSci2Tcal2Hit:: R3BSci2HitPar container open";
    }
}

void R3BSci2Tcal2Hit::SetParameter()
{
    //--- Parameter Container ---
    fPos_p0 = fSci2Hit_Par->GetPos_p0();
    fPos_p1 = fSci2Hit_Par->GetPos_p1();
}

InitStatus R3BSci2Tcal2Hit::Init()
{
    // get access to Cal data
    FairRootManager* mgr = FairRootManager::Instance();
    if (NULL == mgr)
        LOG(fatal) << "FairRootManager not found";

    fCalItems = (TClonesArray*)mgr->GetObject("Sci2Tcal");
    if (NULL == fCalItems)
        LOG(fatal) << "R3BSci2Tcal2Hit::Init() Sci2Tcal not found";

    // request storage of Hit data in output tree
    fHitItems = new TClonesArray("R3BSci2HitData");
    mgr->Register("Sci2Hit", "Sci2 hit data", fHitItems, !fOnline);

    Icount = 0;
    SetParameter();
    return kSUCCESS;
}

InitStatus R3BSci2Tcal2Hit::ReInit()
{
    SetParContainers();
    SetParameter();
    return kSUCCESS;
}

void R3BSci2Tcal2Hit::Exec(Option_t* option)
{
    UInt_t nHits = 0, iDet = 0, iCh = 0;
    Int_t multTcal[2][3];
    Double_t iRawTimeNs[2][3][64];
    Double_t PosCal = -1000.;
    Double_t Tmean = -1;        // 0.5*(TrawLEFT + TrawRIGHT)
    Double_t Tmean_w_Tref = -1; // 0.5*(TrawLEFT + TrawRIGHT) - Tref

    for (UShort_t d = 0; d < 2; d++)
        for (UShort_t pmt = 0; pmt < 3; pmt++)
        {
            multTcal[d][pmt] = 0;
            for (UShort_t m = 0; m < 64; m++)
                iRawTimeNs[d][pmt][m] = 0.;
        }

    // --- -------------- --- //
    // --- read tcal data --- //
    // --- -------------- --- //
    if (fCalItems && fCalItems->GetEntriesFast())
    {
        nHits = fCalItems->GetEntriesFast();
        for (Int_t ihit = 0; ihit < nHits; ihit++)
        {
            R3BSci2TcalData* hittcal = (R3BSci2TcalData*)fCalItems->At(ihit);
            if (!hittcal)
                continue;
            iDet = hittcal->GetDetector() - 1;
            iCh = hittcal->GetChannel() - 1;
            iRawTimeNs[iDet][iCh][multTcal[iDet][iCh]] = hittcal->GetRawTimeNs();
            multTcal[iDet][iCh]++;
        } // --- end of loop over Tcal data --- //

        for (UShort_t d = 0; d < 2; d++)
        {
            // This hit selection requiring both multiplicity
            // being the same should not be used for the
            // actual offline analysis, but still sufficient
            // assumption  for the online analysis.
            // For the offline analysis, it is important to
            // check time difference between two detectors
            // to get a pair of hits with reasonable position.
            // At the Sci2Hit level, we don't care about higher
            // multiplicity. The good hit will be selected by
            // later analysis, such as R3BIncomingBeta to find
            // a good hit to give a proper tof value.
            if (multTcal[d][0] < 64 && multTcal[d][0] == multTcal[d][1])
            {
                for (int m = 0; m < multTcal[d][0]; m++)
                {
                    PosCal = fPos_p0 + fPos_p1 * (iRawTimeNs[d][0][m] - iRawTimeNs[d][1][m]);
                    Tmean = 0.5 * (iRawTimeNs[d][0][m] + iRawTimeNs[d][1][m]);
                    if (multTcal[d][2] == 1)
                        Tmean_w_Tref = Tmean - iRawTimeNs[d][2][0];
                    AddHitData(d + 1, PosCal, Tmean, Tmean_w_Tref);
                }
            } // end of mult left == mult right
        }     // end of loop over the number of detectors
    }         // end of if Tcal data
    return;
}

void R3BSci2Tcal2Hit::FinishEvent()
{
    if (fHitItems)
    {
        fHitItems->Clear();
    }
}

// -----   Private method AddHitData  --------------------------------------------
R3BSci2HitData* R3BSci2Tcal2Hit::AddHitData(Int_t sci, Double_t x, Double_t tmean, Double_t tmean_w_tref)
{
    // It fills the R3BSofSciHitData
    TClonesArray& clref = *fHitItems;
    Int_t size = clref.GetEntriesFast();
    return new (clref[size]) R3BSci2HitData(sci, x, tmean, tmean_w_tref);
}

ClassImp(R3BSci2Tcal2Hit);
