#include "rx_bit_chain.hpp"
#include "frontend.hpp"
#include "jamlse.hpp"
#include "truth.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <filesystem>

int main() {
    std::cout << "SNR(dB) | Frames A | Frames B | SER A | SER B\n";
    std::cout << "--------------------------------------------------\n";
    
    for (int snr = 8; snr <= 20; snr += 2) {
        std::string dir = "out/cnc_" + std::to_string(snr) + "db";
        rx::Truth truth;
        try { truth = rx::loadTruth(dir + "/truth.json"); }
        catch (...) { continue; }

        auto r = rx::readIqFloat32(dir + "/iq.bin");
        double fs = truth.sampleRateHz;

        rx::JamlseParams p;
        p.baudHz       = truth.symbolRateHz;
        p.beta         = truth.rolloff;
        p.L            = 3;
        p.L_dfse       = 3;
        p.sps_trellis  = 1;
        p.f_cfo_A      = 0.0;
        p.eps_A        = truth.streams[0].fractionalDelaySymbols;
        p.phase_A      = 0.0;
        p.P_A          = truth.streams[0].power;
        p.delta_f      = truth.streams[1].cfoHz - truth.streams[0].cfoHz;
        p.eps_B        = truth.streams[1].fractionalDelaySymbols;
        p.phase_B      = truth.streams[1].phaseRad - truth.streams[0].phaseRad;
        p.P_B          = truth.streams[1].power;
        p.lms_mu       = 0.0;

        auto r_cfo = rx::correctCfo(r, fs, truth.streams[0].cfoHz, truth.streams[0].phaseRad);
        auto y_mf  = rx::applyMatchedFilter(r_cfo, fs, truth.symbolRateHz, truth.rolloff);
        double best_offset = truth.streams[0].fractionalDelaySymbols * fs / truth.symbolRateHz;

        // Redirect std::cerr to suppress debug prints during sweep
        std::streambuf* oldCerr = std::cerr.rdbuf();
        std::cerr.rdbuf(nullptr);
        auto result = rx::runJamlse(y_mf, fs, best_offset, p);
        std::cerr.rdbuf(oldCerr);

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

        auto trueSymbolsToIndices = [](const std::vector<rx::Cx>& syms) {
            std::vector<int> idx(syms.size());
            for (size_t i = 0; i < syms.size(); ++i) {
                int rePos = (syms[i].real() >= 0);
                int imPos = (syms[i].imag() >= 0);
                idx[i] = imPos ? (rePos ? 0 : 1) : (rePos ? 3 : 2);
            }
            return idx;
        };

        auto rawSer = [&](const std::vector<int>& decoded, const std::vector<rx::Cx>& trueSyms) {
            auto trueIdx = trueSymbolsToIndices(trueSyms);
            size_t err = 0;
            size_t n = std::min(decoded.size(), trueIdx.size());
            for (size_t i = 0; i < n; ++i)
                if (decoded[i] != trueIdx[i]) err++;
            return (double)err / n;
        };

        auto framesA = rx::receiveBits(symbolsToBits(result.symbols_A));
        auto framesB = rx::receiveBits(symbolsToBits(result.symbols_B));

        auto trueSymsA = rx::readSymbolsFloat32(dir + "/symbols_A.bin");
        auto trueSymsB = rx::readSymbolsFloat32(dir + "/symbols_B.bin");

        std::cout << std::setw(7) << snr << " | "
                  << std::setw(8) << framesA.size() << " | "
                  << std::setw(8) << framesB.size() << " | "
                  << std::fixed << std::setprecision(4) << rawSer(result.symbols_A, trueSymsA) << " | "
                  << rawSer(result.symbols_B, trueSymsB) << "\n";
    }
    return 0;
}