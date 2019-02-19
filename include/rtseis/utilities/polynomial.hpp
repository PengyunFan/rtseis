#ifndef RTSEIS_UTILS_POLYNOMIAL_HPP
#define RTSEIS_UTILS_POLYNOMIAL_HPP 1
#include <complex>
#include <vector>
#include "rtseis/config.h"

namespace RTSeis
{
namespace Utilities
{
namespace Math 
{

namespace Polynomial
{
    int roots(const std::vector<double> coeffs,
              std::vector<std::complex<double>> &roots);
    int poly(const std::vector<std::complex<double>> p,
             std::vector<std::complex<double>> &y);
    int poly(const std::vector<double> p, std::vector<double> &y);
    int polyval(const std::vector<double> p,
                const std::vector<double> x,
                std::vector<double> &y);
    int polyval(const std::vector<std::complex<double>> p,
                const std::vector<std::complex<double>> x,
                std::vector<std::complex<double>> &y);
}; /* End polynomial. */

}; /* End math. */
}; /* End utils. */
}; /* End rtseis. */

#endif
