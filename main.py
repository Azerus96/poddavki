import json
import os
import kestog_core
from fastapi import FastAPI, WebSocket
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles
from starlette.websockets import WebSocketDisconnect
import asyncio # Импортируем asyncio для небольшой задержки

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
            
            # =================================================================
            # >>>>> ИЗМЕНЕНИЕ ЛОГИКИ <<<<<
            # Сервер теперь обрабатывает только один тип сообщения от клиента: 'move'
            # Сообщение 'engine_move' больше не используется.
            # =================================================================
            if payload['type'] == 'move':
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
                found_move = next((m for m in legal_moves if m.mask_from == from_mask and m.mask_to == to_mask), None)

                if not found_move:
                    print("--- [ЛОГ] ВЕРДИКТ: Ход игрока НЕ НАЙДЕН в списке легальных. Отправка ошибки. ---")
                    await websocket.send_json({"type": "error", "message": "Нелегальный ход!"})
                    continue

                # --- Если ход игрока легален ---
                print("--- [ЛОГ] ВЕРДИКТ: Ход игрока НАЙДЕН в списке легальных. Ход принят. ---")
                current_board = kestog_core.apply_move(current_board, found_move, WHITE)

                # Проверка на конец игры после хода игрока
                if not current_board.black_men:
                    await websocket.send_json({"type": "game_over", "message": "Вы победили (у движка не осталось шашек)!"})
                    continue
                
                # Проверка на мульти-взятие
                is_multicapture = False
                if found_move.captured_pieces:
                    next_captures = kestog_core.generate_legal_moves(current_board, WHITE)
                    if next_captures and next_captures[0].captured_pieces > 0:
                        if any(cap.mask_from == found_move.mask_to for cap in next_captures):
                            is_multicapture = True
                
                if is_multicapture:
                    # Если нужно продолжать бить, отправляем доску и ждем следующего хода от игрока
                    await websocket.send_json({ "type": "board_update", "board": {"white_men": str(current_board.white_men), "black_men": str(current_board.black_men), "kings": str(current_board.kings)}, "turn": WHITE, "message": "Завершите взятие!", "must_move_from": to_32 })
                    continue # Пропускаем ход движка, так как игрок должен бить дальше

                # --- Если ход игрока завершен, СРАЗУ ЖЕ запускаем логику движка ---
                
                # Отправляем промежуточный статус, чтобы игрок видел, что его ход принят
                await websocket.send_json({
                    "type": "board_update",
                    "board": {"white_men": str(current_board.white_men), "black_men": str(current_board.black_men), "kings": str(current_board.kings)},
                    "turn": BLACK, # Указываем, что теперь ход движка
                    "message": "Ход движка..."
                })
                await asyncio.sleep(0.1) # Небольшая пауза, чтобы сообщение успело отправиться и отрисоваться

                # Проверяем, есть ли у движка ходы
                engine_moves = kestog_core.generate_legal_moves(current_board, BLACK)
                if not engine_moves:
                    await websocket.send_json({"type": "game_over", "message": "Вы победили (у движка нет ходов)!"})
                    continue

                # Запускаем поиск лучшего хода для движка
                print("\n--- [ЛОГ] Сервер инициирует ход движка. Начинаю поиск... ---")
                result = kestog_core.find_best_move(current_board, BLACK, SEARCH_DEPTH, TIME_LIMIT_MS)
                
                if result.best_move.mask_from != 0:
                    from_idx = result.best_move.mask_from.bit_length() - 1
                    to_idx = result.best_move.mask_to.bit_length() - 1
                    print(f"--- [ЛОГ] Движок выбрал ход: {IDX_TO_ALG[from_idx]} -> {IDX_TO_ALG[to_idx]} (индексы {from_idx} -> {to_idx}) ---")
                    
                    current_board = kestog_core.apply_move(current_board, result.best_move, BLACK)
                    
                    # Проверка на конец игры после хода движка
                    if not current_board.white_men:
                        await websocket.send_json({"type": "game_over", "message": "Вы проиграли (у вас не осталось шашек)!"})
                        continue
                    
                    player_moves = kestog_core.generate_legal_moves(current_board, WHITE)
                    if not player_moves:
                        await websocket.send_json({"type": "game_over", "message": "Вы проиграли (у вас нет ходов)!"})
                    else:
                        # Отправляем финальное состояние доски после хода движка и передаем ход игроку
                        await websocket.send_json({
                            "type": "board_update", 
                            "board": {"white_men": str(current_board.white_men), "black_men": str(current_board.black_men), "kings": str(current_board.kings)}, 
                            "turn": WHITE,
                            "message": "Ваш ход",
                            "engine_info": f"Depth: {result.final_depth}, Score: {result.score}, Nodes: {result.nodes_searched}"
                        })
                else: # Эта ветка маловероятна, т.к. мы уже проверили наличие ходов
                    await websocket.send_json({"type": "game_over", "message": "Вы победили (у движка нет ходов)!"})

    except WebSocketDisconnect:
        print("Client disconnected")
