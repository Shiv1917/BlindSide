#include "rx_bit_chain.hpp"
#include "frontend.hpp"
#include "jamlse.hpp"
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

    std::cout << "=== JAMLSE with PSP (L=3, " << truth.mode << ", " << truth.snrDb << " dB SNR) ===\n";

    rx::JamlseParams p;
    p.baudHz       = truth.symbolRateHz;
    p.beta         = truth.rolloff;
    p.L            = 3;
    p.L_dfse       = 3;
    p.sps_trellis  = 1;

    p.f_cfo_A   = 0.0;
    p.eps_A     = truth.streams[0].fractionalDelaySymbols;
    p.phase_A   = 0.0;
    p.P_A       = truth.streams[0].power;

    p.delta_f   = (truth.mode == "CnC") ? (truth.streams[1].cfoHz - truth.streams[0].cfoHz) : 0.0;
    p.eps_B     = (truth.mode == "CnC") ? truth.streams[1].fractionalDelaySymbols : 0.0;
    p.phase_B   = (truth.mode == "CnC") ? (truth.streams[1].phaseRad - truth.streams[0].phaseRad) : 0.0;
    p.P_B       = (truth.mode == "CnC") ? truth.streams[1].power : 0.0;
    p.lms_mu    = 0.0;

    auto r_cfo = rx::correctCfo(r, fs, truth.streams[0].cfoHz, truth.streams[0].phaseRad);
    auto y_mf  = rx::applyMatchedFilter(r_cfo, fs, truth.symbolRateHz, truth.rolloff);
    double best_offset = truth.streams[0].fractionalDelaySymbols * fs / truth.symbolRateHz;

    auto result = rx::runJamlse(y_mf, fs, best_offset, p);

    auto symbolsToBits = [](const std::vector<int>& syms) {
        rx::Bits bits(syms.size() * 2);
        for (size_t i = 0; i < syms.size(); ++i) {
            static const int HI[4] = {0, 0, 1, 1};
            static const int LO[4] = {0, 1, 1, 0};
            bits[2*i]   = HI[syms[i]];
            bits[2*i+1] = LO[syms[i]];
        }
        return bits;
    };

    std::cout << "\n--- Stream A (true P = " << p.P_A << ") ---\n";
    auto framesA = rx::receiveBits(symbolsToBits(result.symbols_A));
    std::cout << "  Frames  : " << framesA.size() << " / 1000\n";
    if (!framesA.empty()) {
        std::cout << "  Sample  : counter=" << framesA[0].counter
                  << " text=\"" << framesA[0].text << "\"\n";
    }

    if (truth.mode == "CnC" && p.P_B > 1e-3) {
        std::cout << "\n--- Stream B (true P = " << p.P_B << ") ---\n";
        auto framesB = rx::receiveBits(symbolsToBits(result.symbols_B));
        std::cout << "  Frames  : " << framesB.size() << " / 1000\n";
        if (!framesB.empty()) {
            std::cout << "  Sample  : counter=" << framesB[0].counter
                      << " text=\"" << framesB[0].text << "\"\n";
        }
    }
    return 0;
}