#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <exception>
#ifdef DEBUG
#include <cassert>
#endif
#include <memory>
#include <algorithm>
#include <ipps.h>
#include <ippcore.h>
#ifdef __INTEL_COMPILER
#include <pstl/execution>
#include <pstl/algorithm>
#endif
//#include <boost/align/aligned_allocator.hpp>
#include <vector>
#include "rtseis/private/throw.hpp"
#define RTSEIS_LOGGING 1
#include "rtseis/log.h"
#include "rtseis/postProcessing/singleChannel/waveform.hpp"
#include "rtseis/postProcessing/singleChannel/detrend.hpp"
#include "rtseis/postProcessing/singleChannel/demean.hpp"
#include "rtseis/postProcessing/singleChannel/taper.hpp"
#include "rtseis/utilities/design/enums.hpp"
#include "rtseis/utilities/design/filterDesigner.hpp"
#include "rtseis/utilities/math/convolve.hpp"
#include "rtseis/utilities/filterRepresentations/fir.hpp"
#include "rtseis/utilities/filterRepresentations/ba.hpp"
#include "rtseis/utilities/filterRepresentations/sos.hpp"
#include "rtseis/utilities/filterImplementations/firFilter.hpp"
#include "rtseis/utilities/filterImplementations/iirFilter.hpp"
#include "rtseis/utilities/filterImplementations/iiriirFilter.hpp"
#include "rtseis/utilities/filterImplementations/sosFilter.hpp"

using namespace RTSeis;
using namespace PostProcessing::SingleChannel;

static inline 
double computeNyquistFrequencyFromSamplingPeriod(const double dt);
static inline std::pair<double,double>
computeNormalizedFrequencyFromSamplingPeriod(const std::pair<double,double> fc,
                                            const double dt);
static inline
double computeNormalizedFrequencyFromSamplingPeriod(const double fc,
                                                    const double dt);
static inline Utilities::Math::Convolve::Mode
classifyConvolveMode(const ConvolutionMode mode);
static inline Utilities::Math::Convolve::Implementation
classifyConvolveImplementation(const ConvolutionImplementation implementation);
static inline Utilities::FilterDesign::IIRPrototype
classifyIIRPrototype(const IIRPrototype prototype);


static inline void reverse(std::vector<double> &x);
static inline void reverse(const std::vector<double> &x,
                           std::vector<double> &y);
static inline void copy(const std::vector<double> &x, 
                        std::vector<double> &y);

static inline void copy(const std::vector<double> &x, 
                        std::vector<double> &y)
{
#ifdef __INTEL_COMPILER
    std::copy(pstl::execution::unseq, x.begin(), x.end(), y.begin());
#else
    int len = static_cast<int> (x.size());
    ippsCopy_64f(x.data(), y.data(), len);
#endif
}

static inline void reverse(std::vector<double> &x)
{
#ifdef __INTEL_COMPLIER
    std::reverse(pstl::execution::unseq, x.begin(), x.end()); 
#else
    int len = static_cast<int> (x.size());
    ippsFlip_64f_I(x.data(), len);
#endif
    return;
} 

static inline void reverse(const std::vector<double> &x, 
                           std::vector<double> &y)
{
#ifdef __INTEL_COMPILER
    y.resize(x.size());
    std::reverse_copy(pstl::execution::unseq, x.begin(), x.end(), y.begin());
#else
    int len = static_cast<int> (x.size());
    ippsFlip_64f(x.data(), y.data(), len);
#endif
    return;
}

class Waveform::WaveformImpl
{
public:
    WaveformImpl(void)
    {
        return;
    }
    ~WaveformImpl(void)
    {
        return;
    }
    void resizeOutputData(const int n)
    {
        y_.resize(n);
    }
    void setData(const size_t n, const double x[])
    {
        x_.resize(n);
#ifdef __INTEL_COMPILER
        std::copy(pstl::execution::unseq, x, x+n, x_.data());
#else
        std::copy(x, x+n, x_.data());
#endif
        return;
    }
    const double *getInputDataPointer(void) const
    {
        return x_.data();
    }
    double *getOutputDataPointer(void)
    {
        return y_.data();
    }
    int getLengthOfInputSignal(void) const
    {
        return static_cast<int> (x_.size());
    }
//private:
    Utilities::FilterDesign::FilterDesigner filterDesigner;
    std::vector<double> x_;
    std::vector<double> y_;
    /// Sampling period
    double dt = 1;
};

Waveform::Waveform(const double dt) :
    pImpl(new WaveformImpl()) 
{
    if (dt <= 0)
    {
        RTSEIS_THROW_IA("Sampling period = %lf must be positive", dt);
    }
    pImpl->dt = dt;
    return;
}

Waveform::~Waveform(void)
{
    return;
}

void Waveform::setData(const std::vector<double> &x)
{
    size_t n = x.size();
    if (n < 1)
    {
        RTSEIS_ERRMSG("%s", "x has zero length");
        throw std::invalid_argument("x has zero length");
    }
    setData(n, x.data());
    return;
}

void Waveform::setData(const size_t n, const double x[])
{
    if (n < 1 || x == nullptr)
    {
        if (n < 1)
        {
            RTSEIS_ERRMSG("%s", "x has zero length");
            throw std::invalid_argument("x has zero length");
        }
        if (x == nullptr)
        {
            RTSEIS_ERRMSG("%s", "x is NULL");
            throw std::invalid_argument("x is NULL");
        }
        throw std::invalid_argument("Invalid arguments");
    }
    pImpl->setData(n, x);
    return;
}

void Waveform::getData(std::vector<double> &y)
{
    y = pImpl->y_;
    return;
}

void Waveform::getData(const size_t nwork, double y[]) const
{
    size_t leny = getOutputLength();
    if (nwork < leny)
    {
        throw std::invalid_argument("nwork = " + std::to_string(nwork)
                                  + " must be at least = "
                                   + std::to_string(leny));
    }
    if (leny == 0){return;}
    if (y == nullptr)
    {
        throw std::invalid_argument("y is NULL");
    }
#ifdef __INTEL_COMPILER
    std::copy(pstl::execution::unseq, pImpl->y_.begin(), pImpl->y_.end(), y);
#else
    std::copy(pImpl->y_.begin(), pImpl->y_.end(), y);
#endif 
    return;
}

//----------------------------------------------------------------------------//
//                                 Utilities                                  //
//----------------------------------------------------------------------------//

/// TODO delete this function
size_t Waveform::getOutputLength(void) const
{
    return pImpl->y_.size();
}

double Waveform::getNyquistFrequency(void) const noexcept
{
    double fnyq = 1.0/(2.0*pImpl->dt);
    return fnyq;
} 

//----------------------------------------------------------------------------//
//                     Convolution/Correlation/AutoCorrelation                //
//----------------------------------------------------------------------------//

void Waveform::convolve(
    const std::vector<double> &s,
    const ConvolutionMode mode,
    const ConvolutionImplementation implementation)
{
    int nx = pImpl->getLengthOfInputSignal();
    int ny = static_cast<int> (s.size());
    if (nx < 1){RTSEIS_THROW_IA("%s", "No data is set on the module");}
    if (ny < 1){RTSEIS_THROW_IA("%s", "No data points in s");}
    // Classify the convolution mode
    Utilities::Math::Convolve::Mode convcorMode;
    convcorMode = classifyConvolveMode(mode); // throws
    // Classify the convolution implementation
    Utilities::Math::Convolve::Implementation convcorImpl;
    convcorImpl = classifyConvolveImplementation(implementation); // throws
    // Perform the convolution
    int ierr = Utilities::Math::Convolve::convolve(pImpl->x_, s, pImpl->y_,
                                                   convcorMode, convcorImpl);
    if (ierr != 0)
    {
        RTSEIS_ERRMSG("%s", "Failed to compute convolution");
        pImpl->y_.resize(0);
        return;
    }
    return;
}

void Waveform::correlate(
    const std::vector<double> &s, 
    const ConvolutionMode mode,
    const ConvolutionImplementation implementation)
{
    int nx = pImpl->getLengthOfInputSignal();
    int ny = static_cast<int> (s.size());
    if (nx < 1){RTSEIS_THROW_IA("%s", "No data is set on the module");}
    if (ny < 1){RTSEIS_THROW_IA("%s", "No data points in s");}
    // Classify the convolution mode
    Utilities::Math::Convolve::Mode convcorMode;
    convcorMode = classifyConvolveMode(mode); // throws
    // Classify the convolution implementation
    Utilities::Math::Convolve::Implementation convcorImpl;
    convcorImpl = classifyConvolveImplementation(implementation); // throws
    // Perform the correlation
    int ierr = Utilities::Math::Convolve::correlate(pImpl->x_, s, pImpl->y_,
                                                    convcorMode, convcorImpl);
    if (ierr != 0)
    {
        RTSEIS_ERRMSG("%s", "Failed to compute correlation");
        pImpl->y_.resize(0);
        return;
    }
    return;
}

void Waveform::autocorrelate(
    const ConvolutionMode mode,
    const ConvolutionImplementation implementation)
{
    int nx = pImpl->getLengthOfInputSignal();
    if (nx < 1){RTSEIS_THROW_IA("%s", "No data is set on the module");}
    // Classify the convolution mode
    Utilities::Math::Convolve::Mode convcorMode;
    convcorMode = classifyConvolveMode(mode); // throws
    // Classify the convolution implementation
    Utilities::Math::Convolve::Implementation convcorImpl;
    convcorImpl = classifyConvolveImplementation(implementation); // throws
    // Perform the correlation
    int ierr = Utilities::Math::Convolve::autocorrelate(pImpl->x_, pImpl->y_,
                                                    convcorMode, convcorImpl);
    if (ierr != 0)
    {
        RTSEIS_ERRMSG("%s", "Failed to compute autocorrelation");
        pImpl->y_.resize(0);
        return;
    }
    return;
}

//----------------------------------------------------------------------------//
//                            Demeaning/detrending                            //
//----------------------------------------------------------------------------//

void Waveform::demean(void)
{
    int len = pImpl->getLengthOfInputSignal();
    if (len < 1)
    {
        RTSEIS_WARNMSG("%s", "No data is set on the module");
        return;
    }
    // Demean the data
    try
    {
        DemeanParameters parms(RTSeis::Precision::DOUBLE);
        Demean demean(parms);
        const double *x = pImpl->getInputDataPointer();
        pImpl->resizeOutputData(len);
        double  *y = pImpl->getOutputDataPointer();
        demean.apply(len, x, y);
    }
    catch (const std::invalid_argument &ia)
    {
        RTSEIS_ERRMSG("%s", ia.what());
        throw std::invalid_argument("Algorithmic failure");
    }
    return; 
}

void Waveform::detrend(void)
{
    int len = pImpl->getLengthOfInputSignal();
    if (len < 2)
    {
        RTSEIS_WARNMSG("%s", "At least 2 data points required to detrend");
        return;
    }
    // Detrend the data
    DetrendParameters parms(RTSeis::Precision::DOUBLE);
    Detrend detrend(parms);
    const double *x = pImpl->getInputDataPointer();
    pImpl->resizeOutputData(len);
    double  *y = pImpl->getOutputDataPointer();
    detrend.apply(len, x, y); 
    return;
}
//----------------------------------------------------------------------------//
//                           Band-specific Filters                            //
//----------------------------------------------------------------------------//

void Waveform::iirLowpassFilter(const int order, const double fc,
                                const IIRPrototype prototype,
                                const double ripple,
                                const bool lzeroPhase)
{
    // Check that there's data
    int len = pImpl->getLengthOfInputSignal();
    if (len < 1)
    {   
        RTSEIS_WARNMSG("%s", "No data is set on the module");
        return;
    }
    // Compute normalized frequencies
    double r = computeNormalizedFrequencyFromSamplingPeriod(fc, pImpl->dt);
    Utilities::FilterDesign::IIRPrototype ptype;
    ptype = classifyIIRPrototype(prototype);
    Utilities::FilterRepresentations::BA ba;
    pImpl->filterDesigner.designLowpassIIRFilter(
                        order, r, ptype, ripple, ba,
                        Utilities::FilterDesign::IIRFilterDomain::DIGITAL);
    iirFilter(ba, lzeroPhase);
    return;
}

void Waveform::sosLowpassFilter(const int order, const double fc, 
                                const IIRPrototype prototype,
                                const double ripple,
                                const bool lzeroPhase)
{
    // Check that there's data
    int len = pImpl->getLengthOfInputSignal();
    if (len < 1)
    {
        RTSEIS_WARNMSG("%s", "No data is set on the module");
        return;
    }
    // Compute normalized frequencies
    double r = computeNormalizedFrequencyFromSamplingPeriod(fc, pImpl->dt);
    Utilities::FilterDesign::IIRPrototype ptype;
    ptype = classifyIIRPrototype(prototype);
    Utilities::FilterRepresentations::SOS sos;
    pImpl->filterDesigner.designLowpassIIRFilter(
                     order, r, ptype, ripple, sos,
                     Utilities::FilterDesign::SOSPairing::NEAREST,
                     Utilities::FilterDesign::IIRFilterDomain::DIGITAL);
    sosFilter(sos, lzeroPhase);
    return;
}

void Waveform::iirHighpassFilter(const int order, const double fc, 
                                 const IIRPrototype prototype,
                                 const double ripple,
                                 const bool lzeroPhase)
{
    // Check that there's data
    int len = pImpl->getLengthOfInputSignal();
    if (len < 1)
    {   
        RTSEIS_WARNMSG("%s", "No data is set on the module");
        return;
    }   
    // Compute normalized frequencies
    double r = computeNormalizedFrequencyFromSamplingPeriod(fc, pImpl->dt);
    Utilities::FilterDesign::IIRPrototype ptype;
    ptype = classifyIIRPrototype(prototype);
    Utilities::FilterRepresentations::BA ba; 
    pImpl->filterDesigner.designHighpassIIRFilter(
                     order, r, ptype, ripple, ba, 
                     Utilities::FilterDesign::IIRFilterDomain::DIGITAL);
    iirFilter(ba, lzeroPhase);
    return;
}

void Waveform::sosHighpassFilter(const int order, const double fc, 
                                 const IIRPrototype prototype,
                                 const double ripple,
                                 const bool lzeroPhase)
{
    // Check that there's data
    int len = pImpl->getLengthOfInputSignal();
    if (len < 1)
    {   
        RTSEIS_WARNMSG("%s", "No data is set on the module");
        return;
    }   
    // Compute normalized frequencies
    double r = computeNormalizedFrequencyFromSamplingPeriod(fc, pImpl->dt);
    Utilities::FilterDesign::IIRPrototype ptype;
    ptype = classifyIIRPrototype(prototype);
    Utilities::FilterRepresentations::SOS sos;
    pImpl->filterDesigner.designHighpassIIRFilter(
                    order, r, ptype, ripple, sos,
                    Utilities::FilterDesign::SOSPairing::NEAREST,
                    Utilities::FilterDesign::IIRFilterDomain::DIGITAL);
    sosFilter(sos, lzeroPhase);
    return;
}

void Waveform::iirBandpassFilter(const int order,
                                 const std::pair<double,double> fc, 
                                 const IIRPrototype prototype,
                                 const double ripple,
                                 const bool lzeroPhase)
{
    // Check that there's data
    int len = pImpl->getLengthOfInputSignal();
    if (len < 1)
    {   
        RTSEIS_WARNMSG("%s", "No data is set on the module");
        return;
    }
    // Compute normalized frequencies
    std::pair<double,double> r
        = computeNormalizedFrequencyFromSamplingPeriod(fc, pImpl->dt);
    Utilities::FilterDesign::IIRPrototype ptype;
    ptype = classifyIIRPrototype(prototype);
    Utilities::FilterRepresentations::BA ba; 
    pImpl->filterDesigner.designBandpassIIRFilter(
                    order, r, ptype, ripple, ba, 
                     Utilities::FilterDesign::IIRFilterDomain::DIGITAL);
    iirFilter(ba, lzeroPhase);
    return;
}

void Waveform::sosBandpassFilter(const int order,
                                 const std::pair<double,double> fc, 
                                 const IIRPrototype prototype,
                                 const double ripple,
                                 const bool lzeroPhase)
{
    // Check that there's data
    int len = pImpl->getLengthOfInputSignal();
    if (len < 1)
    {   
        RTSEIS_WARNMSG("%s", "No data is set on the module");
        return;
    }   
    // Compute normalized frequencies
    std::pair<double,double> r
        = computeNormalizedFrequencyFromSamplingPeriod(fc, pImpl->dt);
    Utilities::FilterDesign::IIRPrototype ptype;
    ptype = classifyIIRPrototype(prototype);
    Utilities::FilterRepresentations::SOS sos;
    pImpl->filterDesigner.designBandpassIIRFilter(
                    order, r, ptype, ripple, sos,
                    Utilities::FilterDesign::SOSPairing::NEAREST,
                    Utilities::FilterDesign::IIRFilterDomain::DIGITAL);
    sosFilter(sos, lzeroPhase);
    return;
}

void Waveform::iirBandstopFilter(const int order,
                                 const std::pair<double,double> fc, 
                                 const IIRPrototype prototype,
                                 const double ripple,
                                 const bool lzeroPhase)
{
    // Check that there's data
    int len = pImpl->getLengthOfInputSignal();
    if (len < 1)
    {
        RTSEIS_WARNMSG("%s", "No data is set on the module");
        return;
    }
    // Compute normalized frequencies
    std::pair<double,double> r
        = computeNormalizedFrequencyFromSamplingPeriod(fc, pImpl->dt);
    Utilities::FilterDesign::IIRPrototype ptype;
    ptype = classifyIIRPrototype(prototype);
    Utilities::FilterRepresentations::BA ba;
    pImpl->filterDesigner.designBandstopIIRFilter(
                    order, r, ptype, ripple, ba,
                    Utilities::FilterDesign::IIRFilterDomain::DIGITAL);
    iirFilter(ba, lzeroPhase);
    return;
}

void Waveform::sosBandstopFilter(const int order,
                                 const std::pair<double,double> fc,
                                 const IIRPrototype prototype,
                                 const double ripple,
                                 const bool lzeroPhase)
{
    // Check that there's data
    int len = pImpl->getLengthOfInputSignal();
    if (len < 1)
    {
        RTSEIS_WARNMSG("%s", "No data is set on the module");
        return;
    }
    // Compute normalized frequencies
    std::pair<double,double> r
        = computeNormalizedFrequencyFromSamplingPeriod(fc, pImpl->dt);
    Utilities::FilterDesign::IIRPrototype ptype;
    ptype = classifyIIRPrototype(prototype);
    Utilities::FilterRepresentations::SOS sos;
    pImpl->filterDesigner.designBandstopIIRFilter(
                    order, r, ptype, ripple, sos,
                    Utilities::FilterDesign::SOSPairing::NEAREST,
                    Utilities::FilterDesign::IIRFilterDomain::DIGITAL);
    sosFilter(sos, lzeroPhase);
    return;
}

//----------------------------------------------------------------------------//
//                               General Filtering                            //
//----------------------------------------------------------------------------//

void Waveform::firFilter(const Utilities::FilterRepresentations::FIR &fir,
                         const bool lremovePhase)
{
    // Check that there's data
    int len = pImpl->getLengthOfInputSignal();
    if (len < 1)
    {
        RTSEIS_WARNMSG("%s", "No data is set on the module");
        return;
    }
    // Initialize the FIR filter
    const std::vector<double> taps = fir.getFilterTaps();
    const int nb = static_cast<int> (taps.size());
    if (nb < 1)
    {
        RTSEIS_THROW_IA("%s", "No filter taps");
        return;
    }
    // Initialize filter
    RTSeis::Utilities::FilterImplementations::FIRFilter firFilter;
    firFilter.initialize(nb, taps.data(),
                   ProcessingMode::POST_PROCESSING,
                   Precision::DOUBLE,
                   Utilities::FilterImplementations::FIRImplementation::DIRECT);
    pImpl->resizeOutputData(len);
    // Zero-phase filtering needs workspace so that x isn't annihalated
    if (lremovePhase)
    {
        std::vector<double> xwork(len);
        copy(pImpl->x_, xwork);
        firFilter.apply(len, xwork.data(), pImpl->y_.data());
        reverse(pImpl->y_, xwork);
        firFilter.apply(len, pImpl->x_.data(), pImpl->y_.data());
        reverse(pImpl->y_);
    }
    else
    {
        firFilter.apply(len, pImpl->x_.data(), pImpl->y_.data());
    }
    return;
}

void Waveform::iirFilter(const Utilities::FilterRepresentations::BA &ba,
                         const bool lremovePhase)
{
    // Check that there's data
    int len = pImpl->getLengthOfInputSignal();
    if (len < 1)
    {
        RTSEIS_WARNMSG("%s", "No data is set on the module");
        return;
    }
    // Initialize the IIR filter
    const std::vector<double> b = ba.getNumeratorCoefficients();
    const std::vector<double> a = ba.getDenominatorCoefficients();
    const int nb = static_cast<int> (b.size());
    const int na = static_cast<int> (a.size());
    if (nb < 1 || na < 1)
    {
        if (na < 1){RTSEIS_THROW_IA("%s", "No denominator coefficients");}
        if (nb < 1){RTSEIS_THROW_IA("%s", "No numerator coefficients");}
        RTSEIS_THROW_IA("%s", "No filter coefficients");
        return;
    }
    // Initialize filter
    if (!lremovePhase)
    {
        RTSeis::Utilities::FilterImplementations::IIRFilter iirFilter;
        iirFilter.initialize(nb, b.data(),
               na, a.data(),
               ProcessingMode::POST_PROCESSING,
               Precision::DOUBLE,
               Utilities::FilterImplementations::IIRDFImplementation::DF2_FAST);
        pImpl->resizeOutputData(len);
        iirFilter.apply(len, pImpl->x_.data(), pImpl->y_.data());
    }
    else
    {
        RTSeis::Utilities::FilterImplementations::IIRIIRFilter iiriirFilter;
        iiriirFilter.initialize(nb, b.data(),
                                na, a.data(),
                                Precision::DOUBLE);
        pImpl->resizeOutputData(len);
        iiriirFilter.apply(len, pImpl->x_.data(), pImpl->y_.data());
    }
    return;
}

void Waveform::sosFilter(const Utilities::FilterRepresentations::SOS &sos,
                         const bool lremovePhase)
{
    // Check that there's data
    int len = pImpl->getLengthOfInputSignal();
    if (len < 1)
    {
        RTSEIS_WARNMSG("%s", "No data is set on the module");
        return;
    }
    // Initialize the FIR filter
    const int ns = sos.getNumberOfSections();
    if (ns < 1)
    {
        RTSEIS_THROW_IA("%s", "No sections in fitler");
    }
    const std::vector<double> bs = sos.getNumeratorCoefficients();
    const std::vector<double> as = sos.getDenominatorCoefficients();
    // Initialize filter
    RTSeis::Utilities::FilterImplementations::SOSFilter sosFilter;
    sosFilter.initialize(ns, bs.data(), as.data(),
                         ProcessingMode::POST_PROCESSING,
                         Precision::DOUBLE);
    pImpl->resizeOutputData(len);
    // Zero-phase filtering needs workspace so that x isn't annihalated
    if (lremovePhase)
    {
        std::vector<double> xwork(len);
        copy(pImpl->x_, xwork);
        sosFilter.apply(len, xwork.data(), pImpl->y_.data());
        reverse(pImpl->y_, xwork);
        sosFilter.apply(len, pImpl->x_.data(), pImpl->y_.data());
        reverse(pImpl->y_);
    }
    else
    {
        sosFilter.apply(len, pImpl->x_.data(), pImpl->y_.data());
    }
    return;
}

//----------------------------------------------------------------------------//
//                                   Tapering                                 //
//----------------------------------------------------------------------------//

void Waveform::taper(const double pct,
                     const TaperParameters::Type window)
{
    int len = pImpl->getLengthOfInputSignal();
    if (len < 1)
    {
        RTSEIS_WARNMSG("%s", "No data is set on the module");
        return;
    }
    // Taper the data
    TaperParameters parms(pct, window, RTSeis::Precision::DOUBLE);
    Taper taper(parms);
    const double *x = pImpl->getInputDataPointer();
    pImpl->resizeOutputData(len);
    double  *y = pImpl->getOutputDataPointer();
    taper.apply(len, x, y);
    return;
}

//----------------------------------------------------------------------------//
//                              Private Functions                             //
//----------------------------------------------------------------------------//
std::pair<double,double>
computeNormalizedFrequencyFromSamplingPeriod(const std::pair<double,double> fc,
                                            const double dt)
{
    std::pair<double,double> r(0, 0);
    double fnyq = computeNyquistFrequencyFromSamplingPeriod(dt);
    if (fc.first < 0)
    {
        RTSEIS_THROW_IA("fc.first=%lf must be positive", fc.first);
    }
    if (fc.first >= fc.second) 
    {
        RTSEIS_THROW_IA("fc.first=%lf must be less than fc.second=%lf",
                        fc.first, fc.second);
    }
    if (fc.second > fnyq)
    {
        RTSEIS_THROW_IA("fc.seoncd=%lf must be in range [%lf,%lf]",
                        fc.second, fc.first, fnyq);
    }
    r.first  = fc.first/fnyq;
    r.second = fc.second/fnyq;
    return r;
}

double computeNormalizedFrequencyFromSamplingPeriod(const double fc,
                                                    const double dt)
{
    double r = 0;
    double fnyq = computeNyquistFrequencyFromSamplingPeriod(dt); 
    if (fc < 0 || fc > fnyq)
    {
        RTSEIS_THROW_IA("fc=%lf must be in range [0,%lf]", fc, fnyq);
    }
    r = fc/fnyq;
    return r;
}
double computeNyquistFrequencyFromSamplingPeriod(const double dt)
{
#ifdef DEBUG
    assert(dt > 0);
#endif
    return 1.0/(2.0*dt);
}
Utilities::Math::Convolve::Mode
classifyConvolveMode(const ConvolutionMode mode)
{
    Utilities::Math::Convolve::Mode convcorMode;
    if (mode == ConvolutionMode::FULL)
    {
        convcorMode = Utilities::Math::Convolve::Mode::FULL;
    }
    else if (mode == ConvolutionMode::SAME)
    {
        convcorMode = Utilities::Math::Convolve::Mode::SAME;
    }
    else if (mode == ConvolutionMode::VALID)
    {
        convcorMode = Utilities::Math::Convolve::Mode::VALID;
    }
    else
    {
        RTSEIS_THROW_IA("Unsupported convolution mode=%d",
                        static_cast<int> (mode));
    }
    return convcorMode;
}

Utilities::Math::Convolve::Implementation 
classifyConvolveImplementation(const ConvolutionImplementation implementation)
{
    // Classify the convolution implementaiton
    Utilities::Math::Convolve::Implementation convcorImpl;
    if (implementation == ConvolutionImplementation::AUTO)
    {   
        convcorImpl = Utilities::Math::Convolve::Implementation::AUTO;
    }   
    else if (implementation == ConvolutionImplementation::DIRECT)
    {   
        convcorImpl = Utilities::Math::Convolve::Implementation::DIRECT;
    }   
    else if (implementation == ConvolutionImplementation::FFT)
    {   
        convcorImpl = Utilities::Math::Convolve::Implementation::FFT;
    }   
    else
    {   
        RTSEIS_THROW_IA("Unsupported convolution implementation=%d",
                        static_cast<int> (implementation));
    }
    return convcorImpl;
}

Utilities::FilterDesign::IIRPrototype
classifyIIRPrototype(const IIRPrototype prototype)
{
    Utilities::FilterDesign::IIRPrototype ptype;
    if (prototype == IIRPrototype::BESSEL)
    {
        ptype = Utilities::FilterDesign::IIRPrototype::BESSEL;
    }
    else if (prototype == IIRPrototype::BUTTERWORTH)
    {
        ptype = Utilities::FilterDesign::IIRPrototype::BUTTERWORTH;
    }
    else if (prototype == IIRPrototype::CHEBYSHEV1)
    {
        ptype = Utilities::FilterDesign::IIRPrototype::CHEBYSHEV1;
    }
    else if (prototype == IIRPrototype::CHEBYSHEV2)
    {
        ptype = Utilities::FilterDesign::IIRPrototype::CHEBYSHEV2;
    }
    else
    {
        RTSEIS_THROW_IA("Unsupported prototype=%d",
                        static_cast<int> (prototype));
    }
    return ptype;
}
