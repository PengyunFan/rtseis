#include <cstdio>
#include <cstdlib>
#include <complex>
#include <vector>
#include <functional>
#include <algorithm>
#ifdef __INTEL_COMPILER
#include <pstl/algorithm>
#include <pstl/execution>
#endif
#define RTSEIS_LOGGING 1
#include "rtseis/log.h"
#include "rtseis/utilities/math/vectorMath.hpp"
#include <ipps.h>

int RTSeis::Utilities::Math::VectorMath::divide(
    const std::vector<std::complex<double>> &den,
    const std::vector<std::complex<double>> &num,
    std::vector<std::complex<double>> &res)
{
    int nDen = static_cast<int> (den.size());
    int nNum = static_cast<int> (num.size());
    if (nNum != nDen)
    {
        RTSEIS_ERRMSG("num has length = %d while den has length = %d",
                      nNum, nDen);
        return -1;
    }
    res.resize(nNum);
    if (nNum <= 0){return 0;}
#ifdef __INTEL_COMPILER
    std::transform(pstl::execution::unseq,
                   num.data(), num.data()+nNum, den.data(), res.data(),
                   std::divides< std::complex<double> > ());
#else
    const Ipp64fc *pSrc1 = static_cast<const Ipp64fc *>
                           (static_cast<const void *> (den.data()));
    const Ipp64fc *pSrc2 = static_cast<const Ipp64fc *>
                           (static_cast<const void *> (num.data()));
    Ipp64fc *pDst  = static_cast<Ipp64fc *> (static_cast<void *> (res.data()));
    IppStatus status = ippsDiv_64fc(pSrc1, pSrc2, pDst, nNum); 
    if (status != ippStsNoErr)
    {
        RTSEIS_ERRMSG("%s", "Division failed");
        return -1;
    }
#endif
    return 0;
}
//============================================================================//

int RTSeis::Utilities::Math::VectorMath::real(
    const std::vector<std::complex<double>> &z,
    std::vector<double> &r)
{
    int n = static_cast<int> (z.size());
    r.resize(n);
    if (n <= 0){return 0;}
    const Ipp64fc *pSrc = static_cast<const Ipp64fc *>
                          (static_cast<const void *> (z.data()));
    Ipp64f *pDst  = r.data();
    IppStatus status = ippsReal_64fc(pSrc, pDst, n); 
    if (status != ippStsNoErr)
    {   
        RTSEIS_ERRMSG("%s", "Division failed");
        return -1; 
    }   
    return 0;
}

//============================================================================//
template<typename T> int RTSeis::Utilities::Math::VectorMath::copysign(
    const  std::vector<T> x, std::vector<T> &y)
{
    int nx = static_cast<int> (x.size());
    y.resize(nx);
    if (nx <= 0){return 0;}
    int ierr = copysign(nx, x.data(), y.data());
    if (ierr != 0){y.resize(0);}
    return ierr;
}

template<typename T> int RTSeis::Utilities::Math::VectorMath::copysign(
    const int n, const T x[], T y[])
{
    if (n <= 0){return 0;}
    if (x == nullptr || y == nullptr)
    {
        if (x == nullptr){RTSEIS_ERRMSG("%s", "x is NULL");}
        if (y == nullptr){RTSEIS_ERRMSG("%s", "y is NULL");}
        return -1;
    }
    constexpr T one = 1;
    #pragma omp simd
    for (int i=0; i<n; i++){y[i] = std::copysign(one, x[i]);}
    return 0;
}
// Instantiate the templates in the library
template int RTSeis::Utilities::Math::VectorMath::copysign<double>(
    const std::vector<double> x, std::vector<double> &y);
template int RTSeis::Utilities::Math::VectorMath::copysign<double>(
    const int n, const double x[], double y[]);
template int RTSeis::Utilities::Math::VectorMath::copysign<float>(
    const std::vector<float> x, std::vector<float> &y);
template int RTSeis::Utilities::Math::VectorMath::copysign<float>(
    const int n, const float x[], float y[]);

//============================================================================//
