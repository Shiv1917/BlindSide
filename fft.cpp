// fft.cpp
#include "fft.hpp"
#include <cmath>
#include <stdexcept>

namespace rx {
void fftRadix2(std::vector<Cx>& x, bool inverse) {
    const size_t n = x.size();
    if (n <= 1) return;
    if ((n & (n - 1)) != 0)
        throw std::runtime_error("FFT size must be a power of 2");

    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2.0 * M_PI / (double)len * (inverse ? +1.0 : -1.0);
        const Cx wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            Cx w(1);
            for (size_t k = 0; k < len / 2; ++k) {
                Cx u = x[i + k];
                Cx v = x[i + k + len / 2] * w;
                x[i + k]           = u + v;
                x[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
    if (inverse)
        for (auto& v : x) v /= (double)n;
}
}