#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "KestoG_Core.hpp"

namespace py = pybind11;

PYBIND11_MODULE(kestog_core, m) {
    m.doc() = "High-performance giveaway checkers core module v2.0 with advanced search";

    py::class_<kestog_core::Bitboard>(m, "Bitboard")
        .def(py::init<>())
        .def_readwrite("white_men", &kestog_core::Bitboard::white_men)
        .def_readwrite("black_men", &kestog_core::Bitboard::black_men)
        .def_readwrite("kings", &kestog_core::Bitboard::kings)
        .def_readwrite("hash", &kestog_core::Bitboard::hash);

    py::class_<kestog_core::Move>(m, "Move")
        .def(py::init<>())
        .def_readwrite("mask_from", &kestog_core::Move::mask_from)
        .def_readwrite("mask_to", &kestog_core::Move::mask_to)
        .def_readwrite("captured_pieces", &kestog_core::Move::captured_pieces)
        .def_readwrite("becomes_king", &kestog_core::Move::becomes_king);

    py::class_<kestog_core::SearchResult>(m, "SearchResult")
        .def(py::init<>())
        .def_readonly("best_move", &kestog_core::SearchResult::best_move)
        .def_readonly("score", &kestog_core::SearchResult::score)
        .def_readonly("nodes_searched", &kestog_core::SearchResult::nodes_searched)
        .def_readonly("time_taken_ms", &kestog_core::SearchResult::time_taken_ms)
        .def_readonly("final_depth", &kestog_core::SearchResult::final_depth);

    m.def("init_engine", &kestog_core::init_engine, "Initializes the engine's Zobrist keys and TT.");
    m.def("find_best_move", &kestog_core::find_best_move, "Finds the best move using iterative deepening search.",
          py::arg("board"), py::arg("color_to_move"), py::arg("max_depth"), py::arg("time_limit_ms"));
    
    m.def("generate_legal_moves", &kestog_core::generate_legal_moves, "Generates all legal moves for a position.");
    m.def("apply_move", &kestog_core::apply_move, "Applies a move to the board.");
    m.def("calculate_hash", &kestog_core::calculate_hash, "Calculates Zobrist hash for a board state.");
}
