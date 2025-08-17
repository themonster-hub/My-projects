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
#include <atomic>
#include <mutex>

#include "engine/util/config.h"
#include "engine/bitboard/bitboard.h"
#include "engine/board/position.h"
#include "engine/movegen/move.h"
#include "engine/util/zobrist.h"
#include "engine/search/search.h"

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

std::string move_to_uci(movegen::Move m) {
    char buf[8]{};
    auto f = [](Square s) { return 'a' + file_of(s); };
    auto r = [](Square s) { return '1' + rank_of(s); };
    Square from = movegen::from_sq(m);
    Square to = movegen::to_sq(m);
    buf[0] = static_cast<char>(f(from));
    buf[1] = static_cast<char>(r(from));
    buf[2] = static_cast<char>(f(to));
    buf[3] = static_cast<char>(r(to));
    if (movegen::is_promotion(m)) {
        char p = 'q';
        switch (movegen::promotion_piece(m)) {
            case KNIGHT: p = 'n'; break;
            case BISHOP: p = 'b'; break;
            case ROOK: p = 'r'; break;
            case QUEEN: p = 'q'; break;
            default: break;
        }
        buf[4] = p;
        buf[5] = '\0';
    } else {
        buf[4] = '\0';
    }
    return std::string(buf);
}

struct SearchController {
    std::atomic<bool> thinking{false};
    std::atomic<bool> stop{false};
    std::thread worker;
    movegen::Move lastBest{0};
    uint64_t lastNodes{0};
    std::mutex mtx; // protects lastBest/lastNodes
};

void start_search(SearchController& ctrl, PositionState& st, search::TranspositionTable& tt, const search::Limits& lim) {
    // Ensure previous is stopped
    if (ctrl.thinking.load()) {
        ctrl.stop.store(true);
        if (ctrl.worker.joinable()) ctrl.worker.join();
        ctrl.thinking.store(false);
    }
    ctrl.stop.store(false);

    ctrl.worker = std::thread([&ctrl, &st, &tt, lim](){
        auto infoCb = [&](int depth, int score, uint64_t nodes, int64_t elapsedMs, const std::vector<movegen::Move>& pv){
            int64_t nps = (elapsedMs > 0) ? static_cast<int64_t>((nodes * 1000) / elapsedMs) : 0;
            std::cout << "info depth " << depth
                      << " score cp " << score
                      << " time " << elapsedMs
                      << " nodes " << nodes
                      << " nps " << nps
                      << " pv";
            for (auto m : pv) std::cout << ' ' << move_to_uci(m);
            std::cout << '\n' << std::flush;
            std::lock_guard<std::mutex> lk(ctrl.mtx);
            if (!pv.empty()) ctrl.lastBest = pv.front();
            ctrl.lastNodes = nodes;
        };
        search::SearchResult sr = search::think(st.pos, lim, tt, ctrl.stop, infoCb);
        {
            std::lock_guard<std::mutex> lk(ctrl.mtx);
            if (sr.bestMove != 0) ctrl.lastBest = sr.bestMove;
            ctrl.lastNodes = sr.nodes;
        }
        // Only print bestmove if we were not externally stopped
        if (!ctrl.stop.load(std::memory_order_relaxed)) {
            std::cout << "bestmove " << move_to_uci(sr.bestMove) << '\n' << std::flush;
        }
        ctrl.thinking.store(false);
    });
    ctrl.thinking.store(true);
}

void stop_search(SearchController& ctrl) {
    if (ctrl.thinking.load()) {
        ctrl.stop.store(true);
        if (ctrl.worker.joinable()) ctrl.worker.join();
        ctrl.thinking.store(false);
    }
}

void handle_go(const std::vector<std::string>& tokens, PositionState& st, search::TranspositionTable& tt, SearchController& ctrl) {
    int depth = 64; // default to deep iterative
    int64_t movetime = 0;
    int64_t wtime = 0, btime = 0, winc = 0, binc = 0;
    int64_t nodes = 0;
    bool infinite = false;

    for (std::size_t i = 1; i < tokens.size(); ++i) {
        const std::string& t = tokens[i];
        if (t == "depth" && i + 1 < tokens.size()) depth = std::atoi(tokens[++i].c_str());
        else if (t == "movetime" && i + 1 < tokens.size()) movetime = std::atoll(tokens[++i].c_str());
        else if (t == "wtime" && i + 1 < tokens.size()) wtime = std::atoll(tokens[++i].c_str());
        else if (t == "btime" && i + 1 < tokens.size()) btime = std::atoll(tokens[++i].c_str());
        else if (t == "winc" && i + 1 < tokens.size()) winc = std::atoll(tokens[++i].c_str());
        else if (t == "binc" && i + 1 < tokens.size()) binc = std::atoll(tokens[++i].c_str());
        else if (t == "nodes" && i + 1 < tokens.size()) nodes = std::atoll(tokens[++i].c_str());
        else if (t == "infinite") infinite = true;
        else if (t == "ponder") { /* ignored for now */ }
        else if (t == "searchmoves") { /* not implemented */ break; }
    }

    search::Limits lim;
    lim.depth = depth;
    lim.movetimeMs = movetime;
    bool white = st.pos.side_to_move() == WHITE;
    lim.timeMs = white ? wtime : btime;
    lim.incMs = white ? winc : binc;
    lim.maxNodes = nodes;
    lim.infinite = infinite;

    start_search(ctrl, st, tt, lim);
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
    zobrist::init();
    PositionState state;
    state.pos.set_fen("startpos");
    search::TranspositionTable tt(static_cast<std::size_t>(options().hashMb));

    SearchController ctrl;

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
            // Resize TT if hash changed
            tt.resize(static_cast<std::size_t>(options().hashMb));
        } else if (cmd == "ucinewgame") {
            stop_search(ctrl);
            state.pos.set_fen("startpos");
            tt.clear();
        } else if (cmd == "position") {
            stop_search(ctrl);
            handle_position(tokens, state);
        } else if (cmd == "go") {
            handle_go(tokens, state, tt, ctrl);
        } else if (cmd == "stop") {
            stop_search(ctrl);
            movegen::Move bm;
            uint64_t nodes;
            {
                std::lock_guard<std::mutex> lk(ctrl.mtx);
                bm = ctrl.lastBest;
                nodes = ctrl.lastNodes;
            }
            if (bm == 0) {
                // As a fallback, pick first legal move
                movegen::MoveList legal;
                state.pos.generate_legal(legal);
                bm = legal.size() ? legal.moves.front() : 0;
            }
            std::cout << "info string nodes " << nodes << '\n';
            std::cout << "bestmove " << move_to_uci(bm) << '\n' << std::flush;
        } else if (cmd == "bench") {
            std::cout << "info string bench not implemented" << '\n';
            std::cout << "bestmove 0000" << '\n' << std::flush;
        } else if (cmd == "perft") {
            stop_search(ctrl);
            handle_perft(tokens, state);
        } else if (cmd == "quit") {
            stop_search(ctrl);
            break;
        } else if (cmd == "ponderhit") {
            // Not implemented; continue the current search as normal
        } else if (cmd == "eval" || cmd == "d") {
            std::cout << "info string debug print not implemented" << '\n' << std::flush;
        } else if (cmd == "help") {
            std::cout << "info string commands: uci, isready, setoption, ucinewgame, position, go, stop, perft, bench, quit" << '\n' << std::flush;
        }
    }
}

} // namespace phish::uci