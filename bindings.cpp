// bindings.cpp

// Основной заголовочный файл pybind11
#include <pybind11/pybind11.h>
// Заголовочный файл для автоматической конвертации стандартных контейнеров C++, таких как std::vector
#include <pybind11/stl.h>
// Наш собственный заголовочный файл с объявлениями структур и функций
#include "KestoG_Core.hpp"

// Создаем псевдоним для пространства имен pybind11 для краткости
namespace py = pybind11;

// Макрос PYBIND11_MODULE создает точку входа, которая будет вызвана,
// когда Python импортирует этот модуль.
// Первым аргументом идет имя модуля ('kestog_core'), оно ДОЛЖНО совпадать с именем в setup.py.
// Второй аргумент 'm' — это объект py::module_, который является главным интерфейсом для определения модуля.
PYBIND11_MODULE(kestog_core, m) {
    // Опционально: добавляем строку документации (docstring) для нашего модуля.
    // Будет видна в Python при вызове help(kestog_core)
    m.doc() = "High-performance giveaway checkers core module, written in C++";

    // "Оборачиваем" структуру Bitboard в Python-класс с именем "Bitboard"
    py::class_<kestog_core::Bitboard>(m, "Bitboard")
        // .def(py::init<>()) делает конструктор по умолчанию доступным в Python
        .def(py::init<>())
        // .def_readwrite делает поля структуры доступными для чтения и записи как атрибуты Python-объекта
        .def_readwrite("white_men", &kestog_core::Bitboard::white_men)
        .def_readwrite("black_men", &kestog_core::Bitboard::black_men)
        .def_readwrite("kings", &kestog_core::Bitboard::kings);

    // Аналогично оборачиваем структуру Move
    py::class_<kestog_core::Move>(m, "Move")
        .def(py::init<>())
        .def_readwrite("mask_from", &kestog_core::Move::mask_from)
        .def_readwrite("mask_to", &kestog_core::Move::mask_to)
        .def_readwrite("captured_pieces", &kestog_core::Move::captured_pieces)
        .def_readwrite("becomes_king", &kestog_core::Move::becomes_king);

    // Делаем наши глобальные C++ функции доступными в Python
    // m.def("имя_в_python", &имя_функции_в_c++, "опциональная_документация")
    m.def("generate_captures", &kestog_core::generate_captures, "Generates all possible capture moves.");
    m.def("generate_quiet_moves", &kestog_core::generate_quiet_moves, "Generates all possible non-capture (quiet) moves.");
    m.def("apply_move", &kestog_core::apply_move, "Applies a move to the board and returns the new board state.");
    m.def("evaluate_giveaway", &kestog_core::evaluate_giveaway, "Evaluates the board position for giveaway checkers.");
}
