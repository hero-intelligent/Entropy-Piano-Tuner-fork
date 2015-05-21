/*****************************************************************************
 * Copyright 2015 Haye Hinrichsen, Christoph Wick
 *
 * This file is part of Entropy Piano Tuner.
 *
 * Entropy Piano Tuner is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * Entropy Piano Tuner is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * Entropy Piano Tuner. If not, see http://www.gnu.org/licenses/.
 *****************************************************************************/

//=============================================================================
//                       Iterative entropy minimization
//=============================================================================

#include "entropyminimizer.h"

#include <random>
#include <iostream>
#include <algorithm>
#include <sstream>

#include "core/calculation/algorithmfactory.h"
#include "core/system/eptexception.h"
#include "core/piano/piano.h"
#include "core/math/mathtools.h"
#include "core/system/timer.h"
#include "core/system/log.h"
#include "core/messages/messagehandler.h"
#include "core/messages/messagechangetuningcurve.h"
#include "core/messages/messagecaluclationprogress.h"

#include "auditorypreprocessing.h"

//=============================================================================
//                        Create algorithm factory
//=============================================================================

template<>
const AlgorithmFactory<entropyminimizer::EntropyMinimizer>
      AlgorithmFactory<entropyminimizer::EntropyMinimizer>::mSingleton(
        AlgorithmFactoryDescription("entropyminimizer"));

namespace entropyminimizer
{


//-----------------------------------------------------------------------------
//                             Constructor
//-----------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////////
/// \brief Constructor
/// \param piano : The complete piano structure will be copied from here
///////////////////////////////////////////////////////////////////////////////

EntropyMinimizer::EntropyMinimizer(const Piano &piano,
                                   const AlgorithmFactoryDescription &description) :
    Algorithm(piano, description),
    mAccumulator(NumberOfBins),
    mPitch(mNumberOfKeys),
    mInitialPitch(mNumberOfKeys),
    mRecalculateEntropy(false),
    mRecalculateKey(-1),
    mRecalculateFrequency(0)
{
    // Define cutoffs for the logarithmic spectra
    // In order to be able to shift them up and down by up to one half tone,
    // there should be a section of 100 cent at each end which is zero.
    double highestfrequency = mPiano.getEqualTempFrequency(mNumberOfKeys-1,0,440)*1.13;
    mLowerCutoff = 100;
    mUpperCutoff = std::min(NumberOfBins-100,
                   MathTools::roundToInteger(ftom(highestfrequency)));
}


//-----------------------------------------------------------------------------
//			   Worker function running in an independent thread
//-----------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////////
/// \brief Main calculation, running in an independent thread.
///
/// This is the main function which carries out the calculation. First it
/// calls the function for auditory preprocessing, then it manages the
/// Monte Carlo iteration.
///////////////////////////////////////////////////////////////////////////////

void EntropyMinimizer::algorithmWorkerFunction()
{
    // show initially a horizontal tuning curve
    for (int k=0; k<mNumberOfKeys; k++)
    {
        double f = mPiano.getEqualTempFrequency(k,0,440);
        //double f = mKeyboard[k].getRecordedFrequency(); // for testing
        mKeyboard[k].setComputedFrequency(f);
        updateTuningCurve(k, f);
    }

    bool success = performAuditoryPreprocessing();

    if (success)
    {
        std::cout << "CalculationManager: Initialize entropy minimizer" << std::endl;

        std::cout << "CalculationManager: Compute initial condition" << std::endl;
        ComputeInitialTuningCurve();

        std::cout << "CalculationManager: Wait 0.5 sec" << std::endl;
        Timer T; T.wait(500);

        MessageHandler::send<MessageCaluclationProgress>
                (MessageCaluclationProgress::CALCULATION_ENTROPY_REDUCTION_STARTED);
        std::cout << "CalculationManager: Start entropy minimization" << std::endl;
        minimizeEntropy();

        std::cout << "CalculationManager: Stop calculation" << std::endl;
    }
}



//-----------------------------------------------------------------------------
//			            Message listener and dispatcher
//-----------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////////
/// \brief Message listener and dispatcher for mouse clicks in the tuning curve
///
/// The tuninng curve can be manipulated manually, i.e., individual markers
/// can be moved with the mouse. If the calculation is running this requires
/// to reset the entropy calculation (which is usually higher after the
/// manipulation). This function sets the corresponding flags.
/// \param m : Pointer to the message.
///////////////////////////////////////////////////////////////////////////////

void EntropyMinimizer::handleMessage(MessagePtr m)
{
    EptAssert(m, "Message has to exist!");

    switch (m->getType())
    {
        case Message::MSG_CHANGE_TUNING_CURVE:
        {
            auto message(std::static_pointer_cast<MessageChangeTuningCurve>(m));
            double f = message->getFrequency();
            int keynumber = message->getKeyNumber();
            if (keynumber>=0) if (f != mKeyboard[keynumber].getComputedFrequency())
            {
                INFORMATION ("Manual change of tuning curve during computation");
                mRecalculateEntropy = true;
                mRecalculateFrequency = f;
                mRecalculateKey = keynumber;
#if CONFIG_ENABLE_XMGRACE
                writeSpectrum(keynumber,"modified",mPitch[keynumber]-getRecordedPitchET440(keynumber));
#endif // CONFIG_ENABLE_XMGRACE

            }
        }
        break;
    default:
        break;
    }
}


//-----------------------------------------------------------------------------
//                         Auditory preprocessing
//-----------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////////
/// \brief Auditory preprocessing
///
/// A large part of the computation is a sensible preprocessing of the
/// logarithmically binned spectra.
///////////////////////////////////////////////////////////////////////////////

bool EntropyMinimizer::performAuditoryPreprocessing()
{
#if CONFIG_ENABLE_XMGRACE
    for (int k=0; k < mNumberOfKeys; ++k) writeSpectrum(k,"before");
#endif // CONFIG_ENABLE_XMGRACE

    INFORMATION("EntropyMinimizer: start auditory preprocessing");
    AuditoryPreprocessing AP(mPiano);

    if (not AP.checkDataConsistency()) return false;

    INFORMATION("EntropyMinimzer: Normalize spectra ");
    for (auto &key : mKeys)
    {
        AP.normalizeSpectrum(key);
        showCalculationProgress(key,0,0.25);
        if (cancelThread()) return false;
    }

    INFORMATION("EntropyMinimzer: Clean spectra ");
    for (auto &key : mKeys)
    {
        AP.cleanSpectrum(key);
        showCalculationProgress(key,0.25,0.25);
        if (cancelThread()) return false;
    }

    INFORMATION("EntropyMinimizer: Cut low frequencies ");
    for (auto &key : mKeys)
    {
        AP.cutLowFrequencies(key);
        showCalculationProgress(key,0.5,0.25);
        if (cancelThread()) return false;
    }

    INFORMATION("EntropyMinimzer: Apply SPLA filter");
    AP.initializeSPLAFilter();
    for (auto &key : mKeys)
    {
        SpectrumType &spectrum = key.getSpectrum();
        AP.convertToSPLA(spectrum);
        showCalculationProgress(key,0.75,0.25);
        if (cancelThread()) return false;
    }

    INFORMATION("EntropyMinimizer: Extrapolate missing inharmonicity values");
    AP.extrapolateInharmonicity();
    if (cancelThread()) return false;

#if CONFIG_ENABLE_XMGRACE
    std::ofstream os("0-measured-inharmonicity.dat");
    for (int k=0; k < mNumberOfKeys; ++k)
        os << k << " " << mKeys[k].getMeasuredInharmonicity() << std::endl;
    os.close();
    os.open("0-extrapolated-inharmonicity.dat");
    for (int k=0; k < mNumberOfKeys; ++k)
        os << k << " " << mKeys[k].getMeasuredInharmonicity() << std::endl;
    os.close();
#endif // CONFIG_ENABLE_XMGRACE

    INFORMATION("EntropyMinimizer: Amend high-frequency spectral lines");
    AP.improveHighFrequencyPeaks();

    INFORMATION("EntropyMinimizer: Mollify spectral lines");
    for (auto &key : mKeys)
    {
        AP.applyMollifier(key);
        showCalculationProgress(key,0,1);
        if (cancelThread()) return false;
    }


#if CONFIG_ENABLE_XMGRACE
    for (int k=0; k < mNumberOfKeys; ++k) writeSpectrum(k,"final");
#endif // CONFIG_ENABLE_XMGRACE

    showCalculationProgress(0);
    INFORMATION("EntropyMinimizer: Stop auditory preprocessing");
    return true;
}



//-----------------------------------------------------------------------------
//                     Update the displayed tuning curve
//-----------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////////
/// \brief Update the tuning curve for a given key number
///
/// This function translates the actual mPitch value into the corresponding
/// frequency, stores the value in the local piano copy mPiano and sends a
/// message that the tuning curve has to be redrawn.
/// \param keynumber : Number of the key to be updated
///////////////////////////////////////////////////////////////////////////////

void EntropyMinimizer::updateTuningcurve (int keynumber)
{
    EptAssert (keynumber>=0 and keynumber<mNumberOfKeys,"Range of keynumber");
    double f = mPiano.getDefiningTempFrequency(keynumber, mPitch[keynumber],440);
    updateTuningCurve(keynumber, f);
}


///////////////////////////////////////////////////////////////////////////////
/// \brief Update the entire tuning curve
///
/// This function translates the all mPitch values into the corresponding
/// frequencies, stores the values in the local piano copy mPiano and sends a
/// sequence of messages that the tuning curve has to be redrawn.
///////////////////////////////////////////////////////////////////////////////

void EntropyMinimizer::updateTuningcurve ()
{
    for (int keynumber = 0; keynumber < mNumberOfKeys; ++keynumber)
        updateTuningcurve(keynumber);
}


//-----------------------------------------------------------------------------
//       Clear the accumulator as well as the intensities and pitches
//-----------------------------------------------------------------------------

void EntropyMinimizer:: clear()
{
    mAccumulator.assign(NumberOfBins,0);
    mPitch.assign(mNumberOfKeys,0);
    mInitialPitch.assign(mNumberOfKeys,0);
}


//-----------------------------------------------------------------------------
//                   Truncate logspectrum at the cutoffs
//-----------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////////
/// \brief  Truncate logspectrum at the cutoffs and return an element.
/// \param spectrum : Logarithmically binned spectrum.
/// \param m : Index of the element to be returned.
///////////////////////////////////////////////////////////////////////////////

double EntropyMinimizer::getElement (SpectrumType &spectrum, int m)
{
    return (m>mLowerCutoff and m<mUpperCutoff ? spectrum[m] : 0);
}


//-----------------------------------------------------------------------------
//                     Add spectrum to the accumulator
//-----------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////////
/// \brief Add or subtract a spectrum to the accumulator.
///
/// Since the entropy is computed from the accumulator, the accmulator values
/// have a probability interpretation. Therfore, spectra should be added
/// \param spectrum : Logarithmic spectrum
/// \param shift : number of bins by which the spectrum is shifted
/// \param intensity : weight at wich the spectrum is added (+) or subtracted (-).
///////////////////////////////////////////////////////////////////////////////

void EntropyMinimizer::addToAccumulator (SpectrumType &spectrum,
                                         int shift, double intensity)
{
    for (int m=0; m<NumberOfBins; ++m)
    {
        mAccumulator[m] += getElement(spectrum,m-shift) * intensity;
        // Tiny negative values are possible and will be truncated here:
        if (mAccumulator[m]<0 and mAccumulator[m]>-1E-10) mAccumulator[m] = 0;
        // Larger negative values will lead to an exception
        EptAssert(mAccumulator[m] >= 0,"negative intensities are inconsistent");
    }
}


//-----------------------------------------------------------------------------
//     Modify a spectral component in the accumulator, keeping the norm
//-----------------------------------------------------------------------------

void EntropyMinimizer::modifySpectralComponent (int keynumber,
                                                int pitch)
{
    EptAssert(keynumber>=0 and keynumber<mNumberOfKeys,"Range of parameter key");
    EptAssert(abs(pitch)<100,"Pitch should be less than a half tone");

    Key &key = mKeys[keynumber];
    SpectrumType &spectrum = key.getSpectrum();
    int  recorded_pitch  = getRecordedPitchET440(keynumber);
    int    old_pitchdiff = mPitch[keynumber] - recorded_pitch;
    int    new_pitchdiff = pitch             - recorded_pitch;

    addToAccumulator(spectrum,old_pitchdiff,-1);
    addToAccumulator(spectrum,new_pitchdiff,1);
    mPitch[keynumber] = pitch;
}


//-----------------------------------------------------------------------------
//                          Set all spectral components
//-----------------------------------------------------------------------------

void EntropyMinimizer::setAllSpectralComponents ()
{
    mAccumulator.assign(NumberOfBins,0);
    for (int k=0; k<mNumberOfKeys; ++k)
    {
        Key &key = mKeys[k];
        SpectrumType spectrum = key.getSpectrum();
        int  recorded_pitch  = getRecordedPitchET440(k);
        int pitchdiff = mPitch[k] - recorded_pitch;

        addToAccumulator(spectrum,pitchdiff,1);
    }
}

//-----------------------------------------------------------------------------
//              Compute initial condition of the tuning curve
//-----------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////////
/// \brief  Compute initial condition of the tuning curve
///
/// As described in the original publication, the Monte Carlo minimization
/// algorithm could in principle start with a flat (equal temperament) tuning
/// curve. However, starting with a flat curve involves the risk that the
/// algorithm finds false minima (for example, a deviation of -50 cent instead
/// of +50 cent which is of course also a valid solution). To prevent the
/// algorithm from running into false minima, we implemented several additional
/// features. In particular, we start out from an approximate tuning curve
/// which already shows the expected stretch. This curve is computed by first
/// defining a linear section between A3 and A5 and then extending this
/// curve to both sides by matching selected hihger partials. The selection
/// of the partials and their weight differs on both sides. Note that
/// possible errors do not play a role at this point because they are
/// elimiated by the subsequent Monte Carlo procedure.
///////////////////////////////////////////////////////////////////////////////

void EntropyMinimizer::ComputeInitialTuningCurve ()
{
    clear();

    // For the computation we need at least two octaves to both sides of A4:
    if (mKeyNumberOfA4<=13 or mNumberOfKeys-mKeyNumberOfA4<=13) return;

    // Return the measured (and extrapolated) inharmonicity B:
    auto B = [this] (int keynumber)
    { return mKeys[keynumber].getMeasuredInharmonicity(); };

    // Compute the expected stretch deviation of the n_th partial of a given key:
    auto cents = [B] (int keynumber, int n)
    { return 600.0  / MathTools::LOG2 * log((1+n*n*B(keynumber))/(1+B(keynumber))); };

    // Define a linear section of the curve in the middle
    int numberA3 = mKeyNumberOfA4-12;
    int numberA4 = mKeyNumberOfA4;
    int numberA5 = mKeyNumberOfA4+12;
//    double centsA34 = cents(numberA3,2);
//    double centsA35 = cents(numberA3,4);
//    double centsA45 = cents(numberA4,2);
//    double correction = 1 + (centsA35-centsA34-centsA45)/centsA35/2;
//    double pitchA5 =   correction * centsA45;
//    double pitchA3 = - correction * centsA34;
    double pitchA5 = cents(numberA4,2);
    double pitchA3 = cents(numberA4,2)-cents(numberA3,4);
    for (int k=numberA3; k<mKeyNumberOfA4; ++k)
        mInitialPitch[k] = pitchA3*(mKeyNumberOfA4-k)/12.0;
    for (int k=mKeyNumberOfA4+1; k<=numberA5; ++k)
        mInitialPitch[k] = pitchA5*(k-mKeyNumberOfA4)/12.0;

    // Extend curve to the right by iteration:
    for (int k=numberA5+1; k<mNumberOfKeys; k++)
    {
        double pitch42 = mInitialPitch[k-12] + cents(k-12,4) -cents(k,2);
        double pitch21 = mInitialPitch[k-12] + cents(k-12,2);
        mInitialPitch[k] = 0.3*pitch42 + 0.7*pitch21;
    }

    // Extend the curve to the left by iteration:
    for (int k=numberA3-1; k>=0; k--)
    {
        double pitch63 = mInitialPitch[k+12] + cents(k+12,3) -cents(k,6);
        double pitch105 = mInitialPitch[k+12] + cents(k+12,5) -cents(k,10);
        double fraction = 1.0*k/numberA3;
        mInitialPitch[k] = pitch63*fraction+pitch105*(1-fraction);
    }
}


//-----------------------------------------------------------------------------
//   Define a heuristic function for the allowed tolerance during Monte Carlo
//-----------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////////
/// \brief Allowed tolerance of the tuning curve during the Monte Carlo process
///
/// In order to avoid false minima, we further restrict the allowed range of
/// the tuning curve. This function returns the allowed tolerance in cents
/// around the initial condition defined above. It is defined heuristically.
/// \param keynumber : Number of the key
///////////////////////////////////////////////////////////////////////////////

int EntropyMinimizer::getTolerance (int keynumber)
{
    const double toleranceA0 = 30;
    const double toleranceA2 = 15;
    const double toleranceA4 = 5;
    const double toleranceA6 = 15;
    const double toleranceA7 = 30;

    auto f = [toleranceA4] (double a, double b, double k)
    { return toleranceA4 + a*k*k + b*k*k*k; };

    const double a1 = (-toleranceA0+8*toleranceA2-7*toleranceA4)/2304.0;
    const double b1 = (-toleranceA0+4*toleranceA2-3*toleranceA4)/55296.0;
    const double a2 = (-19*toleranceA4+27*toleranceA6-8*toleranceA7)/5184.0;
    const double b2 = (5*toleranceA4-9*toleranceA6+4*toleranceA7)/62208.0;
    int dkey = keynumber - mKeyNumberOfA4;
    return MathTools::roundToInteger(dkey<0 ? f(a1,b1,dkey) : f(a2,b2,dkey));

}


////-----------------------------------------------------------------------------
////            Add reference spectrum, defining the basic temperament
////-----------------------------------------------------------------------------

//void EntropyMinimizer::addReferenceSpectrum(double intensity)
//{
//    const double width = 12;  // should be instrument-dependent
//    const double slope = 300; // for average upright

//    int centralkey = mKeyNumberOfA4;
//    int lowerkey = std::max(0,mKeyNumberOfA4-24);
//    int upperkey = std::min(mNumberOfKeys,mKeyNumberOfA4+25);

//    double pitch = 0;
//    for (int keynumber = centralkey; keynumber < upperkey; ++keynumber)
//    {
//        double f = mPiano.getDefiningTempFrequency(keynumber,pitch);
//        int m = MathTools::roundToInteger(ftom(f));
//        EptAssert(m>=0 and m<NumberOfBins,"Range of m-Index");
//        double squaredist = (keynumber-centralkey)*(keynumber-centralkey);
//        double height = intensity * exp(-pow(squaredist/width/width,3.0));
//        mAccumulator[m-2] += height/4;
//        mAccumulator[m-1] += height/2;
//        mAccumulator[m] += height;
//        mAccumulator[m+1] += height/2;
//        mAccumulator[m+2] += height/4;
//        pitch += slope * mPiano.getExpectedInharmonicity(keynumber);
//    }
//    for (int keynumber = centralkey-1; keynumber >= lowerkey; --keynumber)
//    {
//        double f = mPiano.getDefiningTempFrequency(keynumber,pitch);
//        int m = MathTools::roundToInteger(ftom(f));
//        EptAssert(m>=0 and m<NumberOfBins,"Range of m-Index");
//        double squaredist = (keynumber-centralkey)*(keynumber-centralkey);
//        double height = intensity * exp(-pow(squaredist/width/width,3.0));
//        mAccumulator[m-2] += height/4;
//        mAccumulator[m-1] += height/2;
//        mAccumulator[m] += height;
//        mAccumulator[m+1] += height/2;
//        mAccumulator[m+2] += height/4;
//        pitch -= slope * mPiano.getExpectedInharmonicity(keynumber);
//    }
//}


//-----------------------------------------------------------------------------

double EntropyMinimizer::computeEntropy()
{
    SpectrumType copy(mAccumulator);

//    std::ofstream os("MC.dat");
//    for (int m=0; m<NumberOfBins; m++) os << mtof(m) << " " << copy[m] << "\n";
//    os.close();
//    system("xmgrace MC.dat");

    MathTools::normalize(copy);
    return MathTools::computeEntropy(copy);
}

//-----------------------------------------------------------------------------

void EntropyMinimizer::minimizeEntropy ()
{
    int cents=20; // even number, defines width of fluctuations

    // Create random device for probabilistic seeding:
    std::random_device rd;
    // Initialze Mersenne twister with random seed:
    static std::mt19937 generator(rd());
    // Define distributions to be used:
    std::binomial_distribution<int> binomial(cents);
    std::uniform_int_distribution<int> keydist(0,mNumberOfKeys-1);
    std::uniform_real_distribution<double> probdist(0,1);

    // copy initial condition to the actual pitch
    for (int k=0; k<mNumberOfKeys; k++)
        mPitch[k] = MathTools::roundToInteger(mInitialPitch[k]);
    updateTuningcurve();

    setAllSpectralComponents();

    // Add the reference spectrum
    //addReferenceSpectrum(0.2);

    // compute initial entropy
    double H = computeEntropy();
    std::cout << "STARTING WITH ENTROPY H=" << H << std::endl;
    double counter=0, progress=0;

    // helper function for accepting an update and handling the progress bar
    auto acceptUpdate = [&H,&progress,&counter,this] (int keynumber, double Hnew)
    {
        H = Hnew;
        std::cout << "H=" << H << std::endl;
        if (keynumber>=0) updateTuningcurve(keynumber);
        else updateTuningcurve();
        counter++;
        progress += (1-progress) / (1+5000.0/sqrt(counter));
        showCalculationProgress (progress);
        writeAccumulator("0-accumulator.dat");
        writeSpectrum(4,"tuned",mPitch[4]-getRecordedPitchET440(4));
        writeSpectrum(16,"tuned",mPitch[16]-getRecordedPitchET440(16));
        writeSpectrum(28,"tuned",mPitch[28]-getRecordedPitchET440(28));
    };

    double methodratio = 1;

    while (not terminateThread() and progress<1)
    {
        // If external manual change of tuning curve reset entropy
        if (mRecalculateEntropy)
        {
            int manualpitch = getPitchET440(mRecalculateKey,mRecalculateFrequency);
            std::cout << "NEW PITCH(" << mRecalculateKey << ") = "
                      << manualpitch << std::endl;
            modifySpectralComponent(mRecalculateKey,manualpitch);
            H = computeEntropy();
            std::cout << "RESET ENTROPY H=" << H << std::endl;
            mRecalculateEntropy=false;
            mRecalculateKey=-1;
            mRecalculateFrequency=0;
        }

        // Core of the whole entropy piano tuner:
        // Select a random key which is different from A4
        int keynumber;
        do keynumber = keydist(generator);
        while (keynumber==mKeyNumberOfA4);


        if (probdist(generator)<methodratio)
        // perform a Monte Carlo trial in which a whole section is moved by +/- 1.
        {
            std::vector<int> savePitch(mPitch);
            int sign = (probdist(generator)<0.5 ? 1:-1);
            if (keynumber < mKeyNumberOfA4)
            {
                for (int k=0; k<=keynumber; ++k) mPitch[k]+=sign;
            }
            else
            {
                for (int k=keynumber; k<mNumberOfKeys; ++k) mPitch[k]+=sign;
            }
            setAllSpectralComponents();
            double Hnew = computeEntropy();
            if (Hnew < H)
            {
                acceptUpdate (-1,Hnew);
                methodratio *= 0.995;
                std::cout << "METHODRATIO = " << methodratio << std::endl;
            }
            else
            {
                mPitch = savePitch;
                setAllSpectralComponents();
            }
        }
        else
        {
            // Monte-Carlo step by changing the pitch of an individual key
            int oldpitch = mPitch[keynumber];
            double initialpitch =  mInitialPitch[keynumber];
            double tolerance = getTolerance(keynumber);
            int newpitch;
            do newpitch = oldpitch + binomial(generator)-cents/2;
            while (((fabs(oldpitch-initialpitch) < tolerance and
                     fabs(newpitch-initialpitch) > tolerance)
                     or newpitch == oldpitch)
                    and not terminateThread());
            modifySpectralComponent(keynumber,newpitch);
            double Hnew = computeEntropy();
            if (Hnew < H) acceptUpdate (keynumber,Hnew);
            else modifySpectralComponent(keynumber,oldpitch);
        }
    }
#if CONFIG_ENABLE_XMGRACE
    for (int k=0; k < mNumberOfKeys; ++k) writeSpectrum(k,"middle",mPitch[k]-getRecordedPitchET440(k));
#endif // CONFIG_ENABLE_XMGRACE

}



//-----------------------------------------------------------------------------
//                  compute recorded pitch against ET 440
//-----------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////////
/// Compute the pitch versus equal temperament 440 in cents.
/// \return Pitch in cents
///////////////////////////////////////////////////////////////////////////////

double EntropyMinimizer::getRecordedPitchET440(int keynumber)
{
    double ET440 = 440.0 * pow(2,1.0/12.0*(keynumber-mKeyNumberOfA4));
    double frec = mKeys[keynumber].getRecordedFrequency();
    return 1200*log(frec/ET440)/MathTools::LOG2;
}


//-----------------------------------------------------------------------------
//                     get computed pitch against ET 440
//-----------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////////
/// Get the computed pitch versus equal temperament 440 in cents.
/// \param f : frequency
/// \return Pitch in cents
///////////////////////////////////////////////////////////////////////////////

int EntropyMinimizer::getPitchET440(int keynumber, double f)
{
    double ET440 = 440.0 * pow(2,1.0/12.0*(keynumber-mKeyNumberOfA4));
    return MathTools::roundToInteger(1200*log(f/ET440)/MathTools::LOG2);
}



//-----------------------------------------------------------------------------
//			Write function for development purposes
//-----------------------------------------------------------------------------

void EntropyMinimizer::writeAccumulator(std::string filename)
{
#if CONFIG_ENABLE_XMGRACE
    std::ofstream os (filename);
    for (int m=0; m<NumberOfBins; ++m)
    {
        os << Key::IndexToFrequency(m) << "\t" << mAccumulator[m] << std::endl;
    }
    os.close();
#else
    (void)filename; // suppress warnings
#endif // CONFIG_ENABLE_XMGRACE
}

//-----------------------------------------------------------------------------
//			Write function for development purposes
//-----------------------------------------------------------------------------

void EntropyMinimizer::writeSpectrum (int k,std::string filename, int pitch)
{
#if CONFIG_ENABLE_XMGRACE
    std::stringstream ss;
    ss << "spectrum/" << k << "-"<< filename << ".dat";
    std::ofstream os(ss.str());
    auto v = mPiano.getKey(k).getSpectrum();
    os << "# pitch= " << pitch << std::endl;
    for (uint m=abs(pitch); m<v.size()-abs(pitch); ++m)
    {
        if (v.size() != static_cast<size_t>(NumberOfBins)) os << m+pitch << "\t" << v[m] << std::endl;
        else os << Key::IndexToFrequency(m+pitch) << "\t" << v[m] << std::endl;
    }
    os.close();
#else
    (void)filename; (void)k; (void)pitch; // suppress warnings
#endif // CONFIG_ENABLE_XMGRACE
}


}  // namespace entropy minimizer