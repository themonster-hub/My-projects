#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "engine/bitboard/bitboard.h"
#include "engine/util/zobrist.h"
#include "engine/board/position.h"

int main(int argc, char** argv) {
    using namespace phish;
    bitboard::init();
    zobrist::init();

    std::string file = argc > 1 ? argv[1] : "tests/perft/perft_positions.txt";
    std::ifstream in(file);
    if (!in) {
        std::cerr << "Failed to open perft list: " << file << "\n";
        return 1;
    }

    std::string line;
    int failures = 0;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        std::string fenOrStart; std::string depthStr; std::string nodesStr;
        if (!std::getline(iss, fenOrStart, ';')) continue;
        if (!std::getline(iss, depthStr, ';')) continue;
        if (!std::getline(iss, nodesStr, ';')) continue;
        int depth = std::atoi(depthStr.c_str());
        unsigned long long expected = std::strtoull(nodesStr.c_str(), nullptr, 10);

        board::Position pos;
        if (fenOrStart == "startpos") pos.set_fen("startpos");
        else pos.set_fen(fenOrStart);

        auto got = pos.perft(depth);
        std::cout << fenOrStart << ";" << depth << ";" << got << "\n";
        if (got != expected) {
            std::cerr << "Mismatch at depth " << depth << ": got " << got << ", expected " << expected << "\n";
            ++failures;
        }
    }

    return failures == 0 ? 0 : 2;
}