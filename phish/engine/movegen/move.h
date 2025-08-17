#pragma once

#include <cstdint>
#include <vector>

#include "engine/util/types.h"

namespace phish::movegen {

using Move = std::uint32_t;

enum MoveFlags : std::uint32_t {
    QUIET = 0,
    CAPTURE = 1u << 14,
    DOUBLE_PUSH = 1u << 15,
    EN_PASSANT = 1u << 16,
    KING_CASTLE = 1u << 17,
    QUEEN_CASTLE = 1u << 18,
    PROMOTION = 1u << 19
};

inline Move make_move(Square from, Square to, std::uint32_t flags = 0, PieceType promo = NO_PIECE_TYPE) {
    // bits: 0-5 from, 6-11 to, 12-14 promo(3 bits), 14.. flags (overlap ok since PROMO guard)
    std::uint32_t m = 0;
    m |= static_cast<std::uint32_t>(from);
    m |= static_cast<std::uint32_t>(to) << 6;
    if (promo != NO_PIECE_TYPE) {
        m |= (static_cast<std::uint32_t>(promo) & 0x7) << 12;
        m |= PROMOTION;
    }
    m |= flags;
    return m;
}

inline Square from_sq(Move m) { return static_cast<Square>(m & 0x3F); }
inline Square to_sq(Move m) { return static_cast<Square>((m >> 6) & 0x3F); }
inline bool is_capture(Move m) { return (m & CAPTURE) != 0; }
inline bool is_enpassant(Move m) { return (m & EN_PASSANT) != 0; }
inline bool is_double_push(Move m) { return (m & DOUBLE_PUSH) != 0; }
inline bool is_kingside_castle(Move m) { return (m & KING_CASTLE) != 0; }
inline bool is_queenside_castle(Move m) { return (m & QUEEN_CASTLE) != 0; }
inline bool is_promotion(Move m) { return (m & PROMOTION) != 0; }
inline PieceType promotion_piece(Move m) { return static_cast<PieceType>((m >> 12) & 0x7); }

struct MoveList {
    std::vector<Move> moves;
    void clear() { moves.clear(); }
    void add(Move m) { moves.push_back(m); }
    std::size_t size() const { return moves.size(); }
    const Move* begin() const { return moves.data(); }
    const Move* end() const { return moves.data() + moves.size(); }
};

} // namespace phish::movegen