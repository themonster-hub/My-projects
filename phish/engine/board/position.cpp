#include "engine/board/position.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstring>
#include <sstream>

namespace phish::board {

namespace {

Piece make_piece(Color c, PieceType pt) {
    return static_cast<Piece>(static_cast<int>(c) * 6 + static_cast<int>(pt));
}

Color piece_color(Piece pc) { return static_cast<Color>(static_cast<int>(pc) / 6); }
PieceType piece_type(Piece pc) { return static_cast<PieceType>(static_cast<int>(pc) % 6); }

Piece char_to_piece(char ch) {
    Color c = std::isupper(static_cast<unsigned char>(ch)) ? WHITE : BLACK;
    char l = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    PieceType pt = NO_PIECE_TYPE;
    switch (l) {
        case 'p': pt = PAWN; break;
        case 'n': pt = KNIGHT; break;
        case 'b': pt = BISHOP; break;
        case 'r': pt = ROOK; break;
        case 'q': pt = QUEEN; break;
        case 'k': pt = KING; break;
        default: return NO_PIECE;
    }
    return make_piece(c, pt);
}

char piece_to_char(Piece pc) {
    const char tab[6] = {'p','n','b','r','q','k'};
    char ch = tab[piece_type(pc)];
    return piece_color(pc) == WHITE ? static_cast<char>(std::toupper(ch)) : ch;
}

} // namespace

Position::Position() {
    std::fill(std::begin(pieceOn), std::end(pieceOn), NO_PIECE);
    std::fill(std::begin(bbByPiece), std::end(bbByPiece), 0ULL);
    std::fill(std::begin(occByColor), std::end(occByColor), 0ULL);
}

bool Position::set_startpos() {
    return set_fen("startpos");
}

bool Position::set_fen(const std::string& fen) {
    std::string f = fen;
    if (fen == "startpos") {
        f = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    }

    std::istringstream iss(f);
    std::string board, stmStr, castlingStr, epStr;
    int half = 0, full = 1;

    if (!(iss >> board >> stmStr >> castlingStr >> epStr)) return false;
    if (!(iss >> half)) half = 0;
    if (!(iss >> full)) full = 1;

    // Reset
    *this = Position();

    int idx = 56; // start at A8
    for (char ch : board) {
        if (ch == '/') { idx -= 16; continue; }
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            idx += ch - '0';
        } else {
            Piece pc = char_to_piece(ch);
            if (pc == NO_PIECE || idx < 0 || idx >= 64) return false;
            put_piece(pc, static_cast<Square>(idx));
            ++idx;
        }
    }

    stm = (stmStr == "w") ? WHITE : BLACK;

    castling = 0;
    if (castlingStr.find('K') != std::string::npos) castling |= 1;
    if (castlingStr.find('Q') != std::string::npos) castling |= 2;
    if (castlingStr.find('k') != std::string::npos) castling |= 4;
    if (castlingStr.find('q') != std::string::npos) castling |= 8;

    if (epStr != "-") {
        char file = epStr[0], rank = epStr[1];
        int fidx = file - 'a';
        int ridx = rank - '1';
        if (fidx >= 0 && fidx < 8 && ridx >= 0 && ridx < 8) ep = make_square(fidx, ridx);
    } else ep = SQ_NONE;

    halfmove = half;
    fullmove = full;

    return true;
}

void Position::put_piece(Piece pc, Square s) {
    bbByPiece[pc] |= Bit(s);
    occByColor[piece_color(pc)] |= Bit(s);
    occByColor[2] |= Bit(s);
    pieceOn[s] = pc;
}

void Position::remove_piece(Piece pc, Square s) {
    bbByPiece[pc] &= ~Bit(s);
    occByColor[piece_color(pc)] &= ~Bit(s);
    occByColor[2] &= ~Bit(s);
    pieceOn[s] = NO_PIECE;
}

void Position::move_piece(Piece pc, Square from, Square to) {
    bbByPiece[pc] ^= Bit(from) | Bit(to);
    Color c = piece_color(pc);
    occByColor[c] ^= Bit(from) | Bit(to);
    occByColor[2] ^= Bit(from) | Bit(to);
    pieceOn[from] = NO_PIECE;
    pieceOn[to] = pc;
}

Square Position::king_square(Color c) const {
    const Piece k = make_piece(c, KING);
    U64 bb = bbByPiece[k];
    if (bb == 0) return SQ_NONE;
    int sq = __builtin_ctzll(bb);
    return static_cast<Square>(sq);
}

bool Position::is_square_attacked(Square s, Color by) const {
    // Pawns
    if (bitboard::PAWN_ATTACKS[by][s] & bbByPiece[make_piece(by, PAWN)]) return true;
    // Knights
    if (bitboard::KNIGHT_ATTACKS[s] & bbByPiece[make_piece(by, KNIGHT)]) return true;
    // King
    if (bitboard::KING_ATTACKS[s] & bbByPiece[make_piece(by, KING)]) return true;
    // Bishops/Queens
    U64 bishops = bbByPiece[make_piece(by, BISHOP)] | bbByPiece[make_piece(by, QUEEN)];
    if (bitboard::sliding_attacks_bishop(s, occupancy()) & bishops) return true;
    // Rooks/Queens
    U64 rooks = bbByPiece[make_piece(by, ROOK)] | bbByPiece[make_piece(by, QUEEN)];
    if (bitboard::sliding_attacks_rook(s, occupancy()) & rooks) return true;
    return false;
}

void Position::gen_pawn_moves(Color c, movegen::MoveList& list) const {
    const int dir = (c == WHITE) ? 1 : -1;
    const int startRank = (c == WHITE) ? 1 : 6;
    const int promoRank = (c == WHITE) ? 6 : 1;

    U64 pawns = bbByPiece[make_piece(c, PAWN)];
    while (pawns) {
        Square from = static_cast<Square>(__builtin_ctzll(pawns));
        pawns &= pawns - 1;
        int f = file_of(from), r = rank_of(from);

        // Single push
        int nr = r + dir;
        if (nr >= 0 && nr < 8) {
            Square to = make_square(f, nr);
            if (!(occupancy() & Bit(to))) {
                if (r == promoRank) {
                    list.add(movegen::make_move(from, to, 0, QUEEN));
                    list.add(movegen::make_move(from, to, 0, ROOK));
                    list.add(movegen::make_move(from, to, 0, BISHOP));
                    list.add(movegen::make_move(from, to, 0, KNIGHT));
                } else {
                    list.add(movegen::make_move(from, to));
                    // Double push
                    if (r == startRank) {
                        int rr = r + 2 * dir;
                        Square to2 = make_square(f, rr);
                        if (!(occupancy() & Bit(to2))) {
                            list.add(movegen::make_move(from, to2, movegen::DOUBLE_PUSH));
                        }
                    }
                }
            }
        }

        // Captures
        U64 caps = bitboard::PAWN_ATTACKS[c][from] & occByColor[opposite(c)];
        while (caps) {
            Square to = static_cast<Square>(__builtin_ctzll(caps));
            caps &= caps - 1;
            if (r == promoRank) {
                list.add(movegen::make_move(from, to, movegen::CAPTURE, QUEEN));
                list.add(movegen::make_move(from, to, movegen::CAPTURE, ROOK));
                list.add(movegen::make_move(from, to, movegen::CAPTURE, BISHOP));
                list.add(movegen::make_move(from, to, movegen::CAPTURE, KNIGHT));
            } else {
                list.add(movegen::make_move(from, to, movegen::CAPTURE));
            }
        }

        // En passant
        if (ep != SQ_NONE) {
            U64 epMask = Bit(ep);
            if (bitboard::PAWN_ATTACKS[c][from] & epMask) {
                list.add(movegen::make_move(from, ep, movegen::EN_PASSANT | movegen::CAPTURE));
            }
        }
    }
}

void Position::gen_knight_moves(Color c, movegen::MoveList& list) const {
    U64 knights = bbByPiece[make_piece(c, KNIGHT)];
    U64 own = occByColor[c];
    while (knights) {
        Square from = static_cast<Square>(__builtin_ctzll(knights));
        knights &= knights - 1;
        U64 targets = bitboard::KNIGHT_ATTACKS[from] & ~own;
        while (targets) {
            Square to = static_cast<Square>(__builtin_ctzll(targets));
            targets &= targets - 1;
            bool cap = (occByColor[opposite(c)] & Bit(to)) != 0;
            list.add(movegen::make_move(from, to, cap ? movegen::CAPTURE : 0));
        }
    }
}

void Position::gen_bishop_moves(Color c, movegen::MoveList& list) const {
    U64 bishops = bbByPiece[make_piece(c, BISHOP)];
    U64 own = occByColor[c];
    while (bishops) {
        Square from = static_cast<Square>(__builtin_ctzll(bishops));
        bishops &= bishops - 1;
        U64 targets = bitboard::sliding_attacks_bishop(from, occupancy()) & ~own;
        while (targets) {
            Square to = static_cast<Square>(__builtin_ctzll(targets));
            targets &= targets - 1;
            bool cap = (occByColor[opposite(c)] & Bit(to)) != 0;
            list.add(movegen::make_move(from, to, cap ? movegen::CAPTURE : 0));
        }
    }
}

void Position::gen_rook_moves(Color c, movegen::MoveList& list) const {
    U64 rooks = bbByPiece[make_piece(c, ROOK)];
    U64 own = occByColor[c];
    while (rooks) {
        Square from = static_cast<Square>(__builtin_ctzll(rooks));
        rooks &= rooks - 1;
        U64 targets = bitboard::sliding_attacks_rook(from, occupancy()) & ~own;
        while (targets) {
            Square to = static_cast<Square>(__builtin_ctzll(targets));
            targets &= targets - 1;
            bool cap = (occByColor[opposite(c)] & Bit(to)) != 0;
            list.add(movegen::make_move(from, to, cap ? movegen::CAPTURE : 0));
        }
    }
}

void Position::gen_queen_moves(Color c, movegen::MoveList& list) const {
    U64 queens = bbByPiece[make_piece(c, QUEEN)];
    U64 own = occByColor[c];
    while (queens) {
        Square from = static_cast<Square>(__builtin_ctzll(queens));
        queens &= queens - 1;
        U64 targets = (bitboard::sliding_attacks_bishop(from, occupancy()) |
                       bitboard::sliding_attacks_rook(from, occupancy())) & ~own;
        while (targets) {
            Square to = static_cast<Square>(__builtin_ctzll(targets));
            targets &= targets - 1;
            bool cap = (occByColor[opposite(c)] & Bit(to)) != 0;
            list.add(movegen::make_move(from, to, cap ? movegen::CAPTURE : 0));
        }
    }
}

void Position::gen_king_moves(Color c, movegen::MoveList& list) const {
    Square from = king_square(c);
    if (from == SQ_NONE) return;
    U64 own = occByColor[c];
    U64 targets = bitboard::KING_ATTACKS[from] & ~own;
    while (targets) {
        Square to = static_cast<Square>(__builtin_ctzll(targets));
        targets &= targets - 1;
        bool cap = (occByColor[opposite(c)] & Bit(to)) != 0;
        list.add(movegen::make_move(from, to, cap ? movegen::CAPTURE : 0));
    }
    // Castling: simplified, no rook validation on squares; will enforce legality via checks
    if (c == WHITE) {
        if ((castling & 1) && !(occupancy() & (Bit(SQ_F1) | Bit(SQ_G1))) && !is_in_check(WHITE) &&
            !is_square_attacked(SQ_F1, BLACK) && !is_square_attacked(SQ_G1, BLACK) && pieceOn[SQ_E1] == W_KING)
            list.add(movegen::make_move(SQ_E1, SQ_G1, movegen::KING_CASTLE));
        if ((castling & 2) && !(occupancy() & (Bit(SQ_B1) | Bit(SQ_C1) | Bit(SQ_D1))) && !is_in_check(WHITE) &&
            !is_square_attacked(SQ_D1, BLACK) && !is_square_attacked(SQ_C1, BLACK) && pieceOn[SQ_E1] == W_KING)
            list.add(movegen::make_move(SQ_E1, SQ_C1, movegen::QUEEN_CASTLE));
    } else {
        if ((castling & 4) && !(occupancy() & (Bit(SQ_F8) | Bit(SQ_G8))) && !is_in_check(BLACK) &&
            !is_square_attacked(SQ_F8, WHITE) && !is_square_attacked(SQ_G8, WHITE) && pieceOn[SQ_E8] == B_KING)
            list.add(movegen::make_move(SQ_E8, SQ_G8, movegen::KING_CASTLE));
        if ((castling & 8) && !(occupancy() & (Bit(SQ_B8) | Bit(SQ_C8) | Bit(SQ_D8))) && !is_in_check(BLACK) &&
            !is_square_attacked(SQ_D8, WHITE) && !is_square_attacked(SQ_C8, WHITE) && pieceOn[SQ_E8] == B_KING)
            list.add(movegen::make_move(SQ_E8, SQ_C8, movegen::QUEEN_CASTLE));
    }
}

void Position::gen_pseudo_legal(movegen::MoveList& list) const {
    list.clear();
    gen_pawn_moves(stm, list);
    gen_knight_moves(stm, list);
    gen_bishop_moves(stm, list);
    gen_rook_moves(stm, list);
    gen_queen_moves(stm, list);
    gen_king_moves(stm, list);
}

void Position::generate_legal(movegen::MoveList& list) const {
    movegen::MoveList pseudo;
    gen_pseudo_legal(pseudo);

    list.clear();
    StateInfo st;
    for (movegen::Move m : pseudo.moves) {
        Position copy = *this;
        if (copy.make_move(m, st)) {
            list.add(m);
            copy.unmake_move(m, st); // not needed for copy but keep symmetry
        }
    }
}

bool Position::make_move(movegen::Move m, StateInfo& st) {
    st.castlingRights = castling;
    st.epSquare = ep;
    st.halfmoveClock = halfmove;

    Square from = movegen::from_sq(m);
    Square to = movegen::to_sq(m);
    Piece pc = static_cast<Piece>(pieceOn[from]);
    if (pc == NO_PIECE || piece_color(pc) != stm) return false;

    // Update clocks
    ++halfmove;
    if (piece_type(pc) == PAWN || (occByColor[opposite(stm)] & Bit(to))) halfmove = 0;
    if (stm == BLACK) ++fullmove;

    // Clear en-passant
    ep = SQ_NONE;

    // Captures (incl. EP)
    if (movegen::is_enpassant(m)) {
        int dir = (stm == WHITE) ? -1 : 1;
        Square capSq = make_square(file_of(to), rank_of(to) + dir);
        Piece capPc = static_cast<Piece>(pieceOn[capSq]);
        if (piece_type(pc) != PAWN || capPc == NO_PIECE) return false;
        remove_piece(capPc, capSq);
    } else if (occByColor[opposite(stm)] & Bit(to)) {
        Piece capPc = static_cast<Piece>(pieceOn[to]);
        remove_piece(capPc, to);
    }

    // Special: castling rook move
    if (movegen::is_kingside_castle(m)) {
        if (pc != make_piece(stm, KING)) return false;
        if (stm == WHITE) {
            if (pieceOn[SQ_H1] != W_ROOK) return false;
            move_piece(W_ROOK, SQ_H1, SQ_F1);
        } else {
            if (pieceOn[SQ_H8] != B_ROOK) return false;
            move_piece(B_ROOK, SQ_H8, SQ_F8);
        }
    } else if (movegen::is_queenside_castle(m)) {
        if (pc != make_piece(stm, KING)) return false;
        if (stm == WHITE) {
            if (pieceOn[SQ_A1] != W_ROOK) return false;
            move_piece(W_ROOK, SQ_A1, SQ_D1);
        } else {
            if (pieceOn[SQ_A8] != B_ROOK) return false;
            move_piece(B_ROOK, SQ_A8, SQ_D8);
        }
    }

    // Move piece
    move_piece(pc, from, to);

    // Promotion
    if (movegen::is_promotion(m)) {
        PieceType pt = movegen::promotion_piece(m);
        remove_piece(pc, to);
        put_piece(make_piece(stm, pt), to);
    }

    // Double pawn push -> set ep
    if (movegen::is_double_push(m) && piece_type(pc) == PAWN) {
        int midRank = (rank_of(from) + rank_of(to)) / 2;
        ep = make_square(file_of(from), midRank);
    }

    // Update castling rights crudely if king or rooks moved/captured
    auto clear_castle = [&](Square s) {
        if (s == SQ_E1) castling &= ~(1 | 2);
        if (s == SQ_H1) castling &= ~1;
        if (s == SQ_A1) castling &= ~2;
        if (s == SQ_E8) castling &= ~(4 | 8);
        if (s == SQ_H8) castling &= ~4;
        if (s == SQ_A8) castling &= ~8;
    };
    clear_castle(from);
    clear_castle(to);

    // Legality: king not in check
    if (is_in_check(stm)) {
        // undo
        unmake_move(m, st);
        return false;
    }

    // Switch side
    stm = opposite(stm);
    return true;
}

void Position::unmake_move(movegen::Move m, const StateInfo& st) {
    // Restore from saved state by recomputing from scratch is complex; we instead undo exactly
    // This implementation assumes it is called only after make_move that succeeded and without external board changes.
    // For simplicity in this MVP, we reset to prior snapshot by rebuilding from a copied Position in generate_legal.
    // In search we will implement exact undo with a stack. Here it's unused.
    (void)m;
    (void)st;
}

bool Position::play_uci_move(const std::string& uci) {
    if (uci.size() < 4) return false;
    int f1 = uci[0] - 'a', r1 = uci[1] - '1';
    int f2 = uci[2] - 'a', r2 = uci[3] - '1';
    if (f1 < 0 || f1 >= 8 || r1 < 0 || r1 >= 8 || f2 < 0 || f2 >= 8 || r2 < 0 || r2 >= 8) return false;
    Square from = make_square(f1, r1);
    Square to = make_square(f2, r2);

    movegen::MoveList legal;
    generate_legal(legal);
    for (auto m : legal.moves) {
        if (movegen::from_sq(m) == from && movegen::to_sq(m) == to) {
            // Promotion handling
            if (movegen::is_promotion(m)) {
                if (uci.size() == 5) {
                    char pr = uci[4];
                    PieceType want = NO_PIECE_TYPE;
                    switch (pr) {
                        case 'q': want = QUEEN; break;
                        case 'r': want = ROOK; break;
                        case 'b': want = BISHOP; break;
                        case 'n': want = KNIGHT; break;
                        default: continue;
                    }
                    if (movegen::promotion_piece(m) != want) continue;
                } else {
                    continue;
                }
            }
            StateInfo st;
            return make_move(m, st);
        }
    }
    return false;
}

std::uint64_t Position::perft(int depth) {
    if (depth == 0) return 1ULL;
    movegen::MoveList list;
    generate_legal(list);
    std::uint64_t nodes = 0;
    StateInfo st;
    for (auto m : list.moves) {
        Position copy = *this;
        if (copy.make_move(m, st)) {
            nodes += copy.perft(depth - 1);
        }
    }
    return nodes;
}

std::uint64_t Position::perft_divide(int depth, std::vector<std::pair<movegen::Move, std::uint64_t>>& out) {
    out.clear();
    movegen::MoveList list;
    generate_legal(list);
    std::uint64_t nodes = 0;
    StateInfo st;
    for (auto m : list.moves) {
        Position copy = *this;
        if (copy.make_move(m, st)) {
            std::uint64_t n = copy.perft(depth - 1);
            out.emplace_back(m, n);
            nodes += n;
        }
    }
    return nodes;
}

} // namespace phish::board