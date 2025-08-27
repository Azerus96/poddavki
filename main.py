import json
import os
import kestog_core
from fastapi import FastAPI, WebSocket
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles
from starlette.websockets import WebSocketDisconnect

# --- Константы ---
WHITE, BLACK = 1, 2
TT_SIZE_MB = 128
SEARCH_DEPTH = 16
TIME_LIMIT_MS = 5000

# --- ТАБЛИЦА ДЛЯ ЧЕЛОВЕКО-ЧИТАЕМОГО ЛОГИРОВАНИЯ ---
IDX_TO_ALG = [
    'b1', 'd1', 'f1', 'h1', 'a2', 'c2', 'e2', 'g2',
    'b3', 'd3', 'f3', 'h3', 'a4', 'c4', 'e4', 'g4',
    'b5', 'd5', 'f5', 'h5', 'a6', 'c6', 'e6', 'g6',
    'b7', 'd7', 'f7', 'h7', 'a8', 'c8', 'e8', 'g8'
]

# --- Инициализация C++ движка ---
kestog_core.init_engine(TT_SIZE_MB)

# --- Веб-сервер FastAPI ---
app = FastAPI()
app.mount("/static", StaticFiles(directory="static"), name="static")

@app.get("/")
async def read_root(): return FileResponse('static/index.html')

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    
    initial_board = kestog_core.Bitboard()
    initial_board.white_men = 4095
    initial_board.black_men = 4293918720
    initial_board.kings = 0
    current_board = initial_board

    print("--- [ЛОГ] Новая игра началась. Отправка начальной доски клиенту. ---")
    await websocket.send_json({
        "type": "board_update",
        "board": { "white_men": str(current_board.white_men), "black_men": str(current_board.black_men), "kings": str(current_board.kings) },
        "turn": WHITE, "message": "Ваш ход"
    })

    try:
        while True:
            data = await websocket.receive_text()
            payload = json.loads(data)
            
            if payload['type'] == 'engine_move':
                print("\n--- [ЛОГ] Получен запрос на ход движка. Начинаю поиск... ---")
                result = kestog_core.find_best_move(current_board, BLACK, SEARCH_DEPTH, TIME_LIMIT_MS)
                
                if result.best_move.mask_from != 0:
                    from_idx = result.best_move.mask_from.bit_length() - 1
                    to_idx = result.best_move.mask_to.bit_length() - 1
                    print(f"--- [ЛОГ] Движок выбрал ход: {IDX_TO_ALG[from_idx]} -> {IDX_TO_ALG[to_idx]} (индексы {from_idx} -> {to_idx}) ---")
                    
                    # =================================================================
                    # >>>>> ГЛАВНОЕ ИСПРАВЛЕНИЕ <<<<<
                    # Применяем ход движка к состоянию доски на сервере.
                    current_board = kestog_core.apply_move(current_board, result.best_move, BLACK)
                    # =================================================================
                    
                    if not current_board.white_men:
                        await websocket.send_json({"type": "game_over", "message": "Вы проиграли (у вас не осталось шашек)!"})
                        continue
                    
                    player_moves = kestog_core.generate_legal_moves(current_board, WHITE)
                    if not player_moves:
                        await websocket.send_json({"type": "game_over", "message": "Вы проиграли (у вас нет ходов)!"})
                    else:
                        await websocket.send_json({
                            "type": "board_update", 
                            "board": {"white_men": str(current_board.white_men), "black_men": str(current_board.black_men), "kings": str(current_board.kings)}, 
                            "turn": WHITE,
                            "engine_info": f"Depth: {result.final_depth}, Score: {result.score}, Nodes: {result.nodes_searched}"
                        })
                else:
                    await websocket.send_json({"type": "game_over", "message": "Вы победили (у движка нет ходов)!"})

            elif payload['type'] == 'move':
                try:
                    from_32 = int(payload['move']['from'])
                    to_32 = int(payload['move']['to'])
                    print(f"\n--- [ЛОГ] Получен ход от клиента: {IDX_TO_ALG[from_32]} -> {IDX_TO_ALG[to_32]} (индексы {from_32} -> {to_32}) ---")

                    if not (0 <= from_32 < 32 and 0 <= to_32 < 32):
                        await websocket.send_json({"type": "error", "message": "Неверные координаты хода!"})
                        continue
                    from_mask = 1 << from_32
                    to_mask = 1 << to_32
                except (KeyError, ValueError, TypeError):
                    await websocket.send_json({"type": "error", "message": "Неверный формат хода!"})
                    continue
                
                legal_moves = kestog_core.generate_legal_moves(current_board, WHITE)
                print("--- [ЛОГ] C++ движок считает легальными ТОЛЬКО следующие ходы: ---")
                if not legal_moves:
                    print("СПИСОК ПУСТ!")
                else:
                    is_capture_move = legal_moves[0].captured_pieces > 0
                    if is_capture_move:
                        print("(Это ходы-взятия, тихие ходы запрещены)")
                    else:
                        print("(Это тихие ходы, взятий на доске нет)")
                        
                    for move in legal_moves:
                        from_idx = move.mask_from.bit_length() - 1
                        to_idx = move.mask_to.bit_length() - 1
                        print(f"    Ход: {IDX_TO_ALG[from_idx]} -> {IDX_TO_ALG[to_idx]} (индексы {from_idx} -> {to_idx})")
                print("-----------------------------------------------------------------")
                
                found_move = next((m for m in legal_moves if m.mask_from == from_mask and m.mask_to == to_mask), None)

                if found_move:
                    print("--- [ЛОГ] ВЕРДИКТ: Ход игрока НАЙДЕН в списке легальных. Ход принят. ---")
                    current_board = kestog_core.apply_move(current_board, found_move, WHITE)
                    
                    if not current_board.black_men:
                        await websocket.send_json({"type": "game_over", "message": "Вы победили (у движка не осталось шашек)!"})
                        continue
                    
                    is_multicapture = False
                    if found_move.captured_pieces:
                        # Проверяем, есть ли продолжение взятия с той же шашки
                        next_captures = kestog_core.generate_legal_moves(current_board, WHITE)
                        if next_captures and next_captures[0].captured_pieces > 0:
                            # Если есть хоть один ход-взятие, начинающийся с поля, где мы закончили, это мульти-взятие
                            if any(cap.mask_from == found_move.mask_to for cap in next_captures):
                                is_multicapture = True
                    
                    if is_multicapture:
                        await websocket.send_json({ "type": "board_update", "board": {"white_men": str(current_board.white_men), "black_men": str(current_board.black_men), "kings": str(current_board.kings)}, "turn": WHITE, "message": "Завершите взятие!", "must_move_from": to_32 })
                    else:
                        engine_moves = kestog_core.generate_legal_moves(current_board, BLACK)
                        if not engine_moves:
                            await websocket.send_json({"type": "game_over", "message": "Вы победили (у движка нет ходов)!"})
                        else:
                             await websocket.send_json({ "type": "board_update", "board": {"white_men": str(current_board.white_men), "black_men": str(current_board.black_men), "kings": str(current_board.kings)}, "turn": BLACK })
                else:
                    print("--- [ЛОГ] ВЕРДИКТ: Ход игрока НЕ НАЙДЕН в списке легальных. Отправка ошибки. ---")
                    await websocket.send_json({"type": "error", "message": "Нелегальный ход!"})
    except WebSocketDisconnect:
        print("Client disconnected")
