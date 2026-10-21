// symbol_rate.hpp
#pragma once
#include <complex>
#include <vector>

namespace rx {
using Cx = std::complex<double>;

struct SymbolRateEstimate {
    double baudHz          = 0.0;
    double sps             = 0.0;   // fs / baudHz
    double confidence      = 0.0;   // peak / median-of-search-band
    double peakFrequencyHz = 0.0;
};

SymbolRateEstimate estimateSymbolRate(const std::vector<Cx>& r,
                                      double fs,
                                      double minBaudHz,
                                      double maxBaudHz);
}