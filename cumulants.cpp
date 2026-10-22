#include "cumulants.hpp"
#include <cmath>

namespace rx {

PowerEstimate estimatePowerRatioQPSK(const std::vector<Cx>& samples) {
    PowerEstimate est;
    const size_t N = samples.size();
    if (N < 1000) return est;

    double m21 = 0.0, m42 = 0.0;
    Cx m20(0, 0);
    for (const auto& y : samples) {
        double y2 = std::norm(y);
        m21 += y2;
        m42 += y2 * y2;
        m20 += y * y;
    }
    m21 /= N;
    m42 /= N;
    m20 /= N;

    est.m21 = m21;
    est.m42 = m42;
    if (m21 < 1e-12) return est;

    // C42 = m42 - |m20|^2 - 2*m21^2
    double c42 = m42 - std::norm(m20) - 2.0 * m21 * m21;
    est.c42_raw  = c42;
    est.c42_norm = c42 / (m21 * m21);

    // Invert C42_norm = -(r^2+1)/(r+1)^2
    double target = -est.c42_norm;
    if (target < 0.5)   target = 0.5;
    if (target > 0.9999) target = 0.9999;

    double disc = 2.0 * target - 1.0;
    if (disc < 0) disc = 0;
    double r = (target + std::sqrt(disc)) / (1.0 - target);
    est.power_ratio = r;
    est.pd_db       = 10.0 * std::log10(r);
    est.is_sc       = (est.pd_db > 10.0);
    return est;
}
}