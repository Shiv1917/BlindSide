#include "frontend.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>

namespace rx {

static double rrcValue(double t, double beta) {
    const double eps = 1e-9;
    if (std::fabs(t) < eps)
        return 1.0 - beta + 4.0 * beta / M_PI;
    if (std::fabs(std::fabs(4.0 * beta * t) - 1.0) < eps)
        return beta / std::sqrt(2.0) *
               ((1.0 + 2.0 / M_PI) * std::sin(M_PI / (4.0 * beta)) +
                (1.0 - 2.0 / M_PI) * std::cos(M_PI / (4.0 * beta)));
    double num = std::sin(M_PI * t * (1.0 - beta)) + 4.0 * beta * t * std::cos(M_PI * t * (1.0 + beta));
    double den = M_PI * t * (1.0 - (4.0 * beta * t) * (4.0 * beta * t));
    return num / den;
}

std::vector<Cx> correctCfo(const std::vector<Cx>& r, double fs, double cfoHz, double phaseRad) {
    std::vector<Cx> out(r.size());
    for (size_t i = 0; i < r.size(); ++i) {
        double phase = -2.0 * M_PI * cfoHz * i / fs - phaseRad;
        out[i] = r[i] * std::polar(1.0, phase);
    }
    return out;
}

std::vector<Cx> applyMatchedFilter(const std::vector<Cx>& r, double fs, double baudHz, double beta) {
    double sps = fs / baudHz;
    int span = 10; 
    int L = 2 * span * (int)std::round(sps) + 1;
    int c = span * (int)std::round(sps);
    
    std::vector<double> h(L);
    double energy = 0.0;
    for (int j = 0; j < L; ++j) {
        double t = (j - c) / sps;
        h[j] = rrcValue(t, beta);
        energy += h[j] * h[j];
    }
    double scale = std::sqrt(1.0 / energy);
    for (auto& v : h) v *= scale;

    std::vector<Cx> out(r.size(), Cx(0, 0));
    for (size_t m = 0; m < r.size(); ++m) {
        Cx acc(0, 0);
        for (int j = 0; j < L; ++j) {
            int n = m + j - c;
            if (n >= 0 && n < (int)r.size()) acc += r[n] * h[j];
        }
        out[m] = acc;
    }
    return out;
}

std::vector<Cx> searchBestTiming(const std::vector<Cx>& mf_out, double fs, double baudHz, int oversample) {
    double T = 1.0 / baudHz;
    double sps = fs / baudHz;
    
    size_t best_frames = 0;
    double best_offset = 0.0;
    std::vector<Cx> best_symbols;

    for (int k = 0; k < oversample; ++k) {
        double offset_sec = (double)k / oversample * T;
        double offset_samples = offset_sec * fs;
        
        std::vector<Cx> symbols;
        symbols.reserve(mf_out.size() / (size_t)sps + 10);
        
        // Sample at the current fractional delay
        for (double idx = offset_samples; idx < mf_out.size(); idx += sps) {
            size_t i0 = (size_t)idx;
            size_t i1 = i0 + 1;
            if (i1 >= mf_out.size()) break;
            double frac = idx - i0;
            Cx val = mf_out[i0] * (1.0 - frac) + mf_out[i1] * frac;
            symbols.push_back(val);
        }
        
        // Continuous 2nd-order PLL (NDA QPSK)
        double phase = 0.0;
        double freq = 0.0;
        double alpha = 0.1;    // Phase gain
        double beta = 0.001;   // Frequency gain
        
        for (auto& s : symbols) {
            s *= std::polar(1.0, -phase);
            Cx s4 = s * s * s * s;
            // QPSK 4th power targets -1 (angle = pi).
            // arg(-s4) gives the deviation from this target.
            double err = std::arg(-s4) / 4.0; 
            phase += freq + alpha * err;
            freq += beta * err;
        }
        
        if (k == 0 || k == 8) {
            std::cout << "    [Phase Track] offset=" << k << " freq_est=" << freq * baudHz << " Hz\n";
        }
        
        auto frames = receiveSymbols(symbols);
        if (frames.size() > best_frames) {
            best_frames = frames.size();
            best_offset = offset_sec;
            best_symbols = symbols;
        }
    }
    
    std::cout << "  [Timing Search] Best offset = " << best_offset * 1e9 << " ns, "
              << "Frames = " << best_frames << " (from " << (int)oversample << " phases)\n";
              
    return best_symbols;
}
}