#include "rx_bit_chain.hpp"
#include "frontend.hpp"
#include "cfo.hpp"
#include "frac_delay.hpp"
#include "truth.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>

int main(int argc, char** argv) {
    const std::string dir = (argc > 1) ? argv[1] : "out";
    rx::Truth truth;
    try { truth = rx::loadTruth(dir + "/truth.json"); }
    catch (...) { std::cerr << "Cannot load truth.json in " << dir << "\n"; return 1; }

    auto r = rx::readIqFloat32(dir + "/iq.bin");
    double fs = truth.sampleRateHz;
    double baudHz = truth.symbolRateHz;
    double beta = truth.rolloff;

    std::cout << "=== Fractional Delay Estimator ===" << std::endl;
    std::cout << "True eps: " << truth.streams[0].fractionalDelaySymbols;
    if (truth.mode == "CnC") std::cout << ", " << truth.streams[1].fractionalDelaySymbols;
    std::cout << "\n";

    double true_cfo1 = truth.streams[0].cfoHz;
    double true_phase1 = truth.streams[0].phaseRad;
    double true_cfo2 = (truth.mode == "CnC") ? truth.streams[1].cfoHz : true_cfo1;
    double P1 = truth.streams[0].power;
    double P2 = (truth.mode == "CnC") ? truth.streams[1].power : 0.0;

    // Residual CFO of B after removing A's CFO
    double delta_f = true_cfo2 - true_cfo1;

    auto r_cfo = rx::correctCfo(r, fs, true_cfo1, true_phase1);
    auto y_mf = rx::applyMatchedFilter(r_cfo, fs, baudHz, beta);

    auto est = rx::estimateFractionalDelay(y_mf, fs, baudHz, beta, P1, P2, delta_f);
    
    std::cout << "delta_f  = " << delta_f << " Hz\n";
    std::cout << "Est eps1 = " << est.eps1 << "\n";
    std::cout << "Est eps2 = " << est.eps2 << "\n";
    std::cout << "Error    = " << est.error << "\n\n";

    if (truth.mode == "CnC") {
        bool pass1 = std::abs(est.eps1 - truth.streams[0].fractionalDelaySymbols) < 0.1;
        bool pass2 = std::abs(est.eps2 - truth.streams[1].fractionalDelaySymbols) < 0.1;
        if (pass1 && pass2) std::cout << "PASS\n";
        else std::cout << "FAIL\n";
    } else {
        bool pass1 = std::abs(est.eps1 - truth.streams[0].fractionalDelaySymbols) < 0.1;
        if (pass1) std::cout << "PASS\n";
        else std::cout << "FAIL\n";
    }

    return 0;
}