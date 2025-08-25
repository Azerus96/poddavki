#pragma once
#include <cstdint>
#include <vector>

namespace kestog_core {
    // Используем 64-битные целые для битбордов
    using u64 = uint64_t;

    // Структура для представления доски
    struct Bitboard {
        u64 white_men;
        u64 black_men;
        u64 kings;
    };

    // Структура для представления хода
    struct Move {
        u64 mask_from;
        u64 mask_to;
        u64 captured_pieces;
        bool becomes_king;
    };

    // Функции, экспортируемые из ядра
    std::vector<Move> generate_captures(const Bitboard& board, int color_to_move);
    std::vector<Move> generate_quiet_moves(const Bitboard& board, int color_to_move);
    Bitboard apply_move(const Bitboard& board, const Move& move, int color_to_move);
    int evaluate_giveaway(const Bitboard& board, int color_to_move);
}
