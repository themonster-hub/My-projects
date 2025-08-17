#pragma once

#include <cstdint>
#include <vector>
#include <atomic>
#include <functional>

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
    int64_t timeMs = 0;   // Remaining time for side to move (if > 0)
    int64_t incMs = 0;    // Increment per move (if any)
    int64_t movetimeMs = 0; // Exact movetime (overrides timeMs/incMs if > 0)
    int64_t maxNodes = 0; // Node limit (0 = unlimited)
    bool infinite = false;
};

struct SearchResult {
    movegen::Move bestMove = 0;
    std::vector<movegen::Move> pv;
    uint64_t nodes = 0;
};

using InfoCallback = std::function<void(int depth, int score, uint64_t nodes, int64_t elapsedMs, const std::vector<movegen::Move>& pv)>;

SearchResult think(board::Position& pos, const Limits& limits, TranspositionTable& tt, std::atomic<bool>& stop, InfoCallback infoCb = nullptr);

} // namespace phish::search