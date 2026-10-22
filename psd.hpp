// psd.hpp
#pragma once
#include <complex>
#include <vector>

namespace rx {
using Cx = std::complex<double>;

struct PsdResult {
    std::vector<double> power; // PSD values
    std::vector<double> freq;  // Corresponding frequencies in Hz
    double df;                 // Frequency resolution
};

PsdResult computeWelchPsd(const std::vector<Cx>& r, double fs, size_t Nfft = 8192);
}