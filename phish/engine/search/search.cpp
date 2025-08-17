#include "engine/search/search.h"

#include <algorithm>
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

static uint64_t g_nodes;

static int qsearch(board::Position& pos, int alpha, int beta) {
    ++g_nodes;
    int stand = evaluate(pos);
    if (stand >= beta) return beta;
    if (stand > alpha) alpha = stand;
    return alpha;
}

static int score_move(movegen::Move m, movegen::Move ttMove) {
    if (m == ttMove) return 1000000;
    return movegen::is_capture(m) ? 10000 : 0;
}

static bool see_winning(const board::Position& pos, movegen::Move m) {
    // Simple heuristic: promotion or capture of a high-value piece considered winning
    if (movegen::is_promotion(m)) return true;
    if (!movegen::is_capture(m)) return false;
    int victim = pos.piece_at(movegen::to_sq(m));
    if (victim == NO_PIECE) return false;
    PieceType vpt = static_cast<PieceType>(victim % 6);
    return piece_value(vpt) >= 300; // capture of minor piece or better
}

static int negamax(board::Position& pos, int depth, int alpha, int beta, TranspositionTable& tt) {
    if (depth == 0) return qsearch(pos, alpha, beta);

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
            int score = -negamax(pos, depth - 1 - R, -beta, -beta + 1, tt);
            pos.unmake_null_move(st);
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
        return score_move(a, ttMove) > score_move(b, ttMove);
    });

    int bestScore = std::numeric_limits<int>::min() / 2;
    movegen::Move bestMove = 0;
    int alphaOrig = alpha;

    int moveIndex = 0;
    for (auto m : moves.moves) {
        ++g_nodes;
        if (!pos.make_move(m, st)) continue;

        int newDepth = depth - 1;
        // Late move reductions for quiet, non-winning moves
        if (!pos.in_check() && !movegen::is_capture(m) && !movegen::is_promotion(m) && moveIndex > 3 && depth >= 3) {
            newDepth -= 1;
        }

        int score;
        if (bestMove == 0) {
            score = -negamax(pos, newDepth, -beta, -alpha, tt);
        } else {
            score = -negamax(pos, newDepth, -alpha - 1, -alpha, tt);
            if (score > alpha && score < beta) {
                score = -negamax(pos, newDepth, -beta, -alpha, tt);
            }
        }
        pos.unmake_move(m, st);
        ++moveIndex;

        if (score > bestScore) {
            bestScore = score;
            bestMove = m;
        }
        if (bestScore > alpha) alpha = bestScore;
        if (alpha >= beta) break;
    }

    uint8_t flag = 0;
    if (bestScore <= alphaOrig) flag = 1;
    else if (bestScore >= beta) flag = 2;
    tt.store(pos.key(), depth, bestScore, 0, flag, bestMove);
    return bestScore;
}

SearchResult think(board::Position& pos, const Limits& limits, TranspositionTable& tt) {
    SearchResult sr;
    movegen::MoveList legal;
    pos.generate_legal(legal);
    if (legal.size() == 0) { sr.bestMove = 0; return sr; }

    g_nodes = 0;
    movegen::Move bestMove = legal.moves.front();
    int alpha = -30000, beta = 30000;
    for (int d = 1; d <= limits.depth; ++d) {
        int score = negamax(pos, d, alpha, beta, tt);
        (void)score;
        TTEntry tte;
        if (tt.probe(pos.key(), tte) && tte.move != 0) bestMove = tte.move;
    }

    sr.bestMove = bestMove;
    sr.nodes = g_nodes;
    return sr;
}

} // namespace phish::search