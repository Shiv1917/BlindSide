// rolloff.hpp
#pragma once
#include <complex>
#include <vector>

namespace rx {
using Cx = std::complex<double>;

struct RolloffEstimate {
    double beta = 0.0;
    double centerFreqHz = 0.0;
    double confidence = 0.0; // Ratio of best fit to worst fit
};

RolloffEstimate estimateRolloff(const std::vector<Cx>& r, double fs, double baudHz);
}