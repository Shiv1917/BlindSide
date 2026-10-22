#include "cfo.hpp"
#include "fft.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>

namespace rx {

CfoEstimate estimateCfo(const std::vector<Cx>& r, double fs, double baudHz) {
    CfoEstimate est;
    const size_t N = r.size();
    if (N < 4096) return est;

    double power = 0.0;
    for (const auto& v : r) power += std::norm(v);
    power /= N;
    if (power < 1e-12) return est;
    
    std::vector<Cx> r_norm(N);
    for (size_t i = 0; i < N; ++i) r_norm[i] = r[i] / std::sqrt(power);

    size_t Nfft = 16384;
    while (Nfft > N && Nfft > 1024) Nfft >>= 1;
    if (Nfft < 1024) return est;

    double df = fs / (double)Nfft;
    std::vector<Cx> buf(Nfft);

    for (int p = 1; p <= 8; p *= 2) {
        for (size_t i = 0; i < Nfft; ++i) {
            Cx val(1.0, 0.0);
            for (int j = 0; j < p; ++j) val *= r_norm[i];
            buf[i] = val;
        }

        fftRadix2(buf, false);

        std::vector<double> mag(Nfft, 0.0);
        size_t k_max = static_cast<size_t>(std::min(baudHz, 50000.0) / df);
        
        for (size_t i = 0; i <= k_max && i < Nfft/2; ++i) {
            mag[i] = std::abs(buf[i]);
            if (i > 0) mag[Nfft - i] = std::abs(buf[Nfft - i]);
        }

        std::vector<double> mag_sorted;
        for (size_t i = 0; i <= k_max && i < Nfft/2; ++i) {
            mag_sorted.push_back(mag[i]);
            if (i > 0) mag_sorted.push_back(mag[Nfft - i]);
        }
        if (mag_sorted.empty()) break;
        
        std::nth_element(mag_sorted.begin(), mag_sorted.begin() + mag_sorted.size()/2, mag_sorted.end());
        double median = mag_sorted[mag_sorted.size()/2];

        size_t max_idx = 0;
        double max_val = 0.0;
        for (size_t i = 0; i <= k_max && i < Nfft/2; ++i) {
            if (mag[i] > max_val) { max_val = mag[i]; max_idx = i; }
            if (i > 0 && mag[Nfft - i] > max_val) { max_val = mag[Nfft - i]; max_idx = Nfft - i; }
        }

        int num_bins_half_max = 0;
        for (size_t i = 0; i <= k_max && i < Nfft/2; ++i) {
            if (mag[i] > max_val / 2.0) num_bins_half_max++;
            if (i > 0 && mag[Nfft - i] > max_val / 2.0) num_bins_half_max++;
        }

        double peakiness = (median > 1e-12) ? (max_val / median) : 0.0;
        
        if (num_bins_half_max <= 30 && peakiness > 4.0) {
            est.p_order = p;
            est.confidence = peakiness;
            
            // Parabolic interpolation for sub-bin accuracy
            auto getInterpolatedFreq = [&](size_t idx) {
                size_t idx_p = (idx == 0) ? Nfft - 1 : idx - 1;
                size_t idx_n = (idx == Nfft - 1) ? 0 : idx + 1;
                double y0 = std::log(mag[idx_p] + 1e-12);
                double y1 = std::log(mag[idx] + 1e-12);
                double y2 = std::log(mag[idx_n] + 1e-12);
                double denom = (y0 - 2.0 * y1 + y2);
                double delta = 0.0;
                if (std::abs(denom) > 1e-12) delta = 0.5 * (y0 - y2) / denom;
                double f_bin = (idx + delta) * df;
                if (f_bin > fs / 2.0) f_bin -= fs;
                return f_bin;
            };

            std::vector<std::pair<double, size_t>> local_maxima;
            for (size_t i = 1; i <= k_max && i < Nfft/2 - 2; ++i) {
                if (mag[i] > mag[i-1] && mag[i] >= mag[i+1] && mag[i] > max_val * 0.2) {
                    local_maxima.push_back({mag[i], i});
                }
                if (mag[Nfft - i] > mag[Nfft - i - 1] && mag[Nfft - i] >= mag[Nfft - i + 1] && mag[Nfft - i] > max_val * 0.2) {
                    local_maxima.push_back({mag[Nfft - i], Nfft - i});
                }
            }
            if (mag[0] > mag[1] && mag[0] > max_val * 0.2) {
                local_maxima.push_back({mag[0], 0});
            }
            
            std::sort(local_maxima.rbegin(), local_maxima.rend());
            
            std::vector<std::pair<double, size_t>> filtered_peaks;
            for (auto& pk : local_maxima) {
                int d = std::abs((int)pk.second - (int)max_idx);
                d = std::min(d, (int)Nfft - d);
                if (d > 20) filtered_peaks.push_back(pk);
            }

            est.f1_hz = getInterpolatedFreq(max_idx) / p;
            est.phase1_rad = std::arg(buf[max_idx]) / p;
            
            if (!filtered_peaks.empty()) {
                est.f2_hz = getInterpolatedFreq(filtered_peaks[0].second) / p;
                est.phase2_rad = std::arg(buf[filtered_peaks[0].second]) / p;
            }
            break; 
        }
    }
    return est;
}
}