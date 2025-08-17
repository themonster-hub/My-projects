#include "engine/search/search.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <chrono>

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

static uint64_t g_nodes;

// Simple killers and history
static movegen::Move g_killer[128][2]{}; // depth -> two killers
static int g_history[12][64]{};          // moved piece -> to-square history

static int qsearch(board::Position& pos, int alpha, int beta, std::atomic<bool>& stop) {
    if (stop.load(std::memory_order_relaxed)) return alpha;
    ++g_nodes;
    int stand = evaluate(pos);
    if (stand >= beta) return beta;
    if (stand > alpha) alpha = stand;

    // Generate captures only (very basic)
    movegen::MoveList moves;
    pos.generate_legal(moves);
    std::vector<movegen::Move> caps;
    caps.reserve(moves.size());
    for (auto m : moves.moves) if (movegen::is_capture(m) || movegen::is_promotion(m)) caps.push_back(m);
    if (caps.empty()) return alpha;

    // MVV-LVA style score: prefer captures and promotions
    auto scoreCap = [&](movegen::Move m) {
        int s = 0;
        if (movegen::is_capture(m)) s += 10000;
        if (movegen::is_promotion(m)) s += 9000 + piece_value(movegen::promotion_piece(m));
        return s;
    };
    std::sort(caps.begin(), caps.end(), [&](movegen::Move a, movegen::Move b){ return scoreCap(a) > scoreCap(b); });

    board::StateInfo st;
    for (auto m : caps) {
        if (!pos.make_move(m, st)) continue;
        int score = -qsearch(pos, -beta, -alpha, stop);
        pos.unmake_move(m, st);
        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
        if (stop.load(std::memory_order_relaxed)) return alpha;
    }
    return alpha;
}

static int score_move(movegen::Move m, movegen::Move ttMove, movegen::Move killer1, movegen::Move killer2, Piece movedPiece) {
    if (m == ttMove) return 1'000'000;
    if (m == killer1) return 900'000;
    if (m == killer2) return 800'000;
    int base = 0;
    if (movegen::is_capture(m)) base += 100'000;
    base += g_history[movedPiece][movegen::to_sq(m)];
    if (movegen::is_promotion(m)) base += 50'000 + piece_value(movegen::promotion_piece(m));
    return base;
}

static int negamax(board::Position& pos, int depth, int alpha, int beta, TranspositionTable& tt, std::atomic<bool>& stop, int ply) {
    if (stop.load(std::memory_order_relaxed)) return alpha;

    if (depth == 0) return qsearch(pos, alpha, beta, stop);

    TTEntry tte{};
    movegen::Move ttMove = 0;
    if (tt.probe(pos.key(), tte) && tte.depth >= depth) {
        if (tte.flag == 0) return tte.score;
        if (tte.flag == 1 && tte.score <= alpha) return alpha;
        if (tte.flag == 2 && tte.score >= beta) return beta;
        ttMove = tte.move;
    }

    // Null-move pruning
    if (depth >= 3 && !pos.in_check()) {
        board::StateInfo st;
        if (pos.make_null_move(st)) {
            int R = 2;
            int score = -negamax(pos, depth - 1 - R, -beta, -beta + 1, tt, stop, ply + 1);
            pos.unmake_null_move(st);
            if (score >= beta) return beta;
        }
    }

    const bool inCheck = pos.in_check();
    const int staticEval = evaluate(pos);

    board::StateInfo st;
    movegen::MoveList moves;
    pos.generate_legal(moves);
    if (moves.size() == 0) {
        if (inCheck) return -30000 + (100 - depth);
        return 0;
    }

    // Order moves
    movegen::Move killer1 = g_killer[ply][0];
    movegen::Move killer2 = g_killer[ply][1];
    std::sort(moves.moves.begin(), moves.moves.end(), [&](movegen::Move a, movegen::Move b){
        Piece movedA = static_cast<Piece>(pos.piece_at(movegen::from_sq(a)));
        Piece movedB = static_cast<Piece>(pos.piece_at(movegen::from_sq(b)));
        return score_move(a, ttMove, killer1, killer2, movedA) > score_move(b, ttMove, killer1, killer2, movedB);
    });

    int bestScore = std::numeric_limits<int>::min() / 2;
    movegen::Move bestMove = 0;
    int alphaOrig = alpha;

    int moveIndex = 0;
    for (auto m : moves.moves) {
        ++g_nodes;

        const bool isCapOrPromo = movegen::is_capture(m) || movegen::is_promotion(m);

        // Futility pruning: at shallow depths, if static eval is far below alpha and move is quiet
        if (!inCheck && !isCapOrPromo && depth <= 2) {
            int margin = 100 * depth; // simple margin per ply
            if (staticEval + margin <= alpha) {
                ++moveIndex;
                continue;
            }
        }

        if (!pos.make_move(m, st)) { ++moveIndex; continue; }

        // PVS with simple late move reductions for quiet moves
        int score;
        int newDepth = depth - 1;
        if (bestMove == 0) {
            score = -negamax(pos, newDepth, -beta, -alpha, tt, stop, ply + 1);
        } else {
            int reduction = 0;
            if (!inCheck && !isCapOrPromo && depth >= 3) {
                reduction = 1 + ((depth >= 5 && moveIndex >= 5) ? 1 : 0);
                if (newDepth - reduction < 0) reduction = newDepth > 0 ? newDepth - 1 : 0;
            }
            score = -negamax(pos, newDepth - reduction, -alpha - 1, -alpha, tt, stop, ply + 1);
            if (score > alpha) {
                // Re-search at full depth if it improved
                score = -negamax(pos, newDepth, -alpha - 1, -alpha, tt, stop, ply + 1);
                if (score > alpha && score < beta) {
                    score = -negamax(pos, newDepth, -beta, -alpha, tt, stop, ply + 1);
                }
            }
        }
        pos.unmake_move(m, st);

        if (score > bestScore) {
            bestScore = score;
            bestMove = m;
        }
        if (bestScore > alpha) {
            // Update history for quiet moves that improve alpha
            Piece moved = static_cast<Piece>(pos.piece_at(movegen::from_sq(m)));
            if (!movegen::is_capture(m)) {
                g_history[moved][movegen::to_sq(m)] += depth * depth;
                // Update killers
                if (m != killer1) {
                    g_killer[ply][1] = g_killer[ply][0];
                    g_killer[ply][0] = m;
                }
            }
            alpha = bestScore;
        }
        if (alpha >= beta) {
            // Beta cutoff: update history/killers for quiet move
            if (!movegen::is_capture(m)) {
                Piece moved = static_cast<Piece>(pos.piece_at(movegen::from_sq(m)));
                g_history[moved][movegen::to_sq(m)] += depth * depth;
                if (m != killer1) {
                    g_killer[ply][1] = g_killer[ply][0];
                    g_killer[ply][0] = m;
                }
            }
            break;
        }
        if (stop.load(std::memory_order_relaxed)) break;
        ++moveIndex;
    }

    uint8_t flag = 0;
    if (bestScore <= alphaOrig) flag = 1;
    else if (bestScore >= beta) flag = 2;
    tt.store(pos.key(), depth, bestScore, staticEval, flag, bestMove);
    return bestScore;
}

SearchResult think(board::Position& pos, const Limits& limits, TranspositionTable& tt, std::atomic<bool>& stop, InfoCallback infoCb) {
    // Reset counters and heuristics
    g_nodes = 0;
    std::memset(g_killer, 0, sizeof(g_killer));
    std::memset(g_history, 0, sizeof(g_history));

    SearchResult sr;
    movegen::MoveList legal;
    pos.generate_legal(legal);
    if (legal.size() == 0) { sr.bestMove = 0; return sr; }

    movegen::Move bestMove = legal.moves.front();

    auto start = std::chrono::steady_clock::now();

    // Time management: derive soft time budget
    int64_t softTimeBudget = 0;
    if (limits.infinite) {
        softTimeBudget = std::numeric_limits<int64_t>::max();
    } else if (limits.movetimeMs > 0) {
        softTimeBudget = limits.movetimeMs;
    } else if (limits.timeMs > 0) {
        // Spend about 1/30 of remaining time + 0.6 * inc, conservative
        softTimeBudget = limits.timeMs / 30 + (limits.incMs * 3) / 5;
        if (softTimeBudget <= 0) softTimeBudget = limits.timeMs / 40 + limits.incMs / 2;
    } else {
        // No time info: use a small default per move
        softTimeBudget = 50;
    }

    int maxDepth = limits.depth;
    if (limits.infinite) maxDepth = std::max(maxDepth, 128);

    int alpha = -30000, beta = 30000;
    int prevScore = 0;
    for (int d = 1; d <= maxDepth; ++d) {
        // Simple aspiration window around previous score (except at depth 1)
        if (d > 1) {
            int window = 50;
            alpha = std::max(-30000, prevScore - window);
            beta = std::min(30000, prevScore + window);
        } else {
            alpha = -30000; beta = 30000;
        }

        int score = negamax(pos, d, alpha, beta, tt, stop, /*ply=*/0);
        // If fail-low/high on aspiration, re-search with full window
        if (score <= alpha || score >= beta) {
            alpha = -30000; beta = 30000;
            score = negamax(pos, d, alpha, beta, tt, stop, /*ply=*/0);
        }
        prevScore = score;

        TTEntry tte;
        if (tt.probe(pos.key(), tte) && tte.move != 0) bestMove = tte.move;

        auto now = std::chrono::steady_clock::now();
        int64_t elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();

        // Provide info callback if requested (PV reconstruction basic: just best move at root)
        if (infoCb) {
            std::vector<movegen::Move> pv;
            pv.push_back(bestMove);
            infoCb(d, score, g_nodes, elapsedMs, pv);
        }

        // Stop conditions
        if (stop.load(std::memory_order_relaxed)) break;
        if (!limits.infinite && limits.movetimeMs == 0 && softTimeBudget > 0 && elapsedMs > softTimeBudget) break;
        if (limits.movetimeMs > 0 && elapsedMs >= limits.movetimeMs) break;
        if (limits.maxNodes > 0 && g_nodes >= static_cast<uint64_t>(limits.maxNodes)) break;
    }

    sr.bestMove = bestMove;
    sr.nodes = g_nodes;
    return sr;
}

} // namespace phish::search