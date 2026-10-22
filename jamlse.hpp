#pragma once
#include <complex>
#include <vector>

namespace rx {
using Cx = std::complex<double>;

struct JamlseParams {
    double baudHz;
    double beta;
    int    L;              // half-span, e.g. 2 (memory 4 symbols)
    int    L_dfse;         // 2 (reduced state memory)
    int    sps_trellis;    // 2

    // Carrier A (frontend-aligned):
    double f_cfo_A = 0.0;  // residual CFO on A after frontend
    double eps_A   = 0.0;
    double phase_A = 0.0;
    double P_A     = 1.0;

    // Carrier B (residual CFO relative to A):
    double delta_f = 0.0;  // f_B - f_A
    double eps_B   = 0.0;
    double phase_B = 0.0;
    double P_B     = 0.0;

    // PSP
    double lms_mu = 0.01;  // learning rate
};

struct JamlseResult {
    std::vector<int> symbols_A;   // 0..3 (dibit index for each symbol)
    std::vector<int> symbols_B;
    std::vector<double> metric;   // Viterbi metrics over time
};

// Input: matched-filtered signal at native sps (must be multiple of sps_trellis)
//        and the phase offset that best aligns to A (from frontend searchBestTiming).
JamlseResult runJamlse(const std::vector<Cx>& y_mf,
                       double fs,
                       double best_offset_samples,
                       const JamlseParams& p);
}