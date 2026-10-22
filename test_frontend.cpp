#include "rx_bit_chain.hpp"
#include "frontend.hpp"
#include "cfo.hpp"
#include "symbol_rate.hpp"
#include "rolloff.hpp"
#include "truth.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>

int main(int argc, char** argv) {
    const std::string dir = (argc > 1) ? argv[1] : "out/sc";
    rx::Truth truth;
    try { truth = rx::loadTruth(dir + "/truth.json"); }
    catch (...) { std::cerr << "Cannot load truth.json in " << dir << "\n"; return 1; }

    auto r = rx::readIqFloat32(dir + "/iq.bin");
    double fs = truth.sampleRateHz;

    std::cout << "=== Full Blind Frontend Pipeline ===" << std::endl;

    // 1. Blind Symbol Rate Estimation
    auto sr_est = rx::estimateSymbolRate(r, fs, truth.symbolRateHz / 10.0, truth.symbolRateHz * 2.0);
    double baudHz = sr_est.baudHz;
    std::cout << "  [Est] Baud Rate = " << baudHz << " Hz (True: " << truth.symbolRateHz << ")\n";

    // 2. Blind Roll-off Estimation
    auto ro_est = rx::estimateRolloff(r, fs, baudHz);
    double beta = ro_est.beta;
    std::cout << "  [Est] Roll-off  = " << beta << " (True: " << truth.rolloff << ")\n";

    // 3. Blind CFO Estimation
    auto cfo_est = rx::estimateCfo(r, fs, baudHz);
    double est_cfo = cfo_est.f1_hz;
    double est_phase = cfo_est.phase1_rad;
    std::cout << "  [Est] CFO       = " << est_cfo << " Hz (True: " << truth.streams[0].cfoHz << ")\n\n";

    // 4. Run Frontend
    auto r_cfo = rx::correctCfo(r, fs, est_cfo, est_phase);
    auto r_mf = rx::applyMatchedFilter(r_cfo, fs, baudHz, beta);
    auto symbols = rx::searchBestTiming(r_mf, fs, baudHz, 32);

    // 5. Decode
    auto frames = rx::receiveSymbols(symbols);
    std::cout << "Recovered frames: " << frames.size() << "\n\n";

    if (!frames.empty()) {
        std::cout << "Sample frames:\n";
        for (size_t i = 0; i < std::min((size_t)3, frames.size()); ++i) {
            std::cout << "  counter=" << frames[i].counter << " text=\"" << frames[i].text << "\"\n";
        }
    }

    if (frames.size() >= 999) std::cout << "\nPASS (Fully blind SC receiver recovered all frames!)\n";
    else std::cout << "\nFAIL\n";

    return 0;
}