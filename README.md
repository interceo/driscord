# driscord

Минимальный прототип: C++ WebSocket сигналинг + WebRTC клиент на TypeScript (Vite).

## Требования
- CMake >= 3.20, C++20 компилятор
- Boost >= 1.78, OpenSSL
- Node 18+

## Сборка сервера
```
cmake -S . -B build
cmake --build build -j
./build/server/driscord_server 8080
```

## Клиент
```
cd client
npm i
npm run dev
```

Клиент ожидает, что WebSocket сервер доступен по ws://localhost:8080. 