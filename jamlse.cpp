#include "jamlse.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <iostream>
#include <chrono>
#include <vector>

namespace rx {

static double rcPulse(double t, double beta, double T) {
    double tT = t / T;
    const double eps = 1e-9;
    if (std::abs(tT) < eps) return 1.0;
    double num = std::sin(M_PI * tT) * std::cos(M_PI * beta * tT);
    double den = M_PI * tT * (1.0 - (2.0 * beta * tT) * (2.0 * beta * tT));
    if (std::abs(den) < eps) {
        double x = M_PI / (2.0 * beta);
        return (M_PI / 4.0) * (std::sin(x) / x);
    }
    return num / den;
}

static Cx qpskConst(int k) {
    const double PI = 3.14159265358979323846;
    return std::polar(1.0, PI / 4.0 + (PI / 2.0) * k);
}

static inline int makeState(const int* aA, const int* aB, int Ld) {
    int s = 0;
    for (int i = 0; i < Ld; ++i) s = (s << 2) | aA[i];
    for (int i = 0; i < Ld; ++i) s = (s << 2) | aB[i];
    return s;
}
static inline void decomposeState(int s, int* aA, int* aB, int Ld) {
    for (int i = Ld - 1; i >= 0; --i) { aB[i] = s & 3; s >>= 2; }
    for (int i = Ld - 1; i >= 0; --i) { aA[i] = s & 3; s >>= 2; }
}

JamlseResult runJamlse(const std::vector<Cx>& y_mf,
                       double fs,
                       double best_offset_samples,
                       const JamlseParams& p) {
    JamlseResult res;
    const int sps_native = (int)std::round(fs / p.baudHz);
    const int st = p.sps_trellis;
    const int L  = p.L;
    const int TAPS = 2*L + 1;
    const double T  = 1.0 / p.baudHz;
    const double T_sub = T / st;

    int N_SYM = 0;
    while (best_offset_samples + (N_SYM + 2*L) * sps_native < (double)y_mf.size())
        N_SYM++;
    if (N_SYM < 100) return res;

    const int N_STATE = 1 << (2 * 2 * p.L_dfse);
    const int TOTAL_STEPS = N_SYM + L;

    double frontend_gain = std::sqrt((double)sps_native);

    std::vector<std::vector<double>> tap_A(st, std::vector<double>(TAPS));
    std::vector<std::vector<Cx>>     tap_B(st, std::vector<Cx>(TAPS));
    for (int ph = 0; ph < st; ++ph) {
        for (int m = 0; m <= 2*L; ++m) {
            double tA = m * T + ph * T_sub - p.eps_A * T;
            double tB = m * T + ph * T_sub - p.eps_B * T;
            tap_A[ph][m] = rcPulse(tA, p.beta, T);
            tap_B[ph][m] = rcPulse(tB, p.beta, T)
                         * std::polar(1.0, -2.0 * M_PI * p.delta_f * m * T);
        }
    }

    std::vector<Cx> targetA(st * TAPS);
    std::vector<Cx> targetB(st * TAPS);
    for (int ph = 0; ph < st; ++ph) {
        for (int m = 0; m < TAPS; ++m) {
            targetA[ph * TAPS + m] = std::sqrt(p.P_A) * frontend_gain * tap_A[ph][m];
            targetB[ph * TAPS + m] = (p.P_B > 1e-3) ? std::sqrt(p.P_B) * frontend_gain * tap_B[ph][m] : Cx(0, 0);
        }
    }

    std::vector<double> metric_old(N_STATE, 1e30);
    std::vector<double> metric_new(N_STATE, 1e30);

    std::vector<Cx> fA_old(N_STATE * st * TAPS);
    std::vector<Cx> fB_old(N_STATE * st * TAPS);
    std::vector<Cx> fA_new(N_STATE * st * TAPS);
    std::vector<Cx> fB_new(N_STATE * st * TAPS);

    for (int s = 0; s < N_STATE; ++s) {
        for (int ph = 0; ph < st; ++ph) {
            for (int m = 0; m < TAPS; ++m) {
                fA_old[(s * st + ph) * TAPS + m] = targetA[ph * TAPS + m];
                fB_old[(s * st + ph) * TAPS + m] = targetB[ph * TAPS + m];
            }
        }
    }

    std::fill(metric_old.begin(), metric_old.end(), 0.0);

    auto getSample = [&](int sym, int ph) -> Cx {
        double idx = best_offset_samples + sym * sps_native + (ph * sps_native / st);
        size_t i0 = (size_t)idx;
        if (i0 + 1 >= y_mf.size()) return Cx(0, 0);
        double frac = idx - i0;
        return y_mf[i0] * (1.0 - frac) + y_mf[i0+1] * frac;
    };

    res.symbols_A.resize(N_SYM);
    res.symbols_B.resize(N_SYM);

    std::vector<int16_t> survivor_pred(N_STATE * TOTAL_STEPS, -1);
    std::vector<uint8_t> pathA(N_STATE * TOTAL_STEPS);
    std::vector<uint8_t> pathB(N_STATE * TOTAL_STEPS);

    auto start_time = std::chrono::high_resolution_clock::now();
    auto last_print = start_time;

    const int STARTUP = 3000;

    for (int sym = 0; sym < TOTAL_STEPS; ++sym) {
        std::vector<Cx> y_k(st, Cx(0, 0));
        if (sym >= L) {
            for (int ph = 0; ph < st; ++ph) y_k[ph] = getSample(sym - L, ph);
        }

        std::fill(metric_new.begin(), metric_new.end(), 1e30);

        for (int s = 0; s < N_STATE; ++s) {
            if (metric_old[s] >= 1e29) continue;

            int aA_state[3];
            int aB_state[3];
            decomposeState(s, aA_state, aB_state, p.L_dfse);

            int trace_states[7];
            trace_states[0] = s;
            for (int j = 1; j <= 2*p.L; ++j) {
                if (sym >= j) trace_states[j] = survivor_pred[trace_states[j-1] * TOTAL_STEPS + sym - j];
                else trace_states[j] = -1;
            }

            for (int newA = 0; newA < 4; ++newA) {
                for (int newB = 0; newB < 4; ++newB) {
                    int newA_state[3];
                    int newB_state[3];
                    newA_state[0] = newA;
                    newB_state[0] = newB;
                    for (int i = 1; i < p.L_dfse; ++i) {
                        newA_state[i] = aA_state[i-1];
                        newB_state[i] = aB_state[i-1];
                    }
                    int ns = makeState(newA_state, newB_state, p.L_dfse);

                    int syms_A[7];
                    int syms_B[7];
                    syms_A[0] = newA;
                    syms_B[0] = newB;
                    for (int i = 0; i < p.L_dfse; ++i) {
                        syms_A[i+1] = aA_state[i];
                        syms_B[i+1] = aB_state[i];
                    }
                    for (int j = p.L_dfse + 1; j <= 2*p.L; ++j) {
                        if (trace_states[j] >= 0) {
                            syms_A[j] = pathA[trace_states[j] * TOTAL_STEPS + sym - j];
                            syms_B[j] = pathB[trace_states[j] * TOTAL_STEPS + sym - j];
                        } else {
                            syms_A[j] = 0;
                            syms_B[j] = 0;
                        }
                    }

                    double branch_metric = 0.0;
                    Cx err_A_arr[2] = {Cx(0,0), Cx(0,0)};
                    Cx err_B_arr[2] = {Cx(0,0), Cx(0,0)};

                    if (sym >= L) {
                        for (int ph = 0; ph < st; ++ph) {
                            Cx pred_A(0, 0), pred_B(0, 0);
                            for (int m = 0; m <= 2*L; ++m) {
                                pred_A += qpskConst(syms_A[m]) * fA_old[(s * st + ph) * TAPS + m];
                                pred_B += qpskConst(syms_B[m]) * fB_old[(s * st + ph) * TAPS + m];
                            }

                            double t_abs = T * (best_offset_samples / sps_native + (sym - L) + (double)ph / st);
                            double phase_val = 2.0 * M_PI * p.delta_f * t_abs + p.phase_B;
                            Cx phase_B = std::polar(1.0, phase_val);
                            Cx pred_B_rot = pred_B * phase_B;
                            Cx yhat = pred_A + pred_B_rot;

                            double diffRe = y_k[ph].real() - yhat.real();
                            double diffIm = y_k[ph].imag() - yhat.imag();
                            branch_metric += diffRe*diffRe + diffIm*diffIm;

                            Cx err = y_k[ph] - yhat;
                            err_A_arr[ph] = err;
                            err_B_arr[ph] = err * std::conj(phase_B);
                        }
                    }

                    double new_metric = metric_old[s] + branch_metric;
                    if (new_metric < metric_new[ns]) {
                        metric_new[ns] = new_metric;
                        survivor_pred[ns * TOTAL_STEPS + sym] = s;
                        pathA[ns * TOTAL_STEPS + sym] = (uint8_t)newA;
                        pathB[ns * TOTAL_STEPS + sym] = (uint8_t)newB;

                        for (int ph = 0; ph < st; ++ph) {
                            for (int m = 0; m <= 2*L; ++m) {
                                if (sym < STARTUP || p.lms_mu == 0.0) {
                                    fA_new[(ns * st + ph) * TAPS + m] = fA_old[(s * st + ph) * TAPS + m];
                                    fB_new[(ns * st + ph) * TAPS + m] = fB_old[(s * st + ph) * TAPS + m];
                                } else {
                                    const double leak = 1e-6;
                                    fA_new[(ns * st + ph) * TAPS + m] =
                                        fA_old[(s * st + ph) * TAPS + m]
                                        + Cx(p.lms_mu, 0) * err_A_arr[ph] * std::conj(qpskConst(syms_A[m]))
                                        - Cx(leak, 0) * (fA_old[(s * st + ph) * TAPS + m] - targetA[ph * TAPS + m]);
                                    if (p.P_B > 1e-3) {
                                        fB_new[(ns * st + ph) * TAPS + m] =
                                            fB_old[(s * st + ph) * TAPS + m]
                                            + Cx(p.lms_mu, 0) * err_B_arr[ph] * std::conj(qpskConst(syms_B[m]))
                                            - Cx(leak, 0) * (fB_old[(s * st + ph) * TAPS + m] - targetB[ph * TAPS + m]);
                                    } else {
                                        fB_new[(ns * st + ph) * TAPS + m] = Cx(0, 0);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        std::swap(metric_old, metric_new);
        std::swap(fA_old, fA_new);
        std::swap(fB_old, fB_new);

        auto now = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> since_print = now - last_print;
        if (since_print.count() > 5.0) {
            std::chrono::duration<double> elapsed = now - start_time;
            double percent = 100.0 * sym / TOTAL_STEPS;
            double eta = (elapsed.count() / (percent / 100.0)) - elapsed.count();
            std::cout << "\r  Processing: " << (int)percent << "% (ETA: " << (int)eta << "s)    " << std::flush;
            last_print = now;
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> total_time = end_time - start_time;
    std::cout << "\r  Processing: 100% (Took " << (int)total_time.count() << "s)    \n";

    int best_state = 0;
    for (int s = 1; s < N_STATE; ++s)
        if (metric_old[s] < metric_old[best_state]) best_state = s;

    int curr = best_state;
    for (int i = TOTAL_STEPS - 1; i >= 0; --i) {
        int out_idx = i - L;
        if (out_idx >= 0) {
            res.symbols_A[out_idx] = pathA[curr * TOTAL_STEPS + i];
            res.symbols_B[out_idx] = pathB[curr * TOTAL_STEPS + i];
        }
        curr = survivor_pred[curr * TOTAL_STEPS + i];
    }

    res.metric.push_back(metric_old[best_state]);
    return res;
}
}