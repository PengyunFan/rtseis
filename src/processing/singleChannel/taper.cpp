#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <exception>
#include <memory>
#include <ipps.h>
#include "rtseis/postProcessing/singleChannel/taper.hpp"
#include "rtseis/utilities/windowFunctions.hpp"
#include "rtseis/enums.h"

using namespace RTSeis::PostProcessing::SingleChannel;

struct TaperParameters::TaperParametersImpl
{
    /// Percentage of signal to apply taper to
    double pct = 5;
    /// Taper type.
    Type type = Type::HAMMING;
    /// Precision 
    RTSeis::Precision precision = RTSeis::Precision::DOUBLE; 
    /// The processing mode can't be toggled
    const RTSeis::ProcessingMode mode = RTSeis::ProcessingMode::POST_PROCESSING;
};

struct Taper::TaperImpl
{
    public:
        TaperParameters parms; 
        std::vector<double> w8;
        std::vector<float>  w4;
        int winLen0 =-1;
};

TaperParameters::TaperParameters(const double pct,
                                 const Type type,
                                 const RTSeis::Precision precision) :
    pImpl(new TaperParametersImpl())
{
    setPercentage(pct);
    setTaperType(type);
    setPrecision(precision);
    return;
}

TaperParameters::TaperParameters(const TaperParameters &parms)
{
    *this = parms;
    return;
}

TaperParameters::TaperParameters(TaperParameters &&parms)
{
    *this = std::move(parms);
    return;
}

TaperParameters& TaperParameters::operator=(const TaperParameters &parms)
{
    if (&parms == this){return *this;}
    pImpl = std::unique_ptr<TaperParametersImpl> (new TaperParametersImpl());
    pImpl->pct = parms.pImpl->pct;
    pImpl->type = parms.pImpl->type;
    pImpl->precision = parms.pImpl->precision;
    return *this; 
}

TaperParameters& TaperParameters::operator=(TaperParameters &&parms)
{
    if (&parms == this){return *this;}
    pImpl = std::move(parms.pImpl);
    return *this;
}

TaperParameters::~TaperParameters(void)
{
    return;
}

void TaperParameters::setPrecision(const RTSeis::Precision precision)
{
    pImpl->precision = precision;
}

RTSeis::Precision TaperParameters::getPrecision(void) const
{
    return pImpl->precision;
}

void TaperParameters::setTaperType(const Type type)
{
    pImpl->type = type;
    return;
}

TaperParameters::Type TaperParameters::getTaperType(void) const
{
    return pImpl->type;
}

void TaperParameters::setPercentage(const double pct)
{
    if (pct < 0 || pct > 100)
    {
        throw std::invalid_argument("Percentage must be in range [0,100]");
    }
    pImpl->pct = pct;
    return;
}

double TaperParameters::getPercentage(void) const
{
    return pImpl->pct;
}

void TaperParameters::clear(void)
{
    if (pImpl)
    {
        pImpl->pct = 5;
        pImpl->type = Type::HAMMING;
        pImpl->precision = RTSeis::Precision::DOUBLE;
    }
    return;
}

bool TaperParameters::isValid(void) const
{
    if (pImpl->pct < 0 || pImpl->pct > 100){return false;}
    return true;
}

//============================================================================//
//                                    Tapering                                //
//============================================================================//

Taper::Taper(void) :
    pImpl(new TaperImpl())
{
    return;
}

Taper::Taper(const TaperParameters &parameters) :
    pImpl(new TaperImpl())
{
    setParameters(parameters);
    return;
}

Taper::~Taper(void)
{
    clear();
    return;
}

void Taper::clear(void)
{
    if (pImpl)
    {
        pImpl->parms.clear();
        pImpl->w8.clear();
        pImpl->w4.clear();
        pImpl->winLen0 =-1;
    }
    return;
}

Taper& Taper::operator=(const Taper &taper)
{
    if (&taper == this){return *this;}
    clear(); //if (pImpl){clear();}
    pImpl = std::unique_ptr<TaperImpl> (new TaperImpl()); 
    pImpl->parms = taper.pImpl->parms;
    pImpl->w8 = taper.pImpl->w8;
    pImpl->w4 = taper.pImpl->w4;
    pImpl->winLen0 = taper.pImpl->winLen0;
    return *this;
}

void Taper::setParameters(const TaperParameters &parameters)
{
    clear(); // Sets winLen0 to -1
    if (!parameters.isValid())
    {
        throw std::invalid_argument("Taper parameters are invalid");
        return;
    }
    pImpl->parms = parameters;
    return;
}

void Taper::apply(const int nx, const double x[], double y[])
{
    if (nx <= 0){return;}
    if (x == nullptr || y == nullptr)
    {
        if (x == nullptr){throw std::invalid_argument("x is NULL");}
        if (y == nullptr){throw std::invalid_argument("y is NULL");}
        throw std::invalid_argument("Invalid arrays");
    }
    // Deal with an edge case
    if (nx < 3)
    {
        y[0] = 0;
        if (nx == 2){y[1] = 0;}
        return;
    }
    // Compute taper length
    double pct = pImpl->parms.getPercentage();
    int npct = static_cast<int> (static_cast<double> (nx)*pct + 0.5); // Round
    int m = std::min(nx - 2, npct);
    // Redesign the window?  If the parameters were (re)set then winLen0 is -1.
    // Otherwise, if the same length signal is coming at us then the precision
    // of the module can't change so we can just use the old window.
    if (pImpl->winLen0 != m)
    {
        pImpl->w8.reserve(m+2); // Prevent reallocations in a bit
        TaperParameters::Type type = pImpl->parms.getTaperType();
        if (type == TaperParameters::Type::HAMMING)
        {
            RTSeis::Utilities::WindowFunctions::hamming(m, pImpl->w8);
        }
        else if (type == TaperParameters::Type::BLACKMAN)
        {
            RTSeis::Utilities::WindowFunctions::blackman(m, pImpl->w8);
        }
        else if (type == TaperParameters::Type::HANN)
        {
            RTSeis::Utilities::WindowFunctions::hann(m, pImpl->w8);
        }
        else if (type == TaperParameters::Type::BARTLETT)
        {
            RTSeis::Utilities::WindowFunctions::bartlett(m, pImpl->w8);
        }
        else if (type == TaperParameters::SINE)
        {
            RTSeis::Utilities::WindowFunctions::sine(m, pImpl->w8);
        }
        else
        {
            throw std::invalid_argument("Unsupported window");
        }
        // Following SAC definition the end points are set to 0
        pImpl->w8.emplace(pImpl->w8.begin(), 0);
        pImpl->w8.emplace_back(0);
    }
    // Taper first (m+1)/2 points
    int mp12 = (m + 1)/2;
    const double *w = pImpl->w8.data();
    ippsMul_64f(w, x, y, mp12);
    // Copy the intermediate portion of the signal
    int ncopy = nx - mp12 - mp12; // Subtract out two window lengths
    if (ncopy > 0)
    {
        ippsCopy_64f(&x[mp12], &y[mp12], ncopy);
    } 
    // Taper last (m+1)/2 points 
    ippsMul_64f(&w[m+2-mp12], &x[nx-mp12], &y[nx-mp12], mp12);
    return;
}