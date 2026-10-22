#pragma once
#include <complex>
#include <vector>
#include "rx_bit_chain.hpp"

namespace rx {
using Cx = std::complex<double>;

// 1. Rotate the signal to remove the CFO AND initial phase
std::vector<Cx> correctCfo(const std::vector<Cx>& r, double fs, double cfoHz, double phaseRad);

// 2. Apply the RRC Matched Filter
std::vector<Cx> applyMatchedFilter(const std::vector<Cx>& r, double fs, double baudHz, double beta);

// 3. Brute-force timing search (returns the best sampled symbols)
std::vector<Cx> searchBestTiming(const std::vector<Cx>& mf_out, double fs, double baudHz, int oversample = 32);
}