# Архитектура

## Компоненты

Driscord состоит из четырёх частей:

1. `client-qt/` — Qt 6/QML приложение. Оно работает с REST API и напрямую
   использует C++-ядро.
2. `core/` — C++20 медиаядро: WebRTC DataChannel, Opus, захват и воспроизведение
   аудио, захват экрана и FFmpeg-видео.
3. `backend/signaling_server/` — небольшой Boost.Beast WebSocket relay.
4. `backend/api/` — FastAPI, PostgreSQL и JWT.

STUN/TURN в репозитории не реализован. Для него используется отдельный готовый
сервер, обычно coturn.

## Потоки данных

```text
                         PostgreSQL
                             ^
                             |
                    FastAPI :9002
                    ^               ^
                   /                 \
             HTTP/JWT             HTTP/JWT
                 /                     \
          Qt client A              Qt client B
                 \                     /
                  \  WS, SDP + ICE    /
                   Signaling :9001

          Qt client A <===========> Qt client B
                  WebRTC DataChannel (обычно UDP)
                         \
                          coturn :3478 + relay range
                          (только если P2P не удалось)
```

API не участвует в передаче звука. Signaling хранит комнаты только в памяти и
пересылает JSON-сообщения; после обмена SDP/ICE медиатрафик идёт между клиентами
или через TURN.

## Голосовой канал и signaling

При выборе канала клиент соединяется с
`ws://<server>/channels/<channel_id>?u=<username>`. Идентификатор канала является
ключом комнаты. При подключении сервер назначает случайный 16-значный hex ID и
отдаёт `welcome` со списком уже подключённых пиров. Далее relay обрабатывает:

- `offer`, `answer`, `candidate` — установление WebRTC;
- `peer_joined`, `peer_left` — состав комнаты;
- `streaming_start/stop`, `watch_start/stop` — демонстрация экрана.

Сервер также отвечает на `GET /presence` JSON-списком комнат, peer ID и имён.
Состояние не сохраняется между перезапусками.

В текущем коде signaling не сверяет JWT, membership канала или имя пользователя.
Любой клиент, знающий ID канала, может подключиться, а `/presence` доступен без
авторизации. Это важно закрыть до публичного production-развёртывания.

## WebRTC, STUN и TURN

`Transport` всегда добавляет `stun:stun.l.google.com:19302`. Серверы из
`turn_servers` добавляются после него. Поддерживаются:

- `turn:host:port` — TURN поверх UDP;
- `turns:host:port` — TURN поверх TLS.

Если порт не указан, используется `3478`. URL с query-параметрами вида
`?transport=tcp`, IPv6-литералы и отдельная схема `stun:` в `turn_servers`
парсером `add_turn_server()` корректно не разбираются. TCP TURN без TLS тоже
нельзя явно выбрать этой конфигурацией. Для текущего клиента основной совместимый
вариант — `turn:turn.example.org:3478`.

DataChannel создаются отдельно для аудио, управляющих сообщений и видео. Видео
режется на фрагменты примерно по 60 KiB; общий лимит сообщения SCTP выставлен в
128 KiB.

## Хранение данных

PostgreSQL содержит пользователей, серверы, membership, каналы и приглашения.
При старте API выполняет `Base.metadata.create_all()` и точечный `ALTER TABLE`
для `avatar_url`; полноценный запуск Alembic-миграций пока отсутствует.

Аватары, версии и пакеты обновлений хранятся локально в `DATA_DIR` (по умолчанию
`backend/api/data`). Для нескольких экземпляров API потребуется общее файловое
хранилище или перенос этих данных в object storage.
