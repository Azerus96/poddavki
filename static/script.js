document.addEventListener('DOMContentLoaded', () => {
    const boardElement = document.getElementById('board');
    const statusElement = document.getElementById('status');
    const resetButton = document.getElementById('reset-button');

    const WHITE = 1;
    let currentBoard = null;
    let isPlayerTurn = true;
    let socket = null;

    function getInitialBoard() {
        return {
            white_men: "4095",
            black_men: "4290772992",
            kings: "0"
        };
    }

    function renderBoard() {
        boardElement.innerHTML = '';
        for (let row = 7; row >= 0; row--) {
            for (let col = 0; col < 8; col++) {
                const square = document.createElement('div');
                square.className = 'square';
                const isDark = (row + col) % 2 !== 0;

                if (isDark) {
                    square.classList.add('dark');
                    const boardIndex = Math.floor(row * 4) + Math.floor(col / 2);
                    square.dataset.boardIndex = boardIndex;

                    const mask = 1n << BigInt(boardIndex);
                    let piece = null;
                    if (BigInt(currentBoard.white_men) & mask) piece = { color: 'white', isKing: (BigInt(currentBoard.kings) & mask) };
                    if (BigInt(currentBoard.black_men) & mask) piece = { color: 'black', isKing: (BigInt(currentBoard.kings) & mask) };

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
            const fromIndex = parseInt(fromSquare.dataset.boardIndex);
            const toIndex = parseInt(toSquare.dataset.boardIndex);
            const move = { from: fromIndex, to: toIndex };
            socket.send(JSON.stringify({ type: 'move', board: currentBoard, move: move }));
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
        
        // --- НАЧАЛО ИСПРАВЛЕНИЯ ---
        // Клиент не должен начинать игру сам. Он должен ждать первого сообщения от сервера.
        socket.onopen = () => { 
            statusElement.textContent = 'Соединение установлено. Ожидание доски...';
        };
        // --- КОНЕЦ ИСПРАВЛЕНИЯ ---

        socket.onmessage = (event) => {
            const data = JSON.parse(event.data);
            if (data.type === 'board_update') {
                currentBoard = data.board;
                isPlayerTurn = data.turn === WHITE;
                renderBoard();
                statusElement.textContent = data.message || (isPlayerTurn ? 'Ваш ход' : 'Ход движка...');
                if (!isPlayerTurn) {
                    setTimeout(() => socket.send(JSON.stringify({ type: 'engine_move', board: currentBoard })), 200);
                }
            } else if (data.type === 'error') {
                statusElement.textContent = `Ошибка: ${data.message}`;
                isPlayerTurn = true; // Возвращаем ход игроку после ошибки
                renderBoard();
            } else if (data.type === 'game_over') {
                statusElement.textContent = data.message;
                isPlayerTurn = false; // Игра окончена
            }
        };
        socket.onclose = () => { statusElement.textContent = 'Соединение потеряно. Обновите страницу.'; };
    }

    function startNewGame() {
        // Эта функция теперь используется только для кнопки "Начать заново".
        // Она сбрасывает доску локально и отправляет запрос на сервер,
        // но сервер в текущей реализации его не обрабатывает.
        // Для полной перезагрузки лучше просто обновить страницу.
        // Однако, для сброса на клиенте, этого достаточно.
        currentBoard = getInitialBoard();
        isPlayerTurn = true;
        renderBoard();
        statusElement.textContent = 'Ваш ход (белые)';
    }

    connect();
    resetButton.addEventListener('click', () => window.location.reload()); // Самый надежный сброс - перезагрузка
});
