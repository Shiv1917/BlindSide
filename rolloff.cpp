#include "rolloff.hpp"
#include "psd.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>

namespace rx {

// Theoretical RC spectrum (baseband, center = 0)
static double rcSpectrum(double f, double T, double beta) {
    double fabs = std::abs(f);
    double T2 = 1.0 / (2.0 * T);
    if (fabs <= (1.0 - beta) * T2) return 1.0;
    if (fabs > (1.0 + beta) * T2) return 0.0;
    return 0.5 * (1.0 + std::cos(M_PI * T / beta * (fabs - (1.0 - beta) * T2)));
}

RolloffEstimate estimateRolloff(const std::vector<Cx>& r, double fs, double baudHz) {
    RolloffEstimate est;
    PsdResult psd = computeWelchPsd(r, fs);
    if (psd.power.empty()) return est;

    // 1. Find center frequency (centroid of PSD)
    double sumF = 0.0, sumP = 0.0;
    for (size_t i = 0; i < psd.power.size(); ++i) {
        sumF += psd.freq[i] * psd.power[i];
        sumP += psd.power[i];
    }
    est.centerFreqHz = (sumP > 0) ? (sumF / sumP) : 0.0;

    // 2. Estimate noise floor via histogram mode
    double minP = psd.power[0], maxP = psd.power[0];
    for (double p : psd.power) {
        minP = std::min(minP, p);
        maxP = std::max(maxP, p);
    }
    const int numBins = 100;
    std::vector<int> hist(numBins, 0);
    for (double p : psd.power) {
        int b = static_cast<int>((p - minP) / (maxP - minP + 1e-12) * (numBins - 1));
        hist[std::max(0, std::min(numBins - 1, b))]++;
    }
    int maxCount = 0, modeBin = 0;
    for (int i = 0; i < numBins; ++i) {
        if (hist[i] > maxCount) { maxCount = hist[i]; modeBin = i; }
    }
    double noiseFloor = minP + (modeBin + 0.5) * (maxP - minP) / numBins;

    // 3. Grid search beta
    double bestBeta = 0.0, bestErr = 1e30;
    double worstErr = 0.0;
    double T = 1.0 / baudHz;
    
    // We only evaluate over a band around the center (e.g., ±1.2 * baudHz)
    size_t startIdx = 0, endIdx = psd.power.size() - 1;
    while(startIdx < psd.power.size() && psd.freq[startIdx] < est.centerFreqHz - 1.2 * baudHz) startIdx++;
    while(endIdx > 0 && psd.freq[endIdx] > est.centerFreqHz + 1.2 * baudHz) endIdx--;
    
    if (startIdx >= endIdx) return est;

    // Extract measured vector (noise subtracted, clipped at 0)
    std::vector<double> m;
    for (size_t i = startIdx; i <= endIdx; ++i) {
        m.push_back(std::max(0.0, psd.power[i] - noiseFloor));
    }

    for (double beta = 0.05; beta <= 0.95; beta += 0.01) {
        // Build theoretical curve
        std::vector<double> t;
        for (size_t i = startIdx; i <= endIdx; ++i) {
            double f_shifted = psd.freq[i] - est.centerFreqHz;
            t.push_back(rcSpectrum(f_shifted, T, beta));
        }

        // Linear least squares scale: <m, t> / <t, t>
        double num = 0.0, den = 0.0;
        for (size_t i = 0; i < m.size(); ++i) {
            num += m[i] * t[i];
            den += t[i] * t[i];
        }
        double scale = (den > 0) ? (num / den) : 0.0;

        // Compute MSE
        double err = 0.0;
        for (size_t i = 0; i < m.size(); ++i) {
            double diff = m[i] - scale * t[i];
            err += diff * diff;
        }
        
        if (err < bestErr) { bestErr = err; bestBeta = beta; }
        worstErr = std::max(worstErr, err);
    }

    est.beta = bestBeta;
    est.confidence = (bestErr > 0) ? (worstErr / bestErr) : 999.0;
    return est;
}
}