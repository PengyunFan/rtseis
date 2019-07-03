#ifndef RTSEIS_UTILITIES_TRANSFORMS_ENUMS_HPP
#define RTSEIS_UTILITIES_TRANSFORMS_ENUMS_HPP 1

namespace RTSeis::Utilities::Transforms 
{
/*!
 * @brief Defines the Fourier transform implementation.
 */
enum class FourierTransformImplementation
{
    DFT, /*!< Perform a Discrete Fourier Transform computation. */
    FFT  /*!< Force an Fast Fouerier Transform computation.  The 
              implementation will have to zero-pad the signal so that
              its length is a power of 2. */
};
/*!
 * @brief Defines the detrending strategy used by the short-time
 *        Fourier transform.
 */
enum class SlidingWindowDetrendType
{
    REMOVE_NONE,  /*!< Does not modify the data in each segment. */
    REMOVE_MEAN,  /*!< Removes the mean in each segment. */
    REMOVE_TREND  /*!< Removes the linear trend in each segment. */
};
}
#endif
