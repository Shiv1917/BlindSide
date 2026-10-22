#pragma once
#include <complex>
#include <vector>

namespace rx {
using Cx = std::complex<double>;

struct CfoEstimate {
    double f1_hz = 0.0;
    double f2_hz = 0.0;
    int    p_order = 0;      // The power p that yielded the peak
    double confidence = 0.0; // Peak height / median height
    double phase1_rad = 0.0;
    double phase2_rad = 0.0;
};

// Note: baudHz is added to restrict the search band
CfoEstimate estimateCfo(const std::vector<Cx>& r, double fs, double baudHz);
}