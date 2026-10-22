#pragma once
#include <complex>
#include <vector>

namespace rx {
using Cx = std::complex<double>;

struct FracDelayEstimate {
    double eps1 = 0.0;
    double eps2 = 0.0;
    double error = 0.0;
};

FracDelayEstimate estimateFractionalDelay(const std::vector<Cx>& y_mf, 
                                          double fs, double baudHz, double beta, 
                                          double P1, double P2, 
                                          double delta_f_hz);
}