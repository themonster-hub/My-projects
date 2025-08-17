#pragma once

#include <cstdint>
#include <vector>

#include "engine/util/types.h"
#include "engine/movegen/move.h"
#include "engine/board/position.h"

namespace phish::search {

struct TTEntry {
    U64 key;
    int16_t score;
    int16_t eval;
    movegen::Move move;
    uint16_t depth;
    uint8_t flag; // 0=exact,1=alpha,2=beta
    uint8_t age;
};

class TranspositionTable {
public:
    explicit TranspositionTable(std::size_t mb);
    ~TranspositionTable();

    void resize(std::size_t mb);
    void clear();

    void store(U64 key, int depth, int score, int eval, uint8_t flag, movegen::Move move);
    bool probe(U64 key, TTEntry& out) const;

private:
    TTEntry* table = nullptr;
    std::size_t numEntries = 0;
    uint8_t currentAge = 0;
};

struct Limits {
    int depth = 1;
    int64_t timeMs = 0;
    int64_t incMs = 0;
    bool infinite = false;
};

struct SearchResult {
    movegen::Move bestMove = 0;
    std::vector<movegen::Move> pv;
    uint64_t nodes = 0;
};

SearchResult think(board::Position& pos, const Limits& limits, TranspositionTable& tt);

} // namespace phish::search