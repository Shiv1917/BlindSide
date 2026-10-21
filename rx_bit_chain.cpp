#include "rx_bit_chain.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

namespace rx {

// ---------- internal ----------
static uint32_t readBits(const Bits& b, size_t pos, int nbits) {
    uint32_t v = 0;
    for (int i = 0; i < nbits; ++i) v = (v << 1) | (b[pos + i] & 1);
    return v;
}

// Gray: dibit (hi,lo) -> constellation phase index 0..3  (Section 5b)
static int dibitToIndex(int hi, int lo) {
    static const int t[4] = {0, 1, 3, 2};
    return t[(hi << 1) | lo];
}
static void indexToDibit(int idx, int& hi, int& lo) {
    static const int HI[4] = {0, 0, 1, 1};
    static const int LO[4] = {0, 1, 1, 0};
    hi = HI[idx];
    lo = LO[idx];
}

// ---------- demapper (Section 6) ----------
Bits demapQpsk(const std::vector<Cx>& sym) {
    Bits bits(sym.size() * 2);
    for (size_t n = 0; n < sym.size(); ++n) {
        bool rePos = sym[n].real() >= 0;
        bool imPos = sym[n].imag() >= 0;
        int k = imPos ? (rePos ? 0 : 1) : (rePos ? 3 : 2);
        indexToDibit(k, bits[2 * n], bits[2 * n + 1]);
    }
    return bits;
}

// ---------- differential decoder (Section 5b) ----------
Bits diffDecode(const Bits& in) {
    Bits out(in.size(), 0);
    int prev = 0;
    for (size_t n = 0; n + 1 < in.size(); n += 2) {
        int cur  = dibitToIndex(in[n], in[n + 1]);
        int step = (cur - prev) & 3;
        indexToDibit(step, out[n], out[n + 1]);
        prev = cur;
    }
    return out;
}

// ---------- self-sync descrambler (Section 4 / IESS-310) ----------
Bits descramble(const Bits& in, const std::vector<int>& taps) {
    Bits out(in.size());
    for (size_t n = 0; n < in.size(); ++n) {
        int v = in[n];
        for (int lag : taps)
            if (n >= (size_t)lag) v ^= in[n - lag];
        out[n] = v & 1;
    }
    return out;
}

// ---------- CRC-16-CCITT ----------
uint16_t crc16Bits(const Bits& bits) {
    uint16_t crc = 0xFFFF;
    for (int bit : bits) {
        int top = (crc >> 15) & 1;
        crc = static_cast<uint16_t>(crc << 1);
        if (top ^ (bit & 1)) crc ^= 0x1021;
    }
    return crc;
}

// ---------- frame search ----------
std::vector<DecodedFrame> findFrames(const Bits& bits) {
    std::vector<DecodedFrame> found;
    if (bits.size() < (size_t)FRAME_BITS) return found;

    size_t last = bits.size() - FRAME_BITS;
    for (size_t pos = 0; pos <= last; ++pos) {
        if (readBits(bits, pos, SYNC_BITS) != SYNC_WORD) continue;

        DecodedFrame f{};
        f.bitPosition = pos;
        f.sync        = SYNC_WORD;

        Bits body(bits.begin() + pos + SYNC_BITS,
                  bits.begin() + pos + FRAME_BITS - CRC_BITS);

        f.counter = static_cast<uint16_t>(readBits(body, 0, COUNTER_BITS));
        uint32_t len = readBits(body, COUNTER_BITS, LENGTH_BITS);
        f.length = static_cast<uint8_t>(len);
        if (len > (uint32_t)PAYLOAD_BYTES) continue;

        for (uint32_t i = 0; i < len; ++i)
            f.text += static_cast<char>(
                readBits(body, COUNTER_BITS + LENGTH_BITS + 8 * i, 8));

        f.crcReceived = static_cast<uint16_t>(
            readBits(bits, pos + FRAME_BITS - CRC_BITS, CRC_BITS));
        f.crcComputed = crc16Bits(body);
        f.crcOk       = (f.crcReceived == f.crcComputed);

        if (f.crcOk) {
            found.push_back(f);
            pos += FRAME_BITS - 1;      // skip past this frame
        }
    }
    return found;
}

// ---------- full receive paths ----------
std::vector<DecodedFrame> receiveBits(const Bits& scrambledDiffEncoded,
                                      const std::vector<int>& taps) {
    return findFrames(descramble(diffDecode(scrambledDiffEncoded), taps));
}

std::vector<DecodedFrame> receiveSymbols(const std::vector<Cx>& symbols,
                                         const std::vector<int>& taps) {
    return receiveBits(demapQpsk(symbols), taps);
}

// ---------- IO ----------
std::vector<Cx> readIqFloat32(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { std::cerr << "Cannot open " << path << "\n"; return {}; }
    in.seekg(0, std::ios::end);
    size_t bytes = static_cast<size_t>(in.tellg());
    in.seekg(0, std::ios::beg);
    size_t n = bytes / (2 * sizeof(float));
    std::vector<Cx> out(n);
    for (size_t i = 0; i < n; ++i) {
        float iq[2];
        in.read(reinterpret_cast<char*>(iq), sizeof(iq));
        out[i] = Cx(iq[0], iq[1]);
    }
    return out;
}
std::vector<Cx> readSymbolsFloat32(const std::string& p) { return readIqFloat32(p); }

Bits readBitsU8(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { std::cerr << "Cannot open " << path << "\n"; return {}; }
    in.seekg(0, std::ios::end);
    size_t bytes = static_cast<size_t>(in.tellg());
    in.seekg(0, std::ios::beg);
    Bits out(bytes);
    for (size_t i = 0; i < bytes; ++i) {
        char c;
        in.read(&c, 1);
        out[i] = c & 1;
    }
    return out;
}

// ---------- reporting ----------
void printFrames(const std::vector<DecodedFrame>& frames,
                 size_t totalBits, const std::string& name) {
    std::cout << "[" << name << "] recovered " << frames.size() << " frames\n";
    if (frames.empty()) return;

    size_t nonContiguous = 0, emptyText = 0;
    for (size_t i = 1; i < frames.size(); ++i) {
        uint16_t expected = static_cast<uint16_t>(frames[i - 1].counter + 1);
        if (frames[i].counter != expected) ++nonContiguous;
    }
    for (const auto& f : frames) if (f.text.empty()) ++emptyText;

    size_t N = frames.size(), step = std::max<size_t>(1, N / 5);
    std::cout << "  sample frames:\n";
    for (size_t i = 0; i < N; i += step) {
        const auto& f = frames[i];
        std::cout << "    [" << i << "] pos="  << f.bitPosition
                  << " counter=" << f.counter
                  << " len="     << (int)f.length
                  << " text=\""  << f.text << "\"\n";
    }
    std::cout << "  first counter=" << frames.front().counter
              << " last counter="  << frames.back().counter << "\n";
    std::cout << "  first pos="    << frames.front().bitPosition
              << " last pos="      << frames.back().bitPosition
              << " (input bits="   << totalBits << ")\n";
    std::cout << "  counter discontinuities=" << nonContiguous
              << " empty payloads="            << emptyText << "\n";
}

}  // namespace rx