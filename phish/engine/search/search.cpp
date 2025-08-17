#include "engine/search/search.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>

namespace phish::search {

static int piece_value(PieceType pt) {
    switch (pt) {
        case PAWN: return 100;
        case KNIGHT: return 320;
        case BISHOP: return 330;
        case ROOK: return 500;
        case QUEEN: return 900;
        default: return 0;
    }
}

static inline int popcount64(U64 v) { return __builtin_popcountll(v); }

static int evaluate(const board::Position& pos) {
    int score = 0;
    for (int c = 0; c < COLOR_NB; ++c) {
        int sign = (c == WHITE) ? 1 : -1;
        for (int pt = PAWN; pt <= QUEEN; ++pt) {
            Piece piece = static_cast<Piece>(c * 6 + pt);
            U64 bb = pos.pieces(piece);
            score += sign * popcount64(bb) * piece_value(static_cast<PieceType>(pt));
        }
    }
    return (pos.side_to_move() == WHITE) ? score : -score;
}

TranspositionTable::TranspositionTable(std::size_t mb) { resize(mb); }
TranspositionTable::~TranspositionTable() { delete[] table; }

void TranspositionTable::resize(std::size_t mb) {
    delete[] table;
    std::size_t bytes = mb * 1024ULL * 1024ULL;
    numEntries = bytes / sizeof(TTEntry);
    if (numEntries == 0) numEntries = 1;
    table = new TTEntry[numEntries];
    clear();
}

void TranspositionTable::clear() {
    if (!table) return;
    std::memset(table, 0, numEntries * sizeof(TTEntry));
    ++currentAge;
}

void TranspositionTable::store(U64 key, int depth, int score, int eval, uint8_t flag, movegen::Move move) {
    if (!table) return;
    std::size_t idx = key % numEntries;
    TTEntry& e = table[idx];
    if (e.key != key || depth >= e.depth) {
        e.key = key;
        e.depth = static_cast<uint16_t>(depth);
        e.score = static_cast<int16_t>(score);
        e.eval = static_cast<int16_t>(eval);
        e.flag = flag;
        e.move = move;
        e.age = currentAge;
    }
}

bool TranspositionTable::probe(U64 key, TTEntry& out) const {
    if (!table) return false;
    std::size_t idx = key % numEntries;
    const TTEntry& e = table[idx];
    if (e.key == key) { out = e; return true; }
    return false;
}

static constexpr int MAX_PLY = 128;
static uint64_t g_nodes;
static std::chrono::steady_clock::time_point g_start;
static int64_t g_timeBudgetMs;
static bool g_stop;
static movegen::Move g_killers[MAX_PLY][2];
static int g_history[64][64];

static inline bool time_up() {
    if (g_timeBudgetMs <= 0) return false;
    if ((g_nodes & 0x1FFF) != 0) return false; // check every ~8192 nodes
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_start).count();
    return elapsed >= g_timeBudgetMs;
}

static int qsearch(board::Position& pos, int alpha, int beta, int ply) {
    if (time_up()) { g_stop = true; return alpha; }
    ++g_nodes;
    int stand = evaluate(pos);
    if (stand >= beta) return beta;
    if (stand > alpha) alpha = stand;
    return alpha;
}

static int mvv_lva(const board::Position& pos, movegen::Move m) {
    int victimVal = 0;
    if (movegen::is_enpassant(m)) victimVal = piece_value(PAWN);
    else {
        int victim = pos.piece_at(movegen::to_sq(m));
        if (victim != NO_PIECE) victimVal = piece_value(static_cast<PieceType>(victim % 6));
    }
    int attacker = pos.piece_at(movegen::from_sq(m));
    int attackerVal = attacker != NO_PIECE ? piece_value(static_cast<PieceType>(attacker % 6)) : 0;
    return victimVal * 16 - attackerVal;
}

static int score_move(movegen::Move m, movegen::Move ttMove, int ply, const board::Position& pos) {
    if (m == ttMove) return 2'000'000;
    if (movegen::is_capture(m)) return 1'000'000 + mvv_lva(pos, m);
    if (g_killers[ply][0] == m) return 900'000;
    if (g_killers[ply][1] == m) return 800'000;
    return g_history[movegen::from_sq(m)][movegen::to_sq(m)];
}

static int negamax(board::Position& pos, int depth, int alpha, int beta, TranspositionTable& tt, int ply) {
    if (g_stop) return alpha;
    if (depth == 0) return qsearch(pos, alpha, beta, ply);
    if (time_up()) { g_stop = true; return alpha; }

    TTEntry tte{};
    movegen::Move ttMove = 0;
    if (tt.probe(pos.key(), tte) && tte.depth >= depth) {
        if (tte.flag == 0) return tte.score;
        if (tte.flag == 1 && tte.score <= alpha) return alpha;
        if (tte.flag == 2 && tte.score >= beta) return beta;
        ttMove = tte.move;
    }

    if (depth >= 3 && !pos.in_check()) {
        board::StateInfo st;
        if (pos.make_null_move(st)) {
            int R = 2;
            int score = -negamax(pos, depth - 1 - R, -beta, -beta + 1, tt, ply + 1);
            pos.unmake_null_move(st);
            if (g_stop) return alpha;
            if (score >= beta) return beta;
        }
    }

    board::StateInfo st;
    movegen::MoveList moves;
    pos.generate_legal(moves);
    if (moves.size() == 0) {
        if (pos.in_check()) return -30000 + (100 - depth);
        return 0;
    }

    std::sort(moves.moves.begin(), moves.moves.end(), [&](movegen::Move a, movegen::Move b){
        return score_move(a, ttMove, ply, pos) > score_move(b, ttMove, ply, pos);
    });

    int bestScore = std::numeric_limits<int>::min() / 2;
    movegen::Move bestMove = 0;
    int alphaOrig = alpha;

    int moveIndex = 0;
    for (auto m : moves.moves) {
        ++g_nodes;
        if (time_up()) { g_stop = true; break; }
        if (!pos.make_move(m, st)) continue;

        int newDepth = depth - 1;
        bool givesCheck = pos.in_check();
        // LMR for late quiets
        if (!pos.in_check() && !movegen::is_capture(m) && !movegen::is_promotion(m) && !givesCheck && moveIndex > 3 && depth >= 3 && m != ttMove) {
            newDepth -= 1;
        }

        int score;
        if (bestMove == 0) {
            score = -negamax(pos, newDepth, -beta, -alpha, tt, ply + 1);
        } else {
            score = -negamax(pos, newDepth, -alpha - 1, -alpha, tt, ply + 1);
            if (!g_stop && score > alpha && score < beta) {
                score = -negamax(pos, newDepth, -beta, -alpha, tt, ply + 1);
            }
        }
        pos.unmake_move(m, st);
        ++moveIndex;
        if (g_stop) break;

        if (score > bestScore) {
            bestScore = score;
            bestMove = m;
        }
        if (bestScore > alpha) {
            // Update history on quiet alpha improvement
            if (!movegen::is_capture(m) && !movegen::is_promotion(m)) {
                g_history[movegen::from_sq(m)][movegen::to_sq(m)] += depth * depth;
            }
            alpha = bestScore;
        }
        if (alpha >= beta) {
            // Beta cutoff: update killers and history
            if (!movegen::is_capture(m) && !movegen::is_promotion(m)) {
                g_killers[ply][1] = g_killers[ply][0];
                g_killers[ply][0] = m;
                g_history[movegen::from_sq(m)][movegen::to_sq(m)] += depth * depth;
            }
            break;
        }
    }

    uint8_t flag = 0;
    if (bestScore <= alphaOrig) flag = 1;
    else if (bestScore >= beta) flag = 2;
    tt.store(pos.key(), depth, bestScore, 0, flag, bestMove);
    return bestScore;
}

SearchResult think(board::Position& pos, const Limits& limits, TranspositionTable& tt) {
    SearchResult sr;
    std::fill(&g_history[0][0], &g_history[0][0] + 64 * 64, 0);
    std::fill(&g_killers[0][0], &g_killers[0][0] + MAX_PLY * 2, 0);

    movegen::MoveList legal;
    pos.generate_legal(legal);
    if (legal.size() == 0) { sr.bestMove = 0; return sr; }

    g_nodes = 0;
    g_stop = false;
    g_start = std::chrono::steady_clock::now();
    // Simple time budget: allocate ~1/30 of remaining time plus 60% of increment, min 10ms
    int64_t base = limits.timeMs <= 0 ? 0 : limits.timeMs / 30;
    g_timeBudgetMs = (limits.timeMs <= 0 ? 0 : base + static_cast<int64_t>(limits.incMs * 6 / 10));
    if (g_timeBudgetMs > 0 && g_timeBudgetMs < 10) g_timeBudgetMs = 10;

    movegen::Move bestMove = legal.moves.front();
    int lastScore = 0;

    for (int d = 1; d <= limits.depth; ++d) {
        int window = (d >= 4) ? 50 : 200;
        int alpha = lastScore - window;
        int beta = lastScore + window;

        int score = negamax(pos, d, alpha, beta, tt, 0);
        if (!g_stop && (score <= alpha || score >= beta)) {
            // Re-search with wide window
            score = negamax(pos, d, -30000, 30000, tt, 0);
        }
        if (g_stop) break;

        lastScore = score;
        TTEntry tte;
        if (tt.probe(pos.key(), tte) && tte.move != 0) bestMove = tte.move;

        // Stop if time up after this iteration
        if (time_up()) { g_stop = true; break; }
    }

    sr.bestMove = bestMove;
    sr.nodes = g_nodes;
    return sr;
}

} // namespace phish::search