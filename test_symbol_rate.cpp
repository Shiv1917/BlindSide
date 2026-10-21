#include "rx_bit_chain.hpp"
#include "symbol_rate.hpp"
#include "truth.hpp"
#include <cmath>
#include <iomanip>
#include <iostream>

int main(int argc, char** argv) {
    const std::string dir = (argc > 1) ? argv[1] : "out";

    rx::Truth truth;
    try { truth = rx::loadTruth(dir + "/truth.json"); }
    catch (std::exception& e) { std::cerr << e.what() << "\n"; return 1; }

    auto r = rx::readIqFloat32(dir + "/iq.bin");
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "Loaded iq.bin: " << r.size() << " samples\n";
    std::cout << "  fs            = " << truth.sampleRateHz << " Hz\n";
    std::cout << "  true baud     = " << truth.symbolRateHz << " Hz\n";
    std::cout << "  true sps      = " << truth.samplesPerSymbol << "\n";
    std::cout << "  mode          = " << truth.mode << "\n";
    std::cout << "  Es/N0         = " << truth.snrDb << " dB\n";
    std::cout << "  power diff    = " << truth.powerDifferenceDb << " dB\n\n";

    // Wide search band. In a real receiver this would come from the standard
    // or a coarse scan, not from truth.
    const double minBaud = truth.symbolRateHz / 10.0;
    const double maxBaud = truth.symbolRateHz *  2.0;

    auto est = rx::estimateSymbolRate(r, truth.sampleRateHz, minBaud, maxBaud);
    if (est.baudHz <= 0.0) { std::cout << "FAIL: no estimate\n"; return 1; }

    const double relErr = (est.baudHz - truth.symbolRateHz) / truth.symbolRateHz;

    std::cout << "Estimate:\n";
    std::cout << "  baud          = " << est.baudHz << " Hz\n";
    std::cout << "  sps           = " << est.sps   << "\n";
    std::cout << "  confidence    = " << est.confidence << "\n";
    std::cout << "  relative err  = " << (relErr * 100.0) << " %\n\n";

    if      (std::fabs(relErr) < 1e-3) std::cout << "PASS (within 0.1%)\n";
    else if (std::fabs(relErr) < 1e-2) std::cout << "MARGINAL (within 1%)\n";
    else                               std::cout << "FAIL\n";
    return 0;
}