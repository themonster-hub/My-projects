#include "engine/uci/uci.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "engine/util/config.h"
#include "engine/bitboard/bitboard.h"
#include "engine/board/position.h"
#include "engine/movegen/move.h"

namespace phish::uci {

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::vector<std::string> split_tokens(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string tok;
    while (iss >> tok) tokens.push_back(tok);
    return tokens;
}

void send_id() {
    std::cout << "id name Phish 0.1.0" << '\n';
    std::cout << "id author OpenAI" << '\n';
}

void send_options() {
    const auto hw_threads = static_cast<int>(std::thread::hardware_concurrency() == 0 ? 1 : std::thread::hardware_concurrency());
    std::cout << "option name Hash type spin default 16 min 1 max 1048576" << '\n';
    std::cout << "option name Threads type spin default 1 min 1 max " << hw_threads << '\n';
    std::cout << "option name Ponder type check default false" << '\n';
    std::cout << "option name SyzygyPath type string default " << '\n';
    std::cout << "option name SyzygyProbeDepth type spin default 4 min 0 max 20" << '\n';
    std::cout << "option name UseNNUE type check default true" << '\n';
    std::cout << "option name EvalFile type string default phish.nnue" << '\n';
    std::cout << "option name Contempt type spin default 0 min -1000 max 1000" << '\n';
    std::cout << "option name MoveOverhead type spin default 30 min 0 max 1000" << '\n';
    std::cout << "option name MultiPV type spin default 1 min 1 max 256" << '\n';
}

struct PositionState {
    board::Position pos;
};

void handle_setoption(const std::string& line) {
    const auto name_pos = line.find("name ");
    if (name_pos == std::string::npos) return;
    const auto value_pos = line.find(" value ", name_pos);

    std::string name;
    std::string value;

    if (value_pos == std::string::npos) {
        name = line.substr(name_pos + 5);
    } else {
        name = line.substr(name_pos + 5, value_pos - (name_pos + 5));
        value = line.substr(value_pos + 7);
    }

    auto trim = [](std::string& s) {
        const auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
        while (!s.empty() && is_space(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
        while (!s.empty() && is_space(static_cast<unsigned char>(s.back()))) s.pop_back();
    };
    trim(name);
    trim(value);

    set_option(name, value);
}

void handle_position(const std::vector<std::string>& tokens, PositionState& st) {
    st.pos = board::Position();

    if (tokens.size() < 2) return;
    std::size_t idx = 1;

    if (tokens[idx] == "startpos") {
        st.pos.set_fen("startpos");
        ++idx;
    } else if (tokens[idx] == "fen") {
        ++idx;
        std::ostringstream fen;
        int fen_fields = 0;
        for (; idx < tokens.size(); ++idx) {
            if (tokens[idx] == "moves") break;
            if (fen_fields > 0) fen << ' ';
            fen << tokens[idx];
            ++fen_fields;
            if (fen_fields >= 6) break;
        }
        st.pos.set_fen(fen.str());
    }

    if (idx < tokens.size() && tokens[idx] == "moves") {
        ++idx;
        for (; idx < tokens.size(); ++idx) {
            st.pos.play_uci_move(tokens[idx]);
        }
    }
}

void handle_go(const std::vector<std::string>& /*tokens*/, const PositionState& /*st*/) {
    std::cout << "bestmove 0000" << '\n' << std::flush;
}

void handle_perft(const std::vector<std::string>& tokens, PositionState& st) {
    int depth = 1;
    if (tokens.size() >= 2) depth = std::atoi(tokens[1].c_str());
    std::uint64_t nodes = st.pos.perft(depth);
    std::cout << "info string perft " << depth << " nodes " << nodes << '\n';
}

} // namespace

void run() {
    bitboard::init();
    PositionState state;
    state.pos.set_fen("startpos");

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        const auto tokens = split_tokens(line);
        if (tokens.empty()) continue;

        const std::string cmd = tokens[0];

        if (cmd == "uci") {
            send_id();
            send_options();
            std::cout << "uciok" << '\n' << std::flush;
        } else if (cmd == "isready") {
            std::cout << "readyok" << '\n' << std::flush;
        } else if (cmd == "setoption") {
            handle_setoption(line);
        } else if (cmd == "ucinewgame") {
            state.pos.set_fen("startpos");
        } else if (cmd == "position") {
            handle_position(tokens, state);
        } else if (cmd == "go") {
            handle_go(tokens, state);
        } else if (cmd == "stop") {
            std::cout << "bestmove 0000" << '\n' << std::flush;
        } else if (cmd == "bench") {
            std::cout << "info string bench not implemented" << '\n';
            std::cout << "bestmove 0000" << '\n' << std::flush;
        } else if (cmd == "perft") {
            handle_perft(tokens, state);
        } else if (cmd == "quit") {
            break;
        } else if (cmd == "ponderhit") {
        } else if (cmd == "eval" || cmd == "d") {
            std::cout << "info string debug print not implemented" << '\n' << std::flush;
        } else if (cmd == "help") {
            std::cout << "info string commands: uci, isready, setoption, ucinewgame, position, go, stop, perft, bench, quit" << '\n' << std::flush;
        }
    }
}

} // namespace phish::uci