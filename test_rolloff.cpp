#include "rx_bit_chain.hpp"
#include "rolloff.hpp"
#include "truth.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>

int main(int argc, char** argv) {
    const std::string dir = (argc > 1) ? argv[1] : "out/sc_clean";
    rx::Truth truth;
    try { truth = rx::loadTruth(dir + "/truth.json"); }
    catch (...) { std::cerr << "Cannot load truth.json in " << dir << "\n"; return 1; }

    auto r = rx::readIqFloat32(dir + "/iq.bin");
    
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "True baud  = " << truth.symbolRateHz << " Hz\n";
    std::cout << "True beta  = " << truth.rolloff << "\n\n";

    // Pass the true baud rate for now (we already validated the estimator)
    auto est = rx::estimateRolloff(r, truth.sampleRateHz, truth.symbolRateHz);
    
    std::cout << "Estimated beta = " << est.beta << "\n";
    std::cout << "Center freq    = " << est.centerFreqHz << " Hz\n";
    std::cout << "Confidence     = " << est.confidence << "\n\n";

    if (std::abs(est.beta - truth.rolloff) < 0.05) {
        std::cout << "PASS (beta within 0.05)\n";
    } else {
        std::cout << "FAIL\n";
    }
    return 0;
}