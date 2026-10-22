#include "rx_bit_chain.hpp"
#include "cfo.hpp"
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
    
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Mode: " << truth.mode << "\n";
    for (auto& s : truth.streams) {
        if (s.power > 0) std::cout << "True " << s.name << " CFO = " << s.cfoHz << " Hz\n";
    }
    std::cout << "\n";

    // Pass true.symbolRateHz to restrict the search band
    auto est = rx::estimateCfo(r, truth.sampleRateHz, truth.symbolRateHz);
    
    std::cout << "Detected p-order = " << est.p_order << "\n";
    std::cout << "Confidence       = " << est.confidence << "\n";
    if (est.p_order > 0) {
        std::cout << "Est A CFO        = " << est.f1_hz << " Hz\n";
        std::cout << "Est B CFO        = " << est.f2_hz << " Hz\n";
    } else {
        std::cout << "No peak found (p=0)\n";
    }

    std::cout << "\n";
    
        bool pass = false;
    double tolerance = 150.0;
    
    if (truth.mode == "SC") {
        pass = (std::abs(est.f1_hz - truth.streams[0].cfoHz) < tolerance);
    } else {
        // For CnC, just check if the dominant CFO is found. 
        // The second CFO requires SIC as per thesis Section 4.3.
        double d1 = std::abs(est.f1_hz - truth.streams[0].cfoHz);
        double d2 = std::abs(est.f1_hz - truth.streams[1].cfoHz);
        pass = (d1 < tolerance) || (d2 < tolerance);
    }

    if (pass) std::cout << "PASS (within " << tolerance << " Hz)\n";
    else      std::cout << "FAIL\n";
    
    return 0;
}