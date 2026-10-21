#pragma once
#include <cstdint>
#include <complex>
#include <string>
#include <vector>

namespace rx {

using Bits = std::vector<int>;        // one bit per element, 0/1
using Cx   = std::complex<double>;

// ---- framing constants (must match transmitter) ----
constexpr uint32_t SYNC_WORD     = 0x1ACFFC1D;
constexpr int      SYNC_BITS     = 32;
constexpr int      COUNTER_BITS  = 16;
constexpr int      LENGTH_BITS   = 8;
constexpr int      PAYLOAD_BYTES = 32;
constexpr int      CRC_BITS      = 16;
constexpr int      FRAME_BITS    = SYNC_BITS + COUNTER_BITS + LENGTH_BITS
                                   + PAYLOAD_BYTES * 8 + CRC_BITS;

// ---- default IESS-310-like self-sync scrambler taps ----
inline const std::vector<int> DEFAULT_TAPS = {18, 23};

// ---- bit chain building blocks ----
Bits demapQpsk(const std::vector<Cx>& sym);                 // 2 bits per symbol
Bits diffDecode(const Bits& in);                            // inverse of diffEncode
Bits descramble(const Bits& in,
                const std::vector<int>& taps = DEFAULT_TAPS);
uint16_t crc16Bits(const Bits& bits);

// ---- frame recovery ----
struct DecodedFrame {
    size_t      bitPosition;   // start bit of the frame in the input stream
    uint32_t    sync;          // should equal SYNC_WORD
    uint16_t    counter;
    uint8_t     length;
    std::string text;          // first 'length' payload bytes
    uint16_t    crcReceived;
    uint16_t    crcComputed;
    bool        crcOk;
};

// Find all CRC-passing frames in a descrambled bit stream.
std::vector<DecodedFrame> findFrames(const Bits& descrambledBits);

// End-to-end: demap + diffDecode + descramble + findFrames
std::vector<DecodedFrame> receiveSymbols(const std::vector<Cx>& symbols,
                                         const std::vector<int>& taps = DEFAULT_TAPS);

// From an already-differential-encoded, scrambled bit stream
std::vector<DecodedFrame> receiveBits(const Bits& scrambledDiffEncoded,
                                      const std::vector<int>& taps = DEFAULT_TAPS);

// ---- IO ----
std::vector<Cx> readIqFloat32(const std::string& path);      // interleaved float32
std::vector<Cx> readSymbolsFloat32(const std::string& path); // same format
Bits            readBitsU8(const std::string& path);         // 1 byte per bit

// ---- reporting ----
void printFrames(const std::vector<DecodedFrame>& frames,
                 size_t totalBits, const std::string& name);

}  // namespace rx