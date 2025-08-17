#include "engine/util/config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <mutex>
#include <string>
#include <string_view>

namespace phish {

static Options g_options;
static std::mutex g_optionsMutex;

Options& options() { return g_options; }

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

void set_option(const std::string& name, const std::string& value) {
    std::scoped_lock lock(g_optionsMutex);
    const std::string lname = to_lower(name);

    if (iequals(lname, "hash")) {
        const long v = std::strtol(value.c_str(), nullptr, 10);
        if (v > 0) g_options.hashMb = static_cast<int>(v);
    } else if (iequals(lname, "threads")) {
        const long v = std::strtol(value.c_str(), nullptr, 10);
        if (v > 0) g_options.threads = static_cast<int>(v);
    } else if (iequals(lname, "ponder")) {
        g_options.ponder = iequals(value, "true") || iequals(value, "1") || iequals(value, "on");
    } else if (iequals(lname, "syzygypath")) {
        g_options.syzygyPath = value;
    } else if (iequals(lname, "syzygyprobedepth")) {
        const long v = std::strtol(value.c_str(), nullptr, 10);
        if (v >= 0) g_options.syzygyProbeDepth = static_cast<int>(v);
    } else if (iequals(lname, "usennue")) {
        g_options.useNNUE = iequals(value, "true") || iequals(value, "1") || iequals(value, "on");
    } else if (iequals(lname, "evalfile")) {
        g_options.evalFile = value;
    } else if (iequals(lname, "contempt")) {
        const long v = std::strtol(value.c_str(), nullptr, 10);
        g_options.contempt = static_cast<int>(v);
    } else if (iequals(lname, "moveoverhead")) {
        const long v = std::strtol(value.c_str(), nullptr, 10);
        if (v >= 0) g_options.moveOverheadMs = static_cast<int>(v);
    } else if (iequals(lname, "multipv")) {
        const long v = std::strtol(value.c_str(), nullptr, 10);
        if (v >= 1) g_options.multiPV = static_cast<int>(v);
    }
}

} // namespace phish