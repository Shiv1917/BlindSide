// symbol_rate.cpp
#include "symbol_rate.hpp"
#include "fft.hpp"
#include <algorithm>
#include <cmath>

namespace rx {

SymbolRateEstimate estimateSymbolRate(const std::vector<Cx>& r,
                                      double fs,
                                      double minBaudHz,
                                      double maxBaudHz) {
    SymbolRateEstimate est;
    const size_t N = r.size();
    if (N < 512) return est;

    // FFT size: 8192 (or as large as input allows, down to 512)
    size_t Nfft = 8192;
    while (Nfft > N && Nfft > 512) Nfft >>= 1;
    if (Nfft < 512) return est;

    const size_t hop   = Nfft / 2;                     // 50% overlap
    const size_t nSeg  = (N - Nfft) / hop + 1;
    std::vector<double> psd(Nfft / 2 + 1, 0.0);

    std::vector<Cx> buf(Nfft);
    for (size_t s = 0; s < nSeg; ++s) {
        double mean = 0.0;
        for (size_t i = 0; i < Nfft; ++i) {
            buf[i] = Cx(std::norm(r[s * hop + i]), 0.0);   // |r|^2
            mean  += buf[i].real();
        }
        mean /= (double)Nfft;
        for (size_t i = 0; i < Nfft; ++i) {
            const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (Nfft - 1));
            buf[i] = (buf[i] - mean) * w;
        }
        fftRadix2(buf, false);
        for (size_t k = 0; k <= Nfft / 2; ++k) psd[k] += std::norm(buf[k]);
    }
    for (auto& v : psd) v /= (double)nSeg;

    const double df   = fs / (double)Nfft;
    const size_t kmin = std::max<size_t>(1, (size_t)std::ceil (minBaudHz / df));
    const size_t kmax = std::min<size_t>(Nfft / 2, (size_t)std::floor(maxBaudHz / df));
    if (kmin >= kmax) return est;

    size_t kpeak = kmin;
    for (size_t k = kmin; k <= kmax; ++k) if (psd[k] > psd[kpeak]) kpeak = k;

    double delta = 0.0;                                 // parabolic refine
    if (kpeak > kmin && kpeak < kmax) {
        const double y0 = psd[kpeak - 1], y1 = psd[kpeak], y2 = psd[kpeak + 1];
        const double denom = y0 - 2.0 * y1 + y2;
        if (std::fabs(denom) > 1e-30) delta = 0.5 * (y0 - y2) / denom;
    }
    const double fPeak = (double)(kpeak + delta) * df;

    std::vector<double> band(psd.begin() + kmin, psd.begin() + kmax + 1);
    std::nth_element(band.begin(), band.begin() + band.size() / 2, band.end());
    const double med = band[band.size() / 2];

    est.baudHz          = fPeak;
    est.sps             = fs / fPeak;
    est.peakFrequencyHz = fPeak;
    est.confidence      = (med > 0.0) ? psd[kpeak] / med : 0.0;
    return est;
}
}