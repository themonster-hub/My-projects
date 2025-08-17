#pragma once

#include <string>

namespace phish {

struct Options {
    int threads = 1;
    int hashMb = 16;
    bool ponder = false;
    std::string syzygyPath;
    int syzygyProbeDepth = 4;
    bool useNNUE = true;
    std::string evalFile = "phish.nnue";
    int contempt = 0; // in centipawns
    int moveOverheadMs = 30;
    int multiPV = 1;
};

Options& options();

void set_option(const std::string& name, const std::string& value);

} // namespace phish