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

static int evaluate(const board::Position& pos) {
    // Very basic material eval from side to move perspective
    int score = 0;
    for (int s = 0; s < 64; ++s) {
        int pc = pos.key(); // dummy to silence unused if needed
        (void)pc;
    }
    // Iterate bitboards for speed
    for (int c = 0; c < COLOR_NB; ++c) {
        for (int pt = PAWN; pt <= QUEEN; ++pt) {
            Piece p = static_cast<Piece>(c * 6 + pt);
            // We don't have getters; reconstruct from attack arrays is not ideal.
            // Minimal: probe occupancy by scanning pieceOn is private. Instead skip detailed eval for MVP.
        }
    }
    // Placeholder 0 eval
    return score;
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

static int qsearch(board::Position& pos, int alpha, int beta) {
    // Placeholder: no captures search, just stand pat
    int stand = evaluate(pos);
    if (stand >= beta) return beta;
    if (stand > alpha) alpha = stand;
    return alpha;
}

static int negamax(board::Position& pos, int depth, int alpha, int beta, TranspositionTable& tt) {
    if (depth == 0) return qsearch(pos, alpha, beta);

    TTEntry tte;
    if (tt.probe(pos.key(), tte) && tte.depth >= depth) {
        if (tte.flag == 0) return tte.score;
        if (tte.flag == 1 && tte.score <= alpha) return alpha;
        if (tte.flag == 2 && tte.score >= beta) return beta;
    }

    board::StateInfo st;
    movegen::MoveList moves;
    pos.generate_legal(moves);
    if (moves.size() == 0) {
        // Checkmate or stalemate
        if (false) return 0; // TODO: detect check
        return 0;
    }

    int bestScore = std::numeric_limits<int>::min() / 2;
    movegen::Move bestMove = 0;

    for (auto m : moves.moves) {
        if (!pos.make_move(m, st)) continue;
        int score = -negamax(pos, depth - 1, -beta, -alpha, tt);
        pos.unmake_move(m, st);
        if (score > bestScore) {
            bestScore = score;
            bestMove = m;
        }
        if (bestScore > alpha) alpha = bestScore;
        if (alpha >= beta) break;
    }

    uint8_t flag = 0;
    if (bestScore <= alpha) flag = 1; // alpha bound
    else if (bestScore >= beta) flag = 2; // beta bound
    tt.store(pos.key(), depth, bestScore, 0, flag, bestMove);
    return bestScore;
}

SearchResult think(board::Position& pos, const Limits& limits, TranspositionTable& tt) {
    SearchResult sr;
    movegen::MoveList legal;
    pos.generate_legal(legal);
    if (legal.size() == 0) { sr.bestMove = 0; return sr; }

    movegen::Move bestMove = legal.moves.front();
    int alpha = -100000, beta = 100000;
    for (int d = 1; d <= limits.depth; ++d) {
        int score = negamax(pos, d, alpha, beta, tt);
        (void)score;
        // We don't reconstruct PV here in MVP; pick TT move when available
        search::TTEntry tte;
        if (tt.probe(pos.key(), tte) && tte.move != 0) bestMove = tte.move;
    }

    sr.bestMove = bestMove;
    return sr;
}

} // namespace phish::search