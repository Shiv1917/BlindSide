#include <cstdint>
#include <iostream>
#include <iomanip>
#include <random>
#include <string>
#include <vector>
#include <cmath>
#include <complex>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

using Bits = std::vector<int>;


// ---------- Section 3a: constants ----------
const uint32_t SYNC_WORD     = 0x1ACFFC1D;
const int      SYNC_BITS     = 32;
const int      COUNTER_BITS  = 16;
const int      LENGTH_BITS   = 8;
const int      PAYLOAD_BYTES = 32;
const int      CRC_BITS      = 16;
const int      FRAME_BITS    = SYNC_BITS + COUNTER_BITS + LENGTH_BITS
                               + PAYLOAD_BYTES * 8 + CRC_BITS;   // 328

// ---------- from earlier sections ----------
void pushBits(Bits& b, uint32_t value, int nbits) {
    for (int i = nbits - 1; i >= 0; --i) b.push_back((value >> i) & 1u);
}

uint16_t crc16Bits(const Bits& bits) {
    uint16_t crc = 0xFFFF;
    for (int bit : bits) {
        int top = (crc >> 15) & 1;
        crc = static_cast<uint16_t>(crc << 1);
        if (top ^ bit) crc ^= 0x1021;
    }
    return crc;
}

// ---------- Section 3b: payload field ----------
// Message characters first, then random bytes up to PAYLOAD_BYTES.
void pushPayload(Bits& b, const std::string& msg, std::mt19937_64& rng) {
    for (int i = 0; i < PAYLOAD_BYTES; ++i) {
        if (i < (int)msg.size()) pushBits(b, static_cast<uint8_t>(msg[i]), 8);
        else                     pushBits(b, rng() & 0xFF, 8);
    }
}

// ---------- Section 3c: body = counter + length + payload ----------
Bits makeBody(const std::string& msg, uint16_t counter, std::mt19937_64& rng) {
    Bits body;
    pushBits(body, counter, COUNTER_BITS);
    pushBits(body, static_cast<uint32_t>(msg.size()), LENGTH_BITS);
    pushPayload(body, msg, rng);
    return body;
}

// ---------- Section 3d: full frame ----------
Bits makeFrame(const std::string& msg, uint16_t counter, std::mt19937_64& rng) {
    Bits frame;
    pushBits(frame, SYNC_WORD, SYNC_BITS);                 // 1. sync
    Bits body = makeBody(msg, counter, rng);               // 2. body
    frame.insert(frame.end(), body.begin(), body.end());
    pushBits(frame, crc16Bits(body), CRC_BITS);            // 3. CRC over body only
    return frame;
}

// ---------- Section 3e: test ----------
uint32_t readBits(const Bits& b, size_t pos, int nbits) {
    uint32_t v = 0;
    for (int i = 0; i < nbits; ++i) v = (v << 1) | b[pos + i];
    return v;
}

// Print each field of one frame.
void describeFrame(const std::string& name, const Bits& f) {
    size_t pos = 0;
    uint32_t sync = readBits(f, pos, SYNC_BITS);     pos += SYNC_BITS;
    uint32_t cnt  = readBits(f, pos, COUNTER_BITS);  pos += COUNTER_BITS;
    uint32_t len  = readBits(f, pos, LENGTH_BITS);   pos += LENGTH_BITS;

    std::string text;                                // first 'len' payload bytes
    for (int i = 0; i < PAYLOAD_BYTES; ++i) {
        char c = static_cast<char>(readBits(f, pos + i * 8, 8));
        if (i < (int)len) text += c;
    }
    pos += PAYLOAD_BYTES * 8;
    uint16_t rxCrc = static_cast<uint16_t>(readBits(f, pos, CRC_BITS));

    Bits body(f.begin() + SYNC_BITS, f.end() - CRC_BITS);
    bool ok = (crc16Bits(body) == rxCrc);

    std::cout << name << ": bits=" << f.size()
              << " sync=0x" << std::hex << std::uppercase << sync << std::dec
              << " counter=" << cnt << " length=" << len
              << " text=\"" << text << "\""
              << " crc=0x" << std::hex << std::uppercase << rxCrc << std::dec
              << " check=" << (ok ? "PASS" : "FAIL") << "\n";
}

Bits makeStream(const std::string& msg, int nFrames, uint64_t seed,
                uint16_t startCounter, int offsetBits, size_t totalBits) {
    std::mt19937_64 rng(seed);
    Bits stream;
    stream.reserve(totalBits);

    for (int i = 0; i < offsetBits; ++i) stream.push_back(rng() & 1);      // leading filler

    for (int k = 0; k < nFrames; ++k) {
        uint16_t counter = static_cast<uint16_t>(startCounter + k);        // wraps at 65536
        Bits f = makeFrame(msg, counter, rng);
        stream.insert(stream.end(), f.begin(), f.end());
    }

    while (stream.size() < totalBits) stream.push_back(rng() & 1);         // trailing filler
    return stream;
}


double onesFraction(const Bits& b) {
    size_t ones = 0;
    for (int x : b) ones += x;
    return static_cast<double>(ones) / b.size();
}

struct FoundFrame {
    size_t   position;     // bit index where the sync word starts
    uint16_t counter;
    std::string text;
};

std::vector<FoundFrame> findFrames(const Bits& bits) {
    std::vector<FoundFrame> found;
    size_t last = bits.size() - FRAME_BITS;
    for (size_t pos = 0; pos <= last; ++pos) {
        if (readBits(bits, pos, SYNC_BITS) != SYNC_WORD) continue;         // 1. sync match?

        Bits body(bits.begin() + pos + SYNC_BITS,
                  bits.begin() + pos + FRAME_BITS - CRC_BITS);             // 2. body
        uint16_t rxCrc = static_cast<uint16_t>(
            readBits(bits, pos + FRAME_BITS - CRC_BITS, CRC_BITS));
        if (crc16Bits(body) != rxCrc) continue;                            // 3. CRC ok?

        FoundFrame f;                                                      // 4. unpack
        f.position = pos;
        f.counter  = static_cast<uint16_t>(readBits(body, 0, COUNTER_BITS));
        uint32_t len = readBits(body, COUNTER_BITS, LENGTH_BITS);
        if (len > (uint32_t)PAYLOAD_BYTES) continue;
        for (uint32_t i = 0; i < len; ++i)
            f.text += static_cast<char>(readBits(body, COUNTER_BITS + LENGTH_BITS + 8 * i, 8));
        found.push_back(f);
        pos += FRAME_BITS - 1;                                             // skip past this frame
    }
    return found;
}

const std::vector<int> SCR_TAPS = {18, 23};

// scrambler:   y[n] = x[n] ^ y[n-a] ^ y[n-b]   (feedback uses OUTPUT bits)
Bits scramble(const Bits& in, const std::vector<int>& taps = SCR_TAPS) {
    Bits out(in.size());
    for (size_t n = 0; n < in.size(); ++n) {
        int v = in[n];
        for (int lag : taps)
            if (n >= (size_t)lag) v ^= out[n - lag];       // bits before the start count as 0
        out[n] = v;
    }
    return out;
}

// descrambler: x[n] = y[n] ^ y[n-a] ^ y[n-b]   (uses only RECEIVED bits)
Bits descramble(const Bits& in, const std::vector<int>& taps = SCR_TAPS) {
    Bits out(in.size());
    for (size_t n = 0; n < in.size(); ++n) {
        int v = in[n];
        for (int lag : taps)
            if (n >= (size_t)lag) v ^= in[n - lag];
        out[n] = v;
    }
    return out;
}

// ---------- Section 5b: differential QPSK encoder / decoder ----------
// Dibit (b1,b0) -> Gray index 0..3 (00->0, 01->1, 11->2, 10->3).
// Index k means constellation angle 45 + 90*k degrees (the mapper in Section 6 will use this).
int dibitToIndex(int hi, int lo) {
    static const int table[4] = {0, 1, 3, 2};          // table[(hi<<1)|lo]
    return table[(hi << 1) | lo];
}
void indexToDibit(int idx, int& hi, int& lo) {
    static const int HI[4] = {0, 0, 1, 1};             // inverse of the table above
    static const int LO[4] = {0, 1, 1, 0};
    hi = HI[idx];
    lo = LO[idx];
}

// Encoder: the data dibit is a phase STEP; the transmitted index is the running sum mod 4.
Bits diffEncode(const Bits& in) {                      // in.size() must be even
    Bits out(in.size());
    int prev = 0;                                      // starting phase index
    for (size_t n = 0; n + 1 < in.size(); n += 2) {
        int step = dibitToIndex(in[n], in[n + 1]);
        int cur  = (prev + step) & 3;
        indexToDibit(cur, out[n], out[n + 1]);
        prev = cur;
    }
    return out;
}

// Decoder: the data dibit is the DIFFERENCE between this index and the previous one.
Bits diffDecode(const Bits& in) {
    Bits out(in.size());
    int prev = 0;
    for (size_t n = 0; n + 1 < in.size(); n += 2) {
        int cur  = dibitToIndex(in[n], in[n + 1]);
        int step = (cur - prev) & 3;
        indexToDibit(step, out[n], out[n + 1]);
        prev = cur;
    }
    return out;
}

// Test helper: what a 90*k degree constellation rotation does to the received dibits.
Bits rotateDibits(const Bits& in, int k) {
    Bits out(in.size());
    for (size_t n = 0; n + 1 < in.size(); n += 2) {
        int idx = (dibitToIndex(in[n], in[n + 1]) + k) & 3;
        indexToDibit(idx, out[n], out[n + 1]);
    }
    return out;
}

// ---------- Section 6: QPSK symbol mapper / demapper ----------
using Cx = std::complex<double>;
const double PI = 3.14159265358979323846;

// Dibit -> Gray index k -> point exp(j*(45 + 90*k) degrees). Every point has |s| = 1.
std::vector<Cx> mapQpsk(const Bits& bits) {                 // bits.size() must be even
    std::vector<Cx> sym(bits.size() / 2);
    for (size_t n = 0; n < sym.size(); ++n) {
        int k = dibitToIndex(bits[2 * n], bits[2 * n + 1]);
        sym[n] = std::polar(1.0, PI / 4 + (PI / 2) * k);
    }
    return sym;
}

// Hard decision by quadrant: index 0 = (+,+), 1 = (-,+), 2 = (-,-), 3 = (+,-).
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

double rrcValue(double t, double beta) {
    const double eps = 1e-9;
    if (std::fabs(t) < eps)                                        // t = 0
        return 1.0 - beta + 4.0 * beta / PI;
    if (std::fabs(std::fabs(4.0 * beta * t) - 1.0) < eps)          // t = +-1/(4 beta)
        return beta / std::sqrt(2.0) *
               ((1.0 + 2.0 / PI) * std::sin(PI / (4.0 * beta)) +
                (1.0 - 2.0 / PI) * std::cos(PI / (4.0 * beta)));
    double num = std::sin(PI * t * (1.0 - beta)) + 4.0 * beta * t * std::cos(PI * t * (1.0 + beta));
    double den = PI * t * (1.0 - (4.0 * beta * t) * (4.0 * beta * t));
    return num / den;
}

// ---------- Section 7b: filter taps, with the fractional delay built in ----------
// Taps cover +-span symbols around the centre tap (index span*sps). eps is in symbols.
// h[j] = h( (j - centre)/sps - eps ): shifting the pulse by eps*T is what h(t - kT - eps) means.
// Scaled so that sum(h^2) = sps for eps = 0, which makes the shaped signal power equal to 1.
std::vector<double> makeRrc(int sps, double beta, double eps, int span) {
    int L = 2 * span * sps + 1;
    int c = span * sps;
    std::vector<double> h0(L), h(L);
    double energy = 0;
    for (int j = 0; j < L; ++j) {
        h0[j] = rrcValue(static_cast<double>(j - c) / sps, beta);
        energy += h0[j] * h0[j];
    }
    double scale = std::sqrt(sps / energy);                       // same scale for every eps
    for (int j = 0; j < L; ++j)
        h[j] = scale * rrcValue(static_cast<double>(j - c) / sps - eps, beta);
    return h;
}

std::vector<Cx> shapePulses(const std::vector<Cx>& sym, int sps, double beta, double eps, int span) {
    std::vector<double> h = makeRrc(sps, beta, eps, span);
    const long L = static_cast<long>(h.size());
    const long N = static_cast<long>(sym.size()) * sps;
    std::vector<Cx> out(N, Cx(0, 0));
    for (size_t k = 0; k < sym.size(); ++k) {
        long start = static_cast<long>(k) * sps - span * sps;     // sample where tap 0 lands
        for (long j = 0; j < L; ++j) {
            long n = start + j;
            if (n >= 0 && n < N) out[n] += sym[k] * h[j];
        }
    }
    return out;
}

std::vector<Cx> matchedFilter(const std::vector<Cx>& x, int sps, double beta, int span) {
    std::vector<double> h = makeRrc(sps, beta, 0.0, span);        // symmetric, so no flip needed
    const long L = static_cast<long>(h.size());
    const long N = static_cast<long>(x.size());
    const long c = span * sps;
    std::vector<Cx> y(N, Cx(0, 0));
    for (long m = 0; m < N; ++m) {
        Cx acc(0, 0);
        for (long j = 0; j < L; ++j) {
            long n = m + j - c;
            if (n >= 0 && n < N) acc += x[n] * h[j];
        }
        y[m] = acc / static_cast<double>(sps);                    // gain of the MF is sps
    }
    return y;
}

double Qfunc(double x) { return 0.5 * std::erfc(x / std::sqrt(2.0)); }

struct SimConfig {
    int         sps        = 8;
    double      beta       = 0.35;
    int         span       = 10;
    double      symbolRate = 1.0e6;          // Hz, only used to label the output
    std::string mode       = "CnC";          // "CnC" = two carriers, "SC" = carrier 1 only
    double      pdDb       = 0.0;            // power difference 10*log10(P1/P2), >= 0
    double      snrDb      = 20.0;           // Es/N0 of the total signal (= 2Eb/N0 for QPSK)
    double      f[2]       = {0.0005, -0.0010};   // carrier offsets in cycles per sample (f_i / fs)
    double      theta[2]   = {0.7, -1.2};         // carrier phases, radians
    double      eps[2]     = {0.0, 0.25};         // fractional delays, in symbols
    uint64_t    noiseSeed  = 777;
};

struct SimResult {
    std::vector<Cx> rx;       // what the receiver gets (signal + noise)
    std::vector<Cx> clean;    // signal without noise (for testing only)
    double P[2];              // true carrier powers, P1 + P2 = 1
    double noiseVar;          // complex noise variance per sample
};

std::vector<Cx> makeCarrier(const std::vector<Cx>& sym, const SimConfig& c, int i, double P) {
    std::vector<Cx> x = shapePulses(sym, c.sps, c.beta, c.eps[i], c.span);
    const double amp = std::sqrt(P);
    for (size_t n = 0; n < x.size(); ++n)
        x[n] *= amp * std::polar(1.0, 2.0 * PI * c.f[i] * static_cast<double>(n) + c.theta[i]);
    return x;
}

SimResult simulate(const std::vector<Cx>& symA, const std::vector<Cx>& symB, const SimConfig& c) {
    SimResult r;
    if (c.mode == "SC") { r.P[0] = 1.0; r.P[1] = 0.0; }
    else {
        double ratio = std::pow(10.0, c.pdDb / 10.0);              // P1/P2
        r.P[0] = ratio / (1.0 + ratio);
        r.P[1] = 1.0 / (1.0 + ratio);
    }
    r.clean = makeCarrier(symA, c, 0, r.P[0]);
    if (r.P[1] > 0) {
        std::vector<Cx> b = makeCarrier(symB, c, 1, r.P[1]);
        for (size_t n = 0; n < r.clean.size(); ++n) r.clean[n] += b[n];
    }

    // Es/N0 = (P_total * T) / N0, N0 = noiseVar * Ts  ->  noiseVar = sps / SNR
    double snr = std::pow(10.0, c.snrDb / 10.0);
    r.noiseVar = c.sps / snr;
    std::mt19937_64 rng(c.noiseSeed);
    std::normal_distribution<double> g(0.0, std::sqrt(r.noiseVar / 2.0));   // per I or Q
    r.rx = r.clean;
    for (Cx& v : r.rx) v += Cx(g(rng), g(rng));
    return r;
}

void writeIq(const std::string& path, const std::vector<Cx>& x) {      // float32, I Q I Q ...
    std::ofstream f(path, std::ios::binary);
    for (const Cx& v : x) {
        float iq[2] = {static_cast<float>(v.real()), static_cast<float>(v.imag())};
        f.write(reinterpret_cast<const char*>(iq), sizeof(iq));
    }
}
void writeBitsU8(const std::string& path, const Bits& b) {              // one byte per bit
    std::ofstream f(path, std::ios::binary);
    for (int v : b) { char c = static_cast<char>(v); f.write(&c, 1); }
}

void writeTruthJson(const std::string& path, const SimConfig& c, const SimResult& r,
                    const std::string& msgA, const std::string& msgB, size_t nSym, size_t nSamples) {
    std::ostringstream j;
    j << std::setprecision(10);
    j << "{\n";
    j << "  \"modulation\": \"QPSK\",\n";
    j << "  \"mode\": \"" << c.mode << "\",\n";
    j << "  \"sampleFormat\": \"interleaved float32 I/Q, little endian\",\n";
    j << "  \"samplesPerSymbol\": " << c.sps << ",\n";
    j << "  \"symbolRateHz\": " << c.symbolRate << ",\n";
    j << "  \"sampleRateHz\": " << c.symbolRate * c.sps << ",\n";
    j << "  \"rolloff\": " << c.beta << ",\n";
    j << "  \"filterSpanSymbols\": " << c.span << ",\n";
    j << "  \"numSymbolsPerStream\": " << nSym << ",\n";
    j << "  \"numSamples\": " << nSamples << ",\n";
    j << "  \"snrDb\": " << c.snrDb << ",\n";
    j << "  \"snrDefinition\": \"Es/N0 of the total signal (P1+P2=1) = 2Eb/N0 for QPSK\",\n";
    j << "  \"noiseVariancePerSample\": " << r.noiseVar << ",\n";
    j << "  \"powerDifferenceDb\": " << c.pdDb << ",\n";
    j << "  \"scramblerTaps\": [" << SCR_TAPS[0] << ", " << SCR_TAPS[1] << "],\n";
    j << "  \"bitChain\": \"frames -> self-sync scrambler -> differential QPSK encoder -> Gray mapper\",\n";
    j << "  \"frameBits\": " << FRAME_BITS << ",\n";
    j << "  \"streams\": [\n";
    for (int i = 0; i < 2; ++i) {
        j << "    {\n";
        j << "      \"name\": \"" << (i == 0 ? "A" : "B") << "\",\n";
        j << "      \"message\": \"" << (i == 0 ? msgA : msgB) << "\",\n";
        j << "      \"power\": " << r.P[i] << ",\n";
        j << "      \"cfoCyclesPerSample\": " << c.f[i] << ",\n";
        j << "      \"cfoHz\": " << c.f[i] * c.symbolRate * c.sps << ",\n";
        j << "      \"phaseRad\": " << c.theta[i] << ",\n";
        j << "      \"fractionalDelaySymbols\": " << c.eps[i] << ",\n";
        j << "      \"symbolsFile\": \"symbols_" << (i == 0 ? "A" : "B") << ".bin\",\n";
        j << "      \"infoBitsFile\": \"bits_" << (i == 0 ? "A" : "B") << ".bin\"\n";
        j << "    }" << (i == 0 ? "," : "") << "\n";
    }
    j << "  ]\n}\n";
    std::ofstream(path) << j.str();
}

// Undo carrier i (with the given f and theta), matched filter, sample at the symbol instants.
std::vector<Cx> recoverSymbols(const std::vector<Cx>& r, const SimConfig& c, int i,
                               double fUsed, double thetaUsed) {
    std::vector<Cx> d(r.size());
    for (size_t n = 0; n < r.size(); ++n)
        d[n] = r[n] * std::polar(1.0, -(2.0 * PI * fUsed * static_cast<double>(n) + thetaUsed));
    std::vector<Cx> y = matchedFilter(d, c.sps, c.beta, c.span);
    int off = static_cast<int>(std::lround(c.eps[i] * c.sps));
    std::vector<Cx> out(r.size() / c.sps);
    for (size_t k = 0; k < out.size(); ++k) out[k] = y[k * c.sps + off];
    return out;
}
size_t framesFrom(const std::vector<Cx>& rxSym) {
    return findFrames(descramble(diffDecode(demapQpsk(rxSym)))).size();
}
double symbolErrorRate(const std::vector<Cx>& rxSym, const std::vector<Cx>& txSym) {
    Bits a = demapQpsk(rxSym), b = demapQpsk(txSym);
    size_t err = 0, cnt = 0;
    for (size_t k = 20; k + 20 < txSym.size(); ++k, ++cnt)
        err += (a[2 * k] != b[2 * k] || a[2 * k + 1] != b[2 * k + 1]);
    return static_cast<double>(err) / cnt;
}

int main() {
    std::string msgA = "My name is Shivansh";
    std::string msgB = "My name is Shipra";
    const int    nFrames   = 1000;
    const int    offsetA   = 40;
    const int    offsetB   = 109;
    const size_t totalBits = static_cast<size_t>(nFrames) * FRAME_BITS + offsetB + 41;   // even

    // Bits and symbols (Sections 3-6)
    Bits bitsA = makeStream(msgA, nFrames, 12345,     0, offsetA, totalBits);
    Bits bitsB = makeStream(msgB, nFrames, 67890, 30000, offsetB, totalBits);
    std::vector<Cx> symA = mapQpsk(diffEncode(scramble(bitsA)));
    std::vector<Cx> symB = mapQpsk(diffEncode(scramble(bitsB)));
    std::cout << std::fixed << std::setprecision(4);

    // ---- Test 1: powers and noise level ----
    SimConfig c1;
    c1.pdDb = 4.0; c1.snrDb = 12.0;
    SimResult r1 = simulate(symA, symB, c1);
    double pClean = 0, pNoise = 0;
    for (size_t n = 0; n < r1.rx.size(); ++n) {
        pClean += std::norm(r1.clean[n]);
        pNoise += std::norm(r1.rx[n] - r1.clean[n]);
    }
    pClean /= r1.rx.size(); pNoise /= r1.rx.size();
    std::cout << "PD=4 dB: P1=" << r1.P[0] << " P2=" << r1.P[1] << " (ratio "
              << 10 * std::log10(r1.P[0] / r1.P[1]) << " dB)\n";
    std::cout << "Mean power of clean sum   : " << pClean << " (want 1)\n";
    std::cout << "Noise power per sample    : " << pNoise << " (want " << r1.noiseVar << ")\n";
    std::cout << "Es/N0 from measurements  : "
              << 10 * std::log10(pClean / pNoise * c1.sps) << " dB (want " << c1.snrDb << ")\n";

    // ---- Test 2: single carrier with CFO + phase; conventions and the 90-degree ambiguity ----
    SimConfig c2;
    c2.mode = "SC"; c2.snrDb = 20.0; c2.eps[0] = 0.25;
    SimResult r2 = simulate(symA, symB, c2);
    std::cout << "SC, exact f/theta       : frames A=" << framesFrom(recoverSymbols(r2.rx, c2, 0, c2.f[0], c2.theta[0])) << "\n";
    std::cout << "SC, theta off by 90 deg : frames A=" << framesFrom(recoverSymbols(r2.rx, c2, 0, c2.f[0], c2.theta[0] + PI / 2)) << "\n";
    std::cout << "SC, theta off by 45 deg : frames A=" << framesFrom(recoverSymbols(r2.rx, c2, 0, c2.f[0], c2.theta[0] + PI / 4)) << "  (45 deg is NOT an ambiguity)\n";

    // ---- Test 3: single-carrier SER against theory ----
    for (double snr : {4.0, 6.0, 8.0}) {
        SimConfig c3 = c2; c3.snrDb = snr;
        SimResult r3 = simulate(symA, symB, c3);
        double esn0 = std::pow(10.0, snr / 10.0), q = Qfunc(std::sqrt(esn0));
        double ser = symbolErrorRate(recoverSymbols(r3.rx, c3, 0, c3.f[0], c3.theta[0]), symA);
        std::cout << "SC Es/N0 " << snr << " dB: SER=" << ser << " theory=" << 2 * q - q * q << "\n";
    }

    // ---- Test 4: two carriers. Remove one with the true parameters, decode the other ----
    SimConfig c4;
    c4.pdDb = 10.0; c4.snrDb = 30.0;                               // eps = {0, 0.25}
    SimResult r4 = simulate(symA, symB, c4);
    std::vector<Cx> onlyA = makeCarrier(symA, c4, 0, r4.P[0]);
    std::vector<Cx> onlyB = makeCarrier(symB, c4, 1, r4.P[1]);
    std::vector<Cx> afterRemoveB(r4.rx.size()), afterRemoveA(r4.rx.size());
    for (size_t n = 0; n < r4.rx.size(); ++n) {
        afterRemoveB[n] = r4.rx[n] - onlyB[n];
        afterRemoveA[n] = r4.rx[n] - onlyA[n];
    }
    std::cout << "CnC PD=10 dB: remove B -> frames A=" << framesFrom(recoverSymbols(afterRemoveB, c4, 0, c4.f[0], c4.theta[0]))
              << ", remove A -> frames B=" << framesFrom(recoverSymbols(afterRemoveA, c4, 1, c4.f[1], c4.theta[1])) << "\n";
    std::cout << "CnC, A decoded directly (B left in as interference): frames A="
              << framesFrom(recoverSymbols(r4.rx, c4, 0, c4.f[0], c4.theta[0])) << "\n";

    // ---- Test 5: write the files and read the I/Q file back ----
    SimConfig cf;                                                   // CnC, PD 4 dB, 12 dB SNR
    cf.pdDb = 4.0; cf.snrDb = 12.0;
    SimResult rf = simulate(symA, symB, cf);
    std::string dir = "out";
    std::filesystem::create_directories(dir);
    writeIq(dir + "/iq.bin", rf.rx);
    writeBitsU8(dir + "/bits_A.bin", bitsA);
    writeBitsU8(dir + "/bits_B.bin", bitsB);
    writeIq(dir + "/symbols_A.bin", symA);
    writeIq(dir + "/symbols_B.bin", symB);
    writeTruthJson(dir + "/truth.json", cf, rf, msgA, msgB, symA.size(), rf.rx.size());

    std::cout << "iq.bin size: " << std::filesystem::file_size(dir + "/iq.bin") << " bytes (want "
              << rf.rx.size() * 8 << ")\n";
    std::ifstream in(dir + "/iq.bin", std::ios::binary);
    double worst = 0;
    for (size_t n = 0; n < rf.rx.size(); ++n) {
        float iq[2];
        in.read(reinterpret_cast<char*>(iq), sizeof(iq));
        worst = std::max(worst, std::abs(Cx(iq[0], iq[1]) - rf.rx[n]));
    }
    std::cout << "Read-back max difference: " << worst << " (float32 rounding only)\n";

        {
        // easy SC: no impairment except noise
        SimConfig ct; ct.mode = "SC"; ct.snrDb = 25.0;
        ct.f[0] = 0.0; ct.theta[0] = 0.0; ct.eps[0] = 0.0;
        SimResult rt = simulate(symA, symB, ct);
        std::string d = dir + "/sc_clean";
        std::filesystem::create_directories(d);
        writeIq(d + "/iq.bin", rt.rx);
        writeIq(d + "/symbols_A.bin", symA);
        writeTruthJson(d + "/truth.json", ct, rt, msgA, msgB, symA.size(), rt.rx.size());
        std::cout << "SC clean scenario written to " << d << "\n";
    }
    {
        // hard SC: CFO + phase + fractional delay + noise
        SimConfig ct; ct.mode = "SC"; ct.snrDb = 25.0;
        SimResult rt = simulate(symA, symB, ct);
        std::string d = dir + "/sc";
        std::filesystem::create_directories(d);
        writeIq(d + "/iq.bin", rt.rx);
        writeIq(d + "/symbols_A.bin", symA);
        writeTruthJson(d + "/truth.json", ct, rt, msgA, msgB, symA.size(), rt.rx.size());
        std::cout << "SC impaired scenario written to " << d << "\n";
    }
    {
        // High-SNR CnC to prove JAMLSE works
        SimConfig ct; ct.mode = "CnC"; ct.pdDb = 4.0; ct.snrDb = 20.0;
        SimResult rt = simulate(symA, symB, ct);
        std::string d = dir + "/cnc_clean";
        std::filesystem::create_directories(d);
        writeIq(d + "/iq.bin", rt.rx);
        writeIq(d + "/symbols_A.bin", symA);
        writeIq(d + "/symbols_B.bin", symB);
        writeTruthJson(d + "/truth.json", ct, rt, msgA, msgB, symA.size(), rt.rx.size());
        std::cout << "CnC clean (20dB SNR) written to " << d << "\n";
    }
        for (int snr = 8; snr <= 20; snr += 2) {
        SimConfig ct; ct.mode = "CnC"; ct.pdDb = 4.0; ct.snrDb = snr;
        SimResult rt = simulate(symA, symB, ct);
        std::string d = dir + "/cnc_" + std::to_string(snr) + "db";
        std::filesystem::create_directories(d);
        writeIq(d + "/iq.bin", rt.rx);
        writeIq(d + "/symbols_A.bin", symA);
        writeIq(d + "/symbols_B.bin", symB);
        writeTruthJson(d + "/truth.json", ct, rt, msgA, msgB, symA.size(), rt.rx.size());
        std::cout << "CnC " << snr << "dB written to " << d << "\n";
    }
}