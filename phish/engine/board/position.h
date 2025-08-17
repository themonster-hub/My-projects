#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/util/types.h"
#include "engine/bitboard/bitboard.h"
#include "engine/movegen/move.h"
#include "engine/util/zobrist.h"

namespace phish::board {

struct StateInfo {
    int castlingRights = 0; // bits: 1=K,2=Q,4=k,8=q
    Square epSquare = SQ_NONE;
    int halfmoveClock = 0;
    U64 hash = 0ULL;
    Piece captured = NO_PIECE;
};

class Position {
public:
    Position();

    bool set_fen(const std::string& fen);
    bool set_startpos();

    Color side_to_move() const { return stm; }
    int castling_rights() const { return castling; }
    Square ep_square() const { return ep; }
    U64 key() const { return hash; }

    // Public queries for search/eval
    U64 pieces(Piece pc) const { return bbByPiece[pc]; }
    U64 color_bb(Color c) const { return occByColor[c]; }
    bool in_check() const { return is_in_check(stm); }
    int piece_at(Square s) const { return pieceOn[s]; }

    // Make/unmake move. Returns false if move illegal.
    bool make_move(movegen::Move m, StateInfo& st);
    void unmake_move(movegen::Move m, const StateInfo& st);

    // Null move for search pruning
    bool make_null_move(StateInfo& st);
    void unmake_null_move(const StateInfo& st);

    // Generate legal moves into list
    void generate_legal(movegen::MoveList& list) const;

    // Apply UCI move text (e2e4, e7e8q) to the position; returns false on failure
    bool play_uci_move(const std::string& uci);

    // Perft utility
    std::uint64_t perft(int depth);
    std::uint64_t perft_divide(int depth, std::vector<std::pair<movegen::Move, std::uint64_t>>& out);

private:
    // Piece data
    U64 bbByPiece[12]{};
    U64 occByColor[3]{}; // [WHITE], [BLACK], [2]=both
    int pieceOn[64]{};   // Piece enum or NO_PIECE

    Color stm = WHITE;
    int castling = 0; // 1=K,2=Q,4=k,8=q
    Square ep = SQ_NONE;
    int halfmove = 0;
    int fullmove = 1;
    U64 hash = 0ULL;

    // Helpers
    U64 occupancy() const { return occByColor[2]; }

    bool is_square_attacked(Square s, Color by) const;
    Square king_square(Color c) const;

    void put_piece(Piece pc, Square s);
    void remove_piece(Piece pc, Square s);
    void move_piece(Piece pc, Square from, Square to);

    void gen_pseudo_legal(movegen::MoveList& list) const;
    void gen_pawn_moves(Color c, movegen::MoveList& list) const;
    void gen_knight_moves(Color c, movegen::MoveList& list) const;
    void gen_bishop_moves(Color c, movegen::MoveList& list) const;
    void gen_rook_moves(Color c, movegen::MoveList& list) const;
    void gen_queen_moves(Color c, movegen::MoveList& list) const;
    void gen_king_moves(Color c, movegen::MoveList& list) const;

    bool is_in_check(Color c) const { return is_square_attacked(king_square(c), opposite(c)); }
};

} // namespace phish::board