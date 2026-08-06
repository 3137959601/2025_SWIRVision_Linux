#ifndef LINEARSTRETCHMATH_H
#define LINEARSTRETCHMATH_H

#include <QtGlobal>
#include <algorithm>
#include <cmath>

namespace LinearStretchMath {

struct Parameters
{
    double k = 1.0;
    double b = 0.0;
    bool valid = false;
};

inline quint16 usbWordToDn13(quint16 value)
{
    return quint16(std::min(8191, int(value >> 3)));
}

inline Parameters calculate(double darkMean, double brightMean,
                            double targetBlack, double targetWhite)
{
    Parameters result;
    const double span = brightMean - darkMean;
    if (!(span > 0.0) || !(targetWhite > targetBlack))
        return result;

    result.k = (targetWhite - targetBlack) / span;
    result.b = targetBlack - result.k * darkMean;
    result.valid = std::isfinite(result.k) && std::isfinite(result.b);
    return result;
}

inline Parameters adjusted(const Parameters &base, double kRatio, double bOffset)
{
    Parameters result;
    if (!base.valid || !(kRatio > 0.0))
        return result;
    result.k = base.k * kRatio;
    // K和基础B使用同一倍率，保证bOffset为0时基础暗场仍映射到目标黑电平。
    result.b = base.b * kRatio + bOffset;
    result.valid = std::isfinite(result.k) && std::isfinite(result.b);
    return result;
}

inline quint16 apply13(quint16 input, const Parameters &parameters)
{
    if (!parameters.valid)
        return std::min<quint16>(input, 8191);
    const long value = std::lround(parameters.k * double(input) + parameters.b);
    return quint16(std::max<long>(0, std::min<long>(8191, value)));
}

} // namespace LinearStretchMath

#endif // LINEARSTRETCHMATH_H
