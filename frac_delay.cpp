#include "frac_delay.hpp"
#include "fft.hpp"
#include <cmath>
#include <algorithm>

namespace rx {

Cx G_eps(double f, double eps_sym, double beta, double T) {
    double eps = eps_sym * T;             // delay in seconds
    double fabs_f = std::abs(f);
    double f_nyq = 1.0 / (2.0 * T);
    Cx val(0.0, 0.0);
    if (fabs_f < (1.0 - beta) * f_nyq) {
        // Passband: pure phase (no aliasing)
        val = std::polar(1.0, -2.0 * M_PI * f * eps);
    } else if (fabs_f <= (1.0 + beta) * f_nyq) {
        // Transition band: Equation (2.23)  -- CORRECT thesis formula
        double theta = M_PI * T / beta * (fabs_f - (1.0 - beta) * f_nyq);
        double a = 0.5 * (1.0 + std::cos(theta));
        double b = 0.5 * (1.0 - std::cos(theta));
        Cx term_a = std::polar(a, -2.0 * M_PI * eps * fabs_f);
        Cx term_b = std::polar(b, -2.0 * M_PI * eps * (fabs_f - 1.0 / T));
        Cx G_pos = term_a + term_b;
        val = (f >= 0) ? G_pos : std::conj(G_pos);
    }
    return val;
}

FracDelayEstimate estimateFractionalDelay(const std::vector<Cx>& y_mf,
                                          double fs, double baudHz, double beta,
                                          double P1, double P2,
                                          double delta_f_hz) {
    FracDelayEstimate best_est;
    double T = 1.0 / baudHz;
    int sps = (int)std::round(fs / baudHz);
    if (sps < 2) return best_est;

    size_t N = y_mf.size();
    size_t num_sym = N / sps;
    if (num_sym < 1024) return best_est;

    int M = 32;
    size_t Nfft = 256;
    int q_max = sps / 2;
    double Ts = T / sps;

    double f_lo = (1.0 - beta) / (2.0 * T);
    double f_hi = 1.0 / (2.0 * T);

    // --- Compute S_meas[q][k] ---
    std::vector<std::vector<Cx>> S_meas(q_max + 1);
    for (int q = 1; q <= q_max; ++q) {
        std::vector<Cx> z0(num_sym), zq(num_sym);
        for (size_t n = 0; n < num_sym; ++n) {
            z0[n] = y_mf[n * sps];
            zq[n] = y_mf[n * sps + q];
        }
        std::vector<Cx> R(2*M + 1, Cx(0, 0));
        for (int m = -M; m <= M; ++m) {
            Cx sum(0, 0);
            for (size_t n = M; n < num_sym - M; ++n)
                sum += z0[n] * std::conj(zq[n - m]);
            R[m + M] = sum / double(num_sym - 2*M);
        }
        std::vector<Cx> buf(Nfft, Cx(0, 0));
        for (int m = -M; m <= M; ++m) {
            double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * (m + M) / (2.0 * M));
            int idx = (m >= 0) ? m : int(Nfft + m);
            buf[idx] = R[m + M] * w;
        }
        fftRadix2(buf, false);
        S_meas[q].resize(Nfft / 2 + 1);
        for (size_t k = 0; k <= Nfft / 2; ++k) S_meas[q][k] = buf[k];
    }

    // --- Precompute transition-band bin list ---
    std::vector<double> freqs;
    std::vector<size_t> kidx;
    for (size_t k = 1; k < Nfft / 2; ++k) {
        double f = k * baudHz / Nfft;
        if (f < f_lo || f > f_hi) continue;
        freqs.push_back(f);
        kidx.push_back(k);
    }
    if (freqs.empty()) return best_est;

    bool fit_eps2 = (P2 > 1e-3 * P1);
    double best_err = 1e30;
    int num_grid = 64;

    for (int i = 0; i < num_grid; ++i) {
        double eps1 = double(i) / num_grid;
        int j_max = fit_eps2 ? num_grid : 1;
        for (int j = 0; j < j_max; ++j) {
            double eps2 = double(j) / num_grid;
            double total_err = 0.0;

            if (!fit_eps2) {
                // SC: per-q magnitude fit against |X1_theory|
                for (int q = 1; q <= q_max; ++q) {
                    double dq = q * 1.0 / sps;
                    double num = 0.0, den = 0.0;
                    for (size_t ki = 0; ki < freqs.size(); ++ki) {
                        Cx G1  = G_eps(freqs[ki], eps1, beta, T);
                        Cx G1d = G_eps(freqs[ki], eps1 - dq, beta, T);
                        double X1 = std::abs(G1 * std::conj(G1d));
                        double Smag = std::abs(S_meas[q][kidx[ki]]);
                        num += Smag * X1;
                        den += X1 * X1;
                    }
                    double scale = (den > 1e-12) ? (num / den) : 0.0;
                    for (size_t ki = 0; ki < freqs.size(); ++ki) {
                        Cx G1  = G_eps(freqs[ki], eps1, beta, T);
                        Cx G1d = G_eps(freqs[ki], eps1 - dq, beta, T);
                        double X1 = std::abs(G1 * std::conj(G1d));
                        double Smag = std::abs(S_meas[q][kidx[ki]]);
                        double diff = Smag - scale * X1;
                        total_err += diff * diff;
                    }
                }
            } else {
                // CnC: magnitude fit against |P1 X1 + P2 X2 e^{-jΔf·q·Ts}|
                for (int q = 1; q <= q_max; ++q) {
                    double dq = q * 1.0 / sps;
                    Cx ramp_q = std::polar(1.0, -2.0 * M_PI * delta_f_hz * q * Ts);
                    double num = 0.0, den = 0.0;
                    for (size_t ki = 0; ki < freqs.size(); ++ki) {
                        double f = freqs[ki];
                        size_t k = kidx[ki];
                        Cx G1  = G_eps(f, eps1, beta, T);
                        Cx G1d = G_eps(f, eps1 - dq, beta, T);
                        Cx X1c = G1 * std::conj(G1d);
                        Cx G2  = G_eps(f - delta_f_hz, eps2, beta, T);
                        Cx G2d = G_eps(f - delta_f_hz, eps2 - dq, beta, T);
                        Cx X2c = G2 * std::conj(G2d) * ramp_q;
                        double Xmag = std::abs(P1 * X1c + P2 * X2c);
                        double Smag = std::abs(S_meas[q][k]);
                        num += Smag * Xmag;
                        den += Xmag * Xmag;
                    }
                    double scale = (den > 1e-12) ? (num / den) : 0.0;
                    for (size_t ki = 0; ki < freqs.size(); ++ki) {
                        double f = freqs[ki];
                        size_t k = kidx[ki];
                        Cx G1  = G_eps(f, eps1, beta, T);
                        Cx G1d = G_eps(f, eps1 - dq, beta, T);
                        Cx X1c = G1 * std::conj(G1d);
                        Cx G2  = G_eps(f - delta_f_hz, eps2, beta, T);
                        Cx G2d = G_eps(f - delta_f_hz, eps2 - dq, beta, T);
                        Cx X2c = G2 * std::conj(G2d) * ramp_q;
                        double Xmag = std::abs(P1 * X1c + P2 * X2c);
                        double Smag = std::abs(S_meas[q][k]);
                        double diff = Smag - scale * Xmag;
                        total_err += diff * diff;
                    }
                }
            }

            if (total_err < best_err) {
                best_err = total_err;
                best_est.eps1 = eps1;
                best_est.eps2 = eps2;
            }
        }
    }

    best_est.error = best_err;
    return best_est;
}
}