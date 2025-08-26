document.addEventListener('DOMContentLoaded', () => {
    const boardElement = document.getElementById('board');
    const statusElement = document.getElementById('status');
    const resetButton = document.getElementById('reset-button');

    const WHITE = 1;
    let currentBoard = null;
    let isPlayerTurn = true;
    let socket = null;

    // Таблица для конвертации индекса 0-31 (внутренний) в 0-63 (для сервера)
    const LOOKUP_32_TO_64 = {
        0: 1, 1: 3, 2: 5, 3: 7,
        4: 8, 5: 10, 6: 12, 7: 14,
        8: 17, 9: 19, 10: 21, 11: 23,
        12: 24, 13: 26, 14: 28, 15: 30,
        16: 33, 17: 35, 18: 37, 19: 39,
        20: 40, 21: 42, 22: 44, 23: 46,
        24: 49, 25: 51, 26: 53, 27: 55,
        28: 56, 29: 58, 30: 60, 31: 62
    };

    function renderBoard() {
        boardElement.innerHTML = '';
        // Создаем доску с правильной ориентацией (a1 внизу слева)
        for (let row = 7; row >= 0; row--) {
            for (let col = 0; col < 8; col++) {
                const square = document.createElement('div');
                const squareIndex64 = row * 8 + col;
                square.className = 'square';
                const isDark = (row + col) % 2 !== 0;

                square.classList.add(isDark ? 'dark' : 'light');
                
                if (isDark) {
                    // Вычисляем индекс 0-31 для черных клеток
                    const boardIndex32 = Math.floor(squareIndex64 / 8) * 4 + Math.floor(squareIndex64 % 8 / 2);
                    square.dataset.boardIndex = boardIndex32;

                    const mask = 1n << BigInt(boardIndex32);
                    let piece = null;
                    if (BigInt(currentBoard.white_men) & mask) piece = { color: 'white', isKing: (BigInt(currentBoard.kings) & mask) !== 0n };
                    if (BigInt(currentBoard.black_men) & mask) piece = { color: 'black', isKing: (BigInt(currentBoard.kings) & mask) !== 0n };

                    if (piece) {
                        const pieceElement = document.createElement('div');
                        pieceElement.className = `piece ${piece.color}`;
                        if (piece.isKing) pieceElement.classList.add('king');
                        if (isPlayerTurn && piece.color === 'white') {
                           pieceElement.addEventListener('touchstart', handleDragStart, { passive: false });
                           pieceElement.addEventListener('mousedown', handleDragStart);
                        }
                        square.appendChild(pieceElement);
                    }
                }
                boardElement.appendChild(square);
            }
        }
    }

    let draggedClone = null;
    let originalPiece = null;

    function handleDragStart(e) {
        e.preventDefault();
        if (!isPlayerTurn) return;
        originalPiece = e.target;

        draggedClone = originalPiece.cloneNode(true);
        const rect = originalPiece.getBoundingClientRect();
        draggedClone.style.width = `${rect.width}px`;
        draggedClone.style.height = `${rect.height}px`;
        draggedClone.classList.add('dragging');
        document.body.appendChild(draggedClone);
        originalPiece.style.opacity = '0.3';

        const touch = e.touches ? e.touches[0] : e;
        moveDraggedPiece(touch);

        if (e.type === 'touchstart') {
            window.addEventListener('touchmove', handleDragMove, { passive: false });
            window.addEventListener('touchend', handleDragEnd, { passive: false });
        } else {
            window.addEventListener('mousemove', handleDragMove);
            window.addEventListener('mouseup', handleDragEnd);
        }
    }

    function handleDragMove(e) {
        e.preventDefault();
        if (draggedClone) {
            const touch = e.touches ? e.touches[0] : e;
            moveDraggedPiece(touch);
        }
    }

    function handleDragEnd(e) {
        e.preventDefault();
        if (!draggedClone || !originalPiece) return;

        const fromSquare = originalPiece.parentElement;
        draggedClone.style.display = 'none';
        const touch = e.changedTouches ? e.changedTouches[0] : e;
        const toSquare = document.elementFromPoint(touch.clientX, touch.clientY)?.closest('.square.dark');

        if (toSquare && fromSquare !== toSquare) {
            const fromIndex32 = parseInt(fromSquare.dataset.boardIndex);
            const toIndex32 = parseInt(toSquare.dataset.boardIndex);
            
            // Конвертируем в 64-клеточную нумерацию перед отправкой
            const fromIndex64 = LOOKUP_32_TO_64[fromIndex32];
            const toIndex64 = LOOKUP_32_TO_64[toIndex32];

            const move = { from: fromIndex64, to: toIndex64 };
            // Отправляем только ход, без доски
            socket.send(JSON.stringify({ type: 'move', move: move }));
        }

        originalPiece.style.opacity = '1';
        draggedClone.remove();
        draggedClone = null;
        originalPiece = null;

        if (e.type === 'touchend') {
            window.removeEventListener('touchmove', handleDragMove);
            window.removeEventListener('touchend', handleDragEnd);
        } else {
            window.removeEventListener('mousemove', handleDragMove);
            window.removeEventListener('mouseup', handleDragEnd);
        }
    }

    function moveDraggedPiece(touch) {
        draggedClone.style.left = `${touch.clientX - draggedClone.offsetWidth / 2}px`;
        draggedClone.style.top = `${touch.clientY - draggedClone.offsetHeight / 2}px`;
    }

    function connect() {
        const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
        socket = new WebSocket(`${protocol}//${window.location.host}/ws`);
        
        socket.onopen = () => { 
            statusElement.textContent = 'Соединение установлено. Ожидание доски...';
        };

        socket.onmessage = (event) => {
            const data = JSON.parse(event.data);
            if (data.type === 'board_update') {
                currentBoard = data.board;
                isPlayerTurn = data.turn === WHITE;
                renderBoard(); // Сначала рендерим доску

                // Если сервер прислал указание на мульти-захват, подсвечиваем шашку
                if (data.must_move_from !== undefined) {
                    const squares = boardElement.querySelectorAll('.square[data-board-index]');
                    squares.forEach(sq => {
                        if (parseInt(sq.dataset.boardIndex) === data.must_move_from) {
                            const piece = sq.querySelector('.piece');
                            if (piece) piece.classList.add('must-move');
                        }
                    });
                }

                statusElement.textContent = data.message || (isPlayerTurn ? 'Ваш ход' : 'Ход движка...');
                if (!isPlayerTurn) {
                    // Отправляем запрос на ход движка без доски
                    setTimeout(() => socket.send(JSON.stringify({ type: 'engine_move' })), 500);
                }
            } else if (data.type === 'error') {
                statusElement.textContent = `Ошибка: ${data.message}`;
                isPlayerTurn = true;
                renderBoard();
            } else if (data.type === 'game_over') {
                currentBoard = data.board || currentBoard; // Обновляем доску, если она пришла с сообщением о конце игры
                renderBoard();
                statusElement.textContent = data.message;
                isPlayerTurn = false;
            }
        };
        socket.onclose = () => { statusElement.textContent = 'Соединение потеряно. Обновите страницу.'; };
    }

    connect();
    resetButton.addEventListener('click', () => window.location.reload());
});
