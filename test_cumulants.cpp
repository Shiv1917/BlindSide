#include "rx_bit_chain.hpp"
#include "frontend.hpp"
#include "cumulants.hpp"
#include "truth.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>

static std::vector<rx::Cx> sampleAtEpsilon(const std::vector<rx::Cx>& mf,
                                            double fs, double baudHz, double eps) {
    double sps = fs / baudHz;
    double offset = eps * sps;
    std::vector<rx::Cx> out;
    for (double idx = offset; idx + 1 < (double)mf.size(); idx += sps) {
        size_t i0 = (size_t)idx;
        double frac = idx - i0;
        out.push_back(mf[i0] * (1.0 - frac) + mf[i0 + 1] * frac);
    }
    return out;
}

int main(int argc, char** argv) {
    const std::string dir = (argc > 1) ? argv[1] : "out";
    rx::Truth truth;
    try { truth = rx::loadTruth(dir + "/truth.json"); }
    catch (...) { std::cerr << "Cannot load truth.json in " << dir << "\n"; return 1; }

    auto r = rx::readIqFloat32(dir + "/iq.bin");
    double fs = truth.sampleRateHz;
    double baudHz = truth.symbolRateHz;
    double beta  = truth.rolloff;

    std::cout << "=== Cumulant-based Power Ratio Estimator ===" << std::endl;
    std::cout << "True mode: " << truth.mode << "\n";
    std::cout << "True PD  : " << truth.powerDifferenceDb << " dB\n\n";

    auto y_mf = rx::applyMatchedFilter(r, fs, baudHz, beta);
    double eps1 = truth.streams[0].fractionalDelaySymbols;
    auto samples = sampleAtEpsilon(y_mf, fs, baudHz, eps1);
    std::cout << "Symbols sampled: " << samples.size() << "\n";

    auto est = rx::estimatePowerRatioQPSK(samples);
    std::cout << "m21       = " << est.m21 << "\n";
    std::cout << "m42       = " << est.m42 << "\n";
    std::cout << "C42_raw   = " << est.c42_raw << "\n";
    std::cout << "C42_norm  = " << est.c42_norm << "\n";
    std::cout << "Est r     = " << est.power_ratio << "\n";
    std::cout << "Est PD    = " << est.pd_db << " dB\n";
    std::cout << "SC?       = " << (est.is_sc ? "yes" : "no") << "\n\n";

    bool pass = false;
    if (truth.mode == "SC") {
        pass = est.is_sc;
    } else {
        double true_r = truth.streams[0].power / truth.streams[1].power;
        double ratio_err = std::abs(est.power_ratio - true_r) / true_r;
        std::cout << "True r       = " << true_r << "\n";
        std::cout << "Ratio error  = " << (ratio_err * 100) << " %\n";
        pass = (ratio_err < 0.5) && !est.is_sc;
    }

    std::cout << (pass ? "PASS\n" : "FAIL\n");
    return 0;
}