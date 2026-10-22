#pragma once
#include <complex>
#include <vector>

namespace rx {
using Cx = std::complex<double>;

struct PowerEstimate {
    double m21       = 0.0;   // E[|y|^2]
    double m42       = 0.0;   // E[|y|^4]
    double c42_raw   = 0.0;
    double c42_norm  = 0.0;
    double power_ratio = 1.0; // r = P1 / P2
    double pd_db     = 0.0;
    bool   is_sc     = false;
};

PowerEstimate estimatePowerRatioQPSK(const std::vector<Cx>& samples);
}