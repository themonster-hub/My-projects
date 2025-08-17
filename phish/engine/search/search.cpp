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

static inline PieceType type_of_piece(Piece p) { return static_cast<PieceType>(static_cast<int>(p) % 6); }

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
static movegen::Move g_countermove[12][64]{}; // prev moved piece + prev to square -> good reply

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
        // Delta pruning: if even capturing biggest plausible gain cannot reach alpha, skip
        Square to = movegen::to_sq(m);
        Piece capPc = static_cast<Piece>(pos.piece_at(to));
        PieceType capType = movegen::is_enpassant(m) ? PAWN : (capPc == NO_PIECE ? NO_PIECE_TYPE : type_of_piece(capPc));
        if (stand + piece_value(capType) + 100 < alpha) continue;

        if (!pos.make_move(m, st)) continue;
        int score = -qsearch(pos, -beta, -alpha, stop);
        pos.unmake_move(m, st);
        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
        if (stop.load(std::memory_order_relaxed)) return alpha;
    }
    return alpha;
}

static inline int mvv_lva(const board::Position& pos, movegen::Move m) {
    if (!movegen::is_capture(m)) return 0;
    Piece attacker = static_cast<Piece>(pos.piece_at(movegen::from_sq(m)));
    Piece victim = static_cast<Piece>(pos.piece_at(movegen::to_sq(m)));
    PieceType a = attacker == NO_PIECE ? NO_PIECE_TYPE : type_of_piece(attacker);
    PieceType v = movegen::is_enpassant(m) ? PAWN : (victim == NO_PIECE ? NO_PIECE_TYPE : type_of_piece(victim));
    return piece_value(v) * 16 - piece_value(a);
}

static int score_move(const board::Position& pos, movegen::Move m, movegen::Move ttMove, movegen::Move killer1, movegen::Move killer2, Piece movedPiece, Piece prevMovedPiece, Square prevTo) {
    if (m == ttMove) return 2'000'000;
    if (prevMovedPiece != NO_PIECE && m == g_countermove[prevMovedPiece][prevTo]) return 1'500'000;
    if (m == killer1) return 900'000;
    if (m == killer2) return 800'000;
    int base = 0;
    if (movegen::is_capture(m)) base += 200'000 + mvv_lva(pos, m);
    base += g_history[movedPiece][movegen::to_sq(m)];
    if (movegen::is_promotion(m)) base += 50'000 + piece_value(movegen::promotion_piece(m));
    return base;
}

static int negamax(board::Position& pos, int depth, int alpha, int beta, TranspositionTable& tt, std::atomic<bool>& stop, int ply, movegen::Move prevMove, Piece prevMovedPiece, Square prevTo, int parentStaticEval) {
    if (stop.load(std::memory_order_relaxed)) return alpha;

    const bool pvNode = (alpha + 1 < beta);

    // Stand pat / static eval
    const bool inCheck = pos.in_check();
    const int staticEval = evaluate(pos);
    const bool improving = (staticEval >= parentStaticEval - 30);

    if (depth == 0) return qsearch(pos, alpha, beta, stop);

    // Razoring near leaf
    if (!inCheck && depth <= 2) {
        int razorMargin = 125 * depth;
        if (staticEval + razorMargin <= alpha) {
            int qs = qsearch(pos, alpha, beta, stop);
            if (qs <= alpha) return qs;
        }
    }

    TTEntry tte{};
    movegen::Move ttMove = 0;
    if (tt.probe(pos.key(), tte) && tte.depth >= depth) {
        if (tte.flag == 0) return tte.score;
        if (tte.flag == 1 && tte.score <= alpha) return alpha;
        if (tte.flag == 2 && tte.score >= beta) return beta;
        ttMove = tte.move;
    }

    // Static null-move pruning
    if (!inCheck && depth <= 3 && staticEval - 120 * depth >= beta) {
        return staticEval;
    }

    // Null-move pruning
    if (depth >= 3 && !inCheck) {
        board::StateInfo st;
        if (pos.make_null_move(st)) {
            int R = 2 + (depth >= 5 ? 1 : 0);
            int score = -negamax(pos, depth - 1 - R, -beta, -beta + 1, tt, stop, ply + 1, 0, NO_PIECE, SQ_NONE, staticEval);
            pos.unmake_null_move(st);
            if (score >= beta) return beta;
        }
    }

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
        return score_move(pos, a, ttMove, killer1, killer2, movedA, prevMovedPiece, prevTo) > score_move(pos, b, ttMove, killer1, killer2, movedB, prevMovedPiece, prevTo);
    });

    int bestScore = std::numeric_limits<int>::min() / 2;
    movegen::Move bestMove = 0;
    int alphaOrig = alpha;

    int moveIndex = 0;
    for (auto m : moves.moves) {
        ++g_nodes;

        const bool isCapOrPromo = movegen::is_capture(m) || movegen::is_promotion(m);

        // Late move pruning for quiet moves at low depth
        if (!pvNode && !inCheck && !isCapOrPromo && depth <= 2) {
            int lmpLimit = (depth == 1 ? 4 : 6);
            if (moveIndex >= lmpLimit) { ++moveIndex; continue; }
        }

        if (!pos.make_move(m, st)) { ++moveIndex; continue; }

        // PVS with improving-aware LMR for quiet moves
        int score;
        int newDepth = depth - 1;
        Piece moved = static_cast<Piece>(pos.piece_at(movegen::from_sq(m))); // from square before move was made; but we need moved piece for history; capture before make_move removed; we already moved; so recompute from st? Instead capture moved using previous line before make_move.
        // Note: 'moved' variable above should have been captured before make_move; adjust below accordingly.

        // Capture actual moved piece before making move
        // However we already made the move. We retrieve it from the destination square by unmaking is complex; so grab before making move in temp variable.

        // Undo and redo to capture moved piece cleanly is too expensive; instead compute 'moved' earlier

        pos.unmake_move(m, st); // revert move to recover state
        moved = static_cast<Piece>(pos.piece_at(movegen::from_sq(m)));
        if (!pos.make_move(m, st)) { ++moveIndex; continue; }

        if (bestMove == 0) {
            score = -negamax(pos, newDepth, -beta, -alpha, tt, stop, ply + 1, m, moved, movegen::to_sq(m), staticEval);
        } else {
            int reduction = 0;
            if (!inCheck && !isCapOrPromo && depth >= 3) {
                reduction = 1 + ((depth >= 5 && moveIndex >= 5) ? 1 : 0);
                if (!improving) reduction += 1;
                if (newDepth - reduction < 0) reduction = newDepth > 0 ? newDepth - 1 : 0;
            }
            score = -negamax(pos, newDepth - reduction, -alpha - 1, -alpha, tt, stop, ply + 1, m, moved, movegen::to_sq(m), staticEval);
            if (score > alpha) {
                // Re-search at full depth if it improved
                score = -negamax(pos, newDepth, -alpha - 1, -alpha, tt, stop, ply + 1, m, moved, movegen::to_sq(m), staticEval);
                if (score > alpha && score < beta) {
                    score = -negamax(pos, newDepth, -beta, -alpha, tt, stop, ply + 1, m, moved, movegen::to_sq(m), staticEval);
                }
            }
        }
        pos.unmake_move(m, st);

        if (score > bestScore) {
            bestScore = score;
            bestMove = m;
        }
        if (bestScore > alpha) {
            // Update history and countermove for quiet moves that improve alpha
            if (!movegen::is_capture(m)) {
                g_history[moved][movegen::to_sq(m)] += depth * depth;
                if (prevMovedPiece != NO_PIECE && prevTo != SQ_NONE) {
                    g_countermove[prevMovedPiece][prevTo] = m;
                }
                // Update killers
                if (m != killer1) {
                    g_killer[ply][1] = g_killer[ply][0];
                    g_killer[ply][0] = m;
                }
            }
            alpha = bestScore;
        }
        if (alpha >= beta) {
            // Beta cutoff: update history/killers/countermove for quiet move
            if (!movegen::is_capture(m)) {
                g_history[moved][movegen::to_sq(m)] += depth * depth;
                if (prevMovedPiece != NO_PIECE && prevTo != SQ_NONE) {
                    g_countermove[prevMovedPiece][prevTo] = m;
                }
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
    std::memset(g_countermove, 0, sizeof(g_countermove));

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

        int parentEval = evaluate(pos);
        int score = negamax(pos, d, alpha, beta, tt, stop, /*ply=*/0, /*prev*/0, /*prevMovedPiece*/NO_PIECE, /*prevTo*/SQ_NONE, parentEval);
        // If fail-low/high on aspiration, re-search with full window
        if (score <= alpha || score >= beta) {
            alpha = -30000; beta = 30000;
            score = negamax(pos, d, alpha, beta, tt, stop, /*ply=*/0, /*prev*/0, /*prevMovedPiece*/NO_PIECE, /*prevTo*/SQ_NONE, parentEval);
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