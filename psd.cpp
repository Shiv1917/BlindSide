// psd.cpp
#include "psd.hpp"
#include "fft.hpp"
#include <cmath>
#include <algorithm>

namespace rx {
PsdResult computeWelchPsd(const std::vector<Cx>& r, double fs, size_t Nfft) {
    PsdResult res;
    const size_t N = r.size();
    if (N < Nfft) return res;

    const size_t hop = Nfft / 2;
    const size_t nSeg = (N - Nfft) / hop + 1;
    size_t halfN = Nfft / 2 + 1;

    res.power.assign(halfN, 0.0);
    res.freq.assign(halfN, 0.0);
    res.df = fs / (double)Nfft;

    // Precompute Hann window
    std::vector<double> window(Nfft);
    double winPow = 0.0;
    for (size_t i = 0; i < Nfft; ++i) {
        window[i] = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (Nfft - 1));
        winPow += window[i] * window[i];
    }

    std::vector<Cx> buf(Nfft);
    for (size_t s = 0; s < nSeg; ++s) {
        for (size_t i = 0; i < Nfft; ++i) {
            buf[i] = r[s * hop + i] * window[i];
        }
        fftRadix2(buf, false);
        for (size_t k = 0; k < halfN; ++k) {
            res.power[k] += std::norm(buf[k]);
        }
    }

    // Normalize PSD
    for (size_t k = 0; k < halfN; ++k) {
        res.power[k] /= (nSeg * fs * winPow);
        res.freq[k]  = (k == 0) ? 0.0 : (k == Nfft/2 ? fs/2.0 : k * res.df);
    }
    
    // Shift so DC is in the middle (optional, but easier for peak searching)
    // For real baseband we just leave it as is, but for complex IQ, we want [-fs/2, fs/2]
    // Let's shift it:
    size_t shift = halfN - 1;
    std::vector<double> shiftedP(2 * shift), shiftedF(2 * shift);
    for (size_t i = 0; i < shift; ++i) {
        shiftedP[i] = res.power[shift - i];
        shiftedF[i] = -res.freq[shift - i];
        
        shiftedP[shift + i] = res.power[i];
        shiftedF[shift + i] = res.freq[i];
    }
    shiftedP[shift] = res.power[0];
    shiftedF[shift] = 0.0;

    res.power = shiftedP;
    res.freq  = shiftedF;
    return res;
}
}