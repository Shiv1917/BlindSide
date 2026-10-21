// truth.hpp
#pragma once
#include <string>
#include <vector>

namespace rx {
struct StreamTruth {
    std::string name;
    std::string message;
    double      power                  = 0.0;
    double      cfoCyclesPerSample     = 0.0;
    double      cfoHz                  = 0.0;
    double      phaseRad               = 0.0;
    double      fractionalDelaySymbols = 0.0;
    std::string symbolsFile;
    std::string infoBitsFile;
};
struct Truth {
    std::string modulation;
    std::string mode;
    double      samplesPerSymbol       = 0.0;
    double      symbolRateHz           = 0.0;
    double      sampleRateHz           = 0.0;
    double      rolloff                = 0.0;
    int         filterSpanSymbols      = 0;
    size_t      numSymbolsPerStream    = 0;
    size_t      numSamples             = 0;
    double      snrDb                  = 0.0;
    double      noiseVariancePerSample = 0.0;
    double      powerDifferenceDb      = 0.0;
    int         frameBits              = 0;
    std::vector<int>         scramblerTaps;
    std::vector<StreamTruth> streams;
};
Truth loadTruth(const std::string& path);
}