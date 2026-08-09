# Архитектура

## Компоненты

Driscord состоит из четырёх частей:

1. `client-qt/` — Qt 6/QML приложение, REST client и UI.
2. `core/` — C++20 lifecycle/signaling слой над Google WebRTC.
3. `backend/signaling_server/` — Boost.Beast WebSocket и SFU на
   libdatachannel Tracks.
4. `backend/api/` — FastAPI, PostgreSQL и JWT.

Медиа идёт client → SFU → clients. Прямых соединений между клиентами нет, а SFU
не декодирует звук или видео.

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
       Qt + Google WebRTC A    Qt + Google WebRTC B
                 \                     /
                  \ WebSocket signaling
                   \ ICE/DTLS/SRTP (RTP/RTCP)
                    Signaling/SFU :9001 + UDP range
```

Клиент является offerer и устанавливает с сервером два PeerConnection:

- `voice`: один upstream microphone track и bounded pool recvonly audio slots;
- `screen`: один upstream screen-video/system-audio pair и bounded pool
  recvonly video/audio pairs.

Независимые remote tracks имеют стабильные SDP `mid`. Сервер сообщает
`mid -> peer_id` через `track_binding`, поэтому перепривязка publisher к
заранее согласованному слоту не требует renegotiation.

## Клиентский media engine

Google WebRTC штатно выполняет microphone capture, APM, Opus, video encoding,
RTP/RTCP, congestion control, NetEq/jitter buffering, audio mixing и decode.
Захват экрана использует `DesktopCapturer` и `VideoTrackSource`. Собственных
Audio/Video Sender/Receiver, codec orchestration и media DataChannels нет.

`GoogleWebRtcRuntime` и session-классы нужны из-за владения потоками,
PeerConnectionFactory/tracks и строгого порядка остановки. Stateless SDP,
RTCStats и RTP transforms вынесены в функции. `GoogleWebRtcClient` связывает
две сессии с UI preferences; его private state планируется разделить на voice
и screen lifecycle-компоненты без появления новых sender/receiver обёрток.

System loopback audio остаётся узким platform adapter. Сведённый Google WebRTC
screen audio выводится через bounded PCM queue и miniaudio hardware device.

## SFU media routing

Сервер принимает encoded RTP на libdatachannel Tracks. `VoiceRouter` и
`ScreenRouter` назначают publishers subscriber slots и переписывают RTP fields,
чтобы downstream видел непрерывный поток со стабильным SSRC. RTCP завершается
на каждом hop: сервер обслуживает NACK из локального packet cache и пересылает
PLI upstream для screen keyframe.

Screen video и его system audio назначаются парой. Клиент может показывать
несколько peers одновременно: отдельные `watch_start/watch_stop` содержат
target `peerId`, и SFU заполняет slots только выбранными publishers. Число
одновременных потоков ограничено заранее согласованной slot capacity.

`GET /media_stats` отдаёт число sessions и streaming peers. Детальная client
quality telemetry берётся из стандартного `RTCStatsReport`.

## Signaling

Клиент подключается к
`ws://<server>/channels/<channel_id>?u=<username>`. Сигналинг переносит roster,
stream/watch state, SDP и ICE. Для media messages поле `connection` обязательно
и принимает только `voice` или `screen`; legacy transport не поддерживается.

Сервер отвечает `welcome`, затем рассылает `peer_joined`/`peer_left`,
`streaming_start/stop` внутри комнаты. `watch_start/stop` являются адресными
запросами к SFU и другим клиентам не рассылаются. SDP между клиентами не
пересылается: его потребляет SFU.

В текущем коде signaling не сверяет JWT, membership канала или заявленное имя
пользователя. `/presence` также открыт. Это необходимо закрыть до публичного
production-развёртывания.

## Хранение данных

PostgreSQL содержит пользователей, серверы, membership, каналы и приглашения.
При старте API выполняет `Base.metadata.create_all()` и точечные изменения
схемы; полноценный Alembic workflow пока отсутствует. Аватары, версии и пакеты
обновлений хранятся в `DATA_DIR` и требуют общего object storage при нескольких
экземплярах API.
