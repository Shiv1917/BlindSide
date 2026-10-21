// fft.hpp
#pragma once
#include <complex>
#include <vector>

namespace rx {
using Cx = std::complex<double>;

// In-place radix-2 Cooley-Tukey FFT. n must be a power of 2.
void fftRadix2(std::vector<Cx>& x, bool inverse);
}
