#include "rx_bit_chain.hpp"
#include <iomanip>
#include <iostream>

using namespace rx;

// ---- TX-side helpers copied only for the test (never use in the receiver) ----
static int dibitToIndex(int hi, int lo) {
    static const int t[4] = {0, 1, 3, 2};
    return t[(hi << 1) | lo];
}
static void indexToDibit(int idx, int& hi, int& lo) {
    static const int HI[4] = {0, 0, 1, 1};
    static const int LO[4] = {0, 1, 1, 0};
    hi = HI[idx]; lo = LO[idx];
}
static Bits diffEncode(const Bits& in) {
    Bits out(in.size(), 0);
    int prev = 0;
    for (size_t n = 0; n + 1 < in.size(); n += 2) {
        int step = dibitToIndex(in[n], in[n + 1]);
        int cur  = (prev + step) & 3;
        indexToDibit(cur, out[n], out[n + 1]);
        prev = cur;
    }
    return out;
}
static Bits scramble(const Bits& in, const std::vector<int>& taps = {18, 23}) {
    Bits out(in.size());
    for (size_t n = 0; n < in.size(); ++n) {
        int v = in[n];
        for (int lag : taps)
            if (n >= (size_t)lag) v ^= out[n - lag];
        out[n] = v & 1;
    }
    return out;
}
static size_t countBitErrors(const Bits& a, const Bits& b) {
    size_t n = std::min(a.size(), b.size());
    size_t e = 0;
    for (size_t i = 0; i < n; ++i) if ((a[i] & 1) != (b[i] & 1)) ++e;
    e += (a.size() > b.size()) ? (a.size() - b.size()) : 0;
    e += (b.size() > a.size()) ? (b.size() - a.size()) : 0;
    return e;
}

int main(int argc, char** argv) {
    std::string dir = (argc > 1) ? argv[1] : "out";

    Bits bitsA          = readBitsU8(dir + "/bits_A.bin");
    std::vector<Cx> symA = readSymbolsFloat32(dir + "/symbols_A.bin");

    std::cout << "Loaded: bitsA=" << bitsA.size()
              << " bits, symA="   << symA.size() << " symbols\n";
    if (bitsA.empty() || symA.empty()) {
        std::cout << "\nMake sure you ran the simulator first so that "
                  << dir << "/bits_A.bin and " << dir << "/symbols_A.bin exist.\n";
        return 1;
    }
    if (symA.size() * 2 != bitsA.size())
        std::cout << "WARNING: symbols*2 != bitsA.size()\n";

    // ---- Test 1: receiveBits on a re-encoded, re-scrambled bitA ----
    // (identical to what demapQpsk would produce from ideal symA)
    Bits txBits = diffEncode(scramble(bitsA));
    auto framesBit = receiveBits(txBits);
    std::cout << "\n--- Test 1: bit chain (known bitA) ---\n";
    printFrames(framesBit, txBits.size(), "receiveBits");

    // ---- Test 2: full symbol chain on symbols_A.bin ----
    auto framesSym = receiveSymbols(symA);
    std::cout << "\n--- Test 2: full symbol chain on symbols_A.bin ---\n";
    printFrames(framesSym, symA.size() * 2, "receiveSymbols");

    // ---- Test 3: bit-error-rate of the chain ----
    Bits recovered = descramble(diffDecode(demapQpsk(symA)));
    size_t ber_cnt = countBitErrors(recovered, bitsA);
    std::cout << "\n--- Test 3: bit chain BER ---\n";
    std::cout << "  recovered bits = " << recovered.size() << "\n";
    std::cout << "  reference bits = " << bitsA.size()  << "\n";
    std::cout << "  bit errors     = " << ber_cnt       << "\n";
    std::cout << "  BER            = "
              << std::scientific << std::setprecision(3)
              << (double)ber_cnt / bitsA.size() << std::dec << "\n";

    // ---- verdict ----
    std::cout << "\n--- Summary ---\n";
    std::cout << "Test 1 (bit chain)   : "
              << (framesBit.size() == 1000 ? "PASS (1000 frames)" : "FAIL") << "\n";
    std::cout << "Test 2 (symbol chain): "
              << (framesSym.size() == 1000 ? "PASS (1000 frames)" : "FAIL") << "\n";
    std::cout << "Test 3 (BER == 0)    : "
              << (ber_cnt == 0 ? "PASS" : "FAIL") << "\n";
    return 0;
}