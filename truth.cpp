// truth.cpp
#include "truth.hpp"
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace rx {

static std::string slurp(const std::string& p) {
    std::ifstream f(p);
    if (!f) throw std::runtime_error("Cannot open " + p);
    std::stringstream ss; ss << f.rdbuf(); return ss.str();
}

static double findNumber(const std::string& s, const std::string& key) {
    const std::string needle = "\"" + key + "\":";
    auto pos = s.find(needle);
    if (pos == std::string::npos) return 0.0;
    pos += needle.size();
    while (pos < s.size() && std::isspace((unsigned char)s[pos])) ++pos;
    size_t start = pos;
    while (pos < s.size() && (std::isdigit((unsigned char)s[pos]) || s[pos]=='-' ||
                              s[pos]=='+' || s[pos]=='.' || s[pos]=='e' || s[pos]=='E'))
        ++pos;
    return std::stod(s.substr(start, pos - start));
}

static std::string findString(const std::string& s, const std::string& key) {
    const std::string needle = "\"" + key + "\":";
    auto pos = s.find(needle);
    if (pos == std::string::npos) return "";
    pos += needle.size();
    while (pos < s.size() && std::isspace((unsigned char)s[pos])) ++pos;
    if (pos >= s.size() || s[pos] != '"') return "";
    ++pos;
    auto end = s.find('"', pos);
    return s.substr(pos, end - pos);
}

Truth loadTruth(const std::string& path) {
    const std::string s = slurp(path);
    Truth t;
    t.modulation             = findString(s, "modulation");
    t.mode                   = findString(s, "mode");
    t.samplesPerSymbol       = findNumber(s, "samplesPerSymbol");
    t.symbolRateHz           = findNumber(s, "symbolRateHz");
    t.sampleRateHz           = findNumber(s, "sampleRateHz");
    t.rolloff                = findNumber(s, "rolloff");
    t.filterSpanSymbols      = (int)findNumber(s, "filterSpanSymbols");
    t.numSymbolsPerStream    = (size_t)findNumber(s, "numSymbolsPerStream");
    t.numSamples             = (size_t)findNumber(s, "numSamples");
    t.snrDb                  = findNumber(s, "snrDb");
    t.noiseVariancePerSample = findNumber(s, "noiseVariancePerSample");
    t.powerDifferenceDb      = findNumber(s, "powerDifferenceDb");
    t.frameBits              = (int)findNumber(s, "frameBits");

    if (auto pos = s.find("\"scramblerTaps\":"); pos != std::string::npos) {
        auto lb = s.find('[', pos); auto rb = s.find(']', lb);
        std::stringstream ss(s.substr(lb + 1, rb - lb - 1));
        std::string tok;
        while (std::getline(ss, tok, ',')) t.scramblerTaps.push_back(std::stoi(tok));
    }

    for (const std::string nm : {"A", "B"}) {
        const std::string needle = "\"name\": \"" + nm + "\"";
        auto pos = s.find(needle);
        if (pos == std::string::npos) continue;
        auto endPos = (nm == "A") ? s.find("\"name\": \"B\"", pos)
                                  : std::string::npos;
        if (endPos == std::string::npos) endPos = s.size();
        const std::string seg = s.substr(pos, endPos - pos);

        StreamTruth st;
        st.name                   = nm;
        st.message                = findString(seg, "message");
        st.power                  = findNumber(seg, "power");
        st.cfoCyclesPerSample     = findNumber(seg, "cfoCyclesPerSample");
        st.cfoHz                  = findNumber(seg, "cfoHz");
        st.phaseRad               = findNumber(seg, "phaseRad");
        st.fractionalDelaySymbols = findNumber(seg, "fractionalDelaySymbols");
        st.symbolsFile            = findString(seg, "symbolsFile");
        st.infoBitsFile           = findString(seg, "infoBitsFile");
        t.streams.push_back(st);
    }
    return t;
}
}