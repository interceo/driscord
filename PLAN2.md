# Ревью миграции на Google WebRTC + RTP SFU, и план правок

> **Статус исполнения, 9 августа 2026.** Документ ниже сохранён как исходное
> ревью. Все дефекты корректности из §1 закрыты: lock inversion в роутерах и
> screen sinks, client mutex вокруг WebRTC, thread-affinity `SetVolume`,
> исключение `rtpMap`, padded RTP, lifetime callbacks и QML reactivity.
> Дополнительно исправлены очистка peers/streams при disconnect, terminal-state
> recovery обеих media PeerConnection, `streaming_stop` при deinit, CI,
> воспроизводимость `depot_tools`, компактная упаковка WebRTC SDK и
> `libdatachannel.so`.
>
> §2.1 (адресные подписки), reconnect replay, per-peer RTCStats и реальные
> loss/reorder/restart tests также реализованы. Актуальными остаются
> production congestion control/simulcast, выбор и loopback policy system
> audio, slot-capacity/active-speaker policy, расширенная observability и
> длительный soak. Фактический статус и оставшийся порядок работы ведутся в
> `PLAN.md`.

## Context

В рабочем дереве лежит незакоммиченная миграция: самописный медиа-пайплайн
(Opus/FFmpeg поверх DataChannel, WSOLA, MediaClock, ReorderBuffer) заменён на
Google WebRTC как единственный клиентский медиа-движок, а сервер стал RTP SFU на
`libdatachannel::Track`. Диff: ~16.7k удалённых строк, ~7.1k новых, 102 файла.

Архитектурное направление правильное и реализовано аккуратно: адаптеры не
протекают типами WebRTC в публичные заголовки, изоляция GnuTLS/NSS DSO решает
реальную проблему коллизии символов BoringSSL/OpenSSL, `--bench`/`--windows`
честно падают с объяснением вместо тихой деградации, PLAN.md откровенно
перечисляет незакрытые пункты.

Цель этого документа — зафиксировать найденные дефекты (с проверкой по коду) и
дать упорядоченный план правок. Приоритет — медиа-пайплайн.

> **Состояние дерева.** Ревью снято по состоянию на ~15:00. Во время ревью
> `voice_rtp_rewriter.*` был заменён на `rtp_slot_rewriter.*`: переписывание
> sequence/timestamp с generation-токенами **уже реализовано**, этого пункта в
> плане нет. Перед исполнением стоит перепроверить пункты по SFU — файлы
> активно правятся.

---

## 1. Блокирующие дефекты корректности

### 1.1 ABBA-дедлок в `close()` обоих роутеров

`VoiceRouter::Impl::close` (`backend/signaling_server/src/voice_router.cpp:283-302`)
и `ScreenRouter::Impl::close` (`screen_router.cpp:504-532`) держат `Impl::mutex`
и вызывают `track->onMessage(nullptr)` / `setMediaHandler(nullptr)`. Присваивание
в `rtc::synchronized_callback` берёт тот же `recursive_mutex`, под которым
исполняется тело колбэка, а тело колбэка — это `route()`, который берёт
`Impl::mutex` (`voice_router.cpp:218`, `screen_router.cpp:355`).

- Поток A (`stop()` / `~Router`): держит `Impl::mutex` → ждёт callback-mutex.
- Поток B (SRTP recv): держит callback-mutex → ждёт `Impl::mutex`.

Достижимо из `SignalingServerFixture::~SignalingServerFixture` (там `stop()`
зовётся с тестового потока) и при выходе последней сессии из комнаты с живым
медиа. `recursive_mutex` не спасает — потоки разные.

**Фикс:** под локом выставить `closed = true` и переложить треки в локальный
`vector`, лок отпустить, отцеплять колбэки уже снаружи. `closed` и так гейтит
`route()`, так что отцепление нужно только для скорости освобождения.

### 1.2 Дедлок клиента: `GoogleWebRtcClient::Impl::mutex` держится поперёк блокирующих вызовов WebRTC

Сигнальный поток WebRTC берёт этот же мьютекс в `on_remote_track`
(`core/src/webrtc/google_webrtc_client.cpp:221`, `:267`) и в `cache_screen_stats`.
Прикладные потоки держат его и делают синхронный marshal на сигнальный поток:

| Держит `Impl::mutex` | Блокирующий вызов под ним |
|---|---|
| `set_deafened` `:609` | `apply_voice_mid_locked` → `remote_audio_track` → `GetTransceivers()` (`google_webrtc_voice_session.cpp:335`) |
| `set_muted` `:596`, `set_master_volume` `:624`, `set_peer_volume` `:642`, `set_peer_muted` `:661` | то же + `AudioTrack::set_enabled` |
| `join_stream` `:701`, `leave_stream` `:738`, `leave_streams` `:717`, `set_stream_volume` `:891` | `set_remote_audio_enabled` |
| `apply_binding` `:174`, `apply_candidate` `:159` | `AddIceCandidate` |
| `start_voice_if_requested` `:205`, `start_screen_if_requested` `:246` | ~20-24 блокирующих вызова фабрики/`AddTransceiver`/SLD |
| `start_sharing` `:835` | `start_desktop_capture`, блокируется на `std::promise` потока захвата (на Wayland — портальный диалог!) |
| `screen_stats_json` `:913` | `GetStats` |

**Фикс:** снимать под локом снапшот (список mid + нужные флаги/громкости),
отпускать мьютекс, вызывать сессию снаружи. Альтернатива — единый однопоточный
executor для всех мутаций сессий.

### 1.3 Дедлок в `RemoteScreenSinks`

`set_audio_enabled` / `set_audio_volume`
(`core/src/webrtc/google_webrtc_screen_adapters.cpp:505-528`) держат
`sinks_mutex_` поперёк `track->set_enabled()` (блокирующий marshal), а
`attach_audio` берёт `sinks_mutex_` **на сигнальном потоке** из `OnTrack`.
Тот же ABBA. Фикс тот же: снапшот `scoped_refptr`, отпустить лок, вызвать.

### 1.4 `std::terminate` от удалённого пира

`primary_rtp_format` объявлена `noexcept`
(`backend/signaling_server/src/sfu_media_utils.hpp:34`) и вызывает
`description.rtpMap(payload_type)` (`sfu_media_utils.cpp:85`).
`Description::Media::rtpMap` **бросает** `std::invalid_argument`, а не возвращает
null (проверено: `_deps/libdatachannel-src/src/description.cpp:989-995`), при этом
`payloadTypes()` наполняется прямо из списка PT в `m=`-строке независимо от
`mRtpMaps` (`description.cpp:900-906`). Любой PT без `a=rtpmap:` (статические
0/8/9, или PT, который пир перечислил, но не описал) → `noexcept` → **abort
сервера, инициируемый удалённо**.

Тот же мёртвый null-check в `apply_forwarding_feedback_policy`
(`sfu_media_utils.cpp:37`) — там не `noexcept`, но исключение улетает в
`try/catch` в `media_connections.cpp:195`, то есть регистрация трека молча
проваливается.

**Фикс:** обернуть `rtpMap` в хелпер, возвращающий `RtpMap*`/`nullptr` через
try/catch; снять `noexcept` там, где оно не гарантировано.
(`extMap`/`extIds` безопасны: `extIds()` строится из самого `mExtMaps`.)

### 1.5 Гонка `RemoteAudioSource::SetVolume`

`AudioTrackProxy::GetSource` — `BYPASS_PROXY`, возвращает непроксированный
источник, поэтому `track->GetSource()->SetVolume(v)`
(`google_webrtc_voice_session.cpp:585`, `google_webrtc_screen_adapters.cpp:526`)
исполняется на прикладном потоке. `RemoteAudioSource::SetVolume` итерирует
`std::list<AudioObserver*> audio_observers_` **без мьютекса**, а
регистрация/снятие наблюдателей идёт с сигнального потока. Гонка при
перепривязке слота одновременно с движением ползунка громкости.
**Фикс:** маршалить на сигнальный поток.

### 1.6 `spsc_queue::reset()` параллельно с продюсером

`GoogleWebRtcPcmPlayout::stop()` (`google_webrtc_pcm_playout.cpp:18-23`) зовёт
`queue_.reset()`, который не потокобезопасен, тогда как рендер-поток ADM
screen-рантайма всё ещё жив (рантайм не сносится вместе с сессией).

### 1.7 Сырой `this` в колбэках, переживающих объект

- `on_rendered_audio` (`google_webrtc_client.cpp:78-86`) захватывает `this`; лямбда
  живёт в ADM внутри `screen_runtime.impl_` — `shared_ptr`, который держат и
  живые screen-сессии. Реальное окно UAF на `screen_playout`.
- `on_offer` / `on_candidate` (`:211-218`, `:255-263`) захватывают `this`, тогда
  как соседние `on_remote_track` / `on_remote_video` корректно берут
  `weak_from_this()`. `close()` не фенсит in-flight `OnIceCandidate`.

### 1.8 Падение по assert на padded RTP

`RtcpSrReporter` содержит `assert(!header->padding())`
(`_deps/libdatachannel-src/src/rtcpsrreporter.cpp:74`). Рерайтер бит `P` не
смотрит вообще. Любой padded-пакет от паблишера → abort в Debug/assert-сборках.

### 1.9 Мульти-стрим UI не работает

`client-qt/qml/components/ContentPanel.qml:164`:

```qml
readonly property bool watching: isStream && appState.isWatchingStream(modelData.id)
```

`isWatchingStream` — `Q_INVOKABLE`, а не `Q_PROPERTY` с NOTIFY, поэтому QML не
регистрирует зависимость от `watchedStreamsChanged`: биндинг вычисляется один раз.
`tile.watching` остаётся `false` → видео-`Image` (`:211`) никогда не показывается,
`onWatchingChanged` (`:166`) не срабатывает. Реактивное свойство
`watchedPeerIds` добавлено в `AppState.h:42`, но **в QML не используется нигде**.

**Фикс:** `appState.watchedPeerIds.indexOf(modelData.id) !== -1`.
Строки 25, 472, 519 — императивные вызовы, их трогать не нужно.

---

## 2. Пайплайн: архитектурные проблемы

### 2.1 Гранулярность подписки — главная проблема

`Transport::send_watch_start()` (`core/src/transport.hpp:91`) не несёт peer id.
Клиент подписывается на комнату целиком, а нужные трансляции отбирает локально:

```cpp
// google_webrtc_client.cpp:363-372 — уже ПОСЛЕ полного decode + I420→RGBA
if (binding == screen_bindings.end() || !watched_peers.contains(binding->second)) return;
```

При `remote_stream_slots = 8` (`google_webrtc_client.cpp:289`) и потолке
4 Мбит/с на слот (`:292`) просмотр **одной** трансляции стоит до ~32 Мбит/с
downlink плюс декодирование и полнокадровая конвертация всего, что потом
выбрасывается. Видео-треки невыбранных mid никогда не гасятся через
`set_enabled(false)` — в отличие от аудио.

**Фикс (этап 2 плана):** передавать в `watch_start` множество peer id; SFU
назначает слоты только под запрошенных паблишеров; клиент дополнительно гасит
видео-трек невыбранного слота.

### 2.2 Congestion control отсутствует полностью

`apply_forwarding_feedback_policy` (`sfu_media_utils.cpp:34-48`) вырезает
`transport-cc` и `goog-remb` из rtpmap и убирает TWCC extmap — и применяется к
**обеим** сторонам (`voice_router.cpp:76,109`, `screen_router.cpp:91,233`).
Следствия:

- у паблишера нет send-side BWE на аплинке;
- SFU не может сообщить энкодеру о заторе у подписчика;
- RR подписчика вообще не потребляется, `requestBitrate` не вызывается;
- `nack` остаётся объявленным на входной ноге, но `RtcpReceivingSession` NACK не
  генерирует — сервер анонсирует то, чего не делает; потери publisher→server
  неустранимы и размножаются на всех подписчиков.

Для screen share с фиксированными 4 Мбит/с это означает: перегруженный
подписчик просто теряет пакеты бесконечно. Это самый большой разрыв в качестве
медиа и он **не** покрыт ни одним тестом.

### 2.3 Состояние соединения игнорируется на обоих концах

- Клиент: обе сессии дергают `callbacks.on_state`
  (`google_webrtc_voice_session.cpp:468`, `google_webrtc_screen_session.cpp:539`),
  но `GoogleWebRtcClient` **никогда не присваивает `on_state`** (проверено
  grep'ом — ноль совпадений в `google_webrtc_client.cpp`). ICE/DTLS failure,
  consent-timeout — молча игнорируются. ICE restart отсутствует как класс.
- Сервер: `onStateChange` (`media_connections.cpp:96-102`) только логирует.
  Удалённая FSM раньше обрабатывала `PcFailed`. Теперь упавший, но ещё
  подключённый по WebSocket паблишер навсегда удерживает слот.
- Единственный путь восстановления — переподключение WebSocket
  (`google_webrtc_client.cpp:132-141`), а оно **теряет подписку**:
  `stop_screen_session` не чистит `watched_peers`, `watch_start` повторно не
  шлётся, `join_stream` не пошлёт, потому что множество непустое.
- `deinit_screen` не шлёт `streaming_stop` — SFU продолжает считать вас
  вещающим.

### 2.4 Системное аудио: обратная связь и обход громкости

Screen-рантайм построен на `webrtc::TestAudioDeviceModule` — по заголовку
upstream это «test API ... can be changed/removed without notice». Он открывает
**второе** устройство вывода параллельно с платформенным ADM голосового
рантайма, без общего клока.

Захват — это монитор дефолтного sink'а, воспроизведение screen-аудио идёт **в**
дефолтный sink, а AEC/AGC/NS для screen-источника явно выключены
(`google_webrtc_screen_session.cpp:166-170`). Пока вы шарите системный звук, всё,
что вы слышите (включая чужие голоса), захватывается и вещается обратно. Выбор
sink'а не проброшен: `capture_audio_list_targets_json` отдаёт список, но
выбранный id в `capture->start()` не передаётся (`google_webrtc_client.cpp:842`).

Отдельно: `GoogleWebRtcPcmPlayout::set_volume/set_muted/output_level` имеют
**ноль вызывающих**. Значит **deafen и мастер-громкость не заглушают системный
звук трансляции** — `set_deafened` трогает только `voice_bindings` (`:609-616`).

### 2.5 Слоты

- Потолки 16 голос / 8 экран; исчерпание **молчаливое** — `break` без лога, без
  `track_binding`, без сигнала клиенту (`voice_router.cpp:161`,
  `screen_router.cpp:290`).
- Кто именно займёт слот — порядок итерации `unordered_map`; политики
  active-speaker нет.
- Слоты не переиспользуются после протухания `weak_ptr` трека:
  `output_pair_for` (`screen_router.cpp:153`) ищет отсутствующий слот, а не
  протухший, поэтому пара с двумя мёртвыми треками занимает место навсегда.
- Пары video/audio формируются исключительно по порядку m-строк — переставь
  `AddTransceiver` на клиенте, и видео слота 0 склеится с аудио слота 3 без
  единой ошибки.
- msid у всех слотов константный (`"driscord-screen"` / `"driscord-voice"`),
  вопреки комментарию рядом; группировку пары держит только CNAME.
- Дублирование истины: `Room::streaming_peers`/`video_watchers` под
  `rooms_mutex_` и `PeerState::streaming`/`watching` под мьютексом роутера
  обновляются неатомарно и могут разъехаться.

### 2.6 Наблюдаемость просела

- `/media_stats` потерял все медиа-счётчики — остались `sessions` и
  `streamingPeers`.
- `StreamStatsOverlay` инстанцируется на каждую плитку, но зовёт глобальный
  агрегат `bridge.screenStatsJson()` (`StreamStatsOverlay.qml:47`) — все плитки
  показывают одинаковые числа по всему PeerConnection.
- Там же захардкожены нули: `videoDecodeFailures`, `audioUnderruns`, `audioFec`,
  `audioStretches` (`:56-61`) — в UI они неотличимы от измеренного нуля.
- `VoiceSessionStats` полностью реализован (`google_webrtc_voice_adapters.cpp:46-77`)
  и **не имеет ни одного потребителя**.
- `TransportStats::rtt_ms` не заполняется — `stats_json` всегда отдаёт `-1`.

### 2.7 Тестовое покрытие пайплайна

Удалено ~26 файлов тестов/харнесса, добавлено 4. Без покрытия остались:
jitter/reorder, потери, A/V-синхронизация, верность видео (PSNR/SSIM), WSOLA/
time-stretch, FSM соединений. Новый screen-тест проверяет «какой-то пиксель > 20»,
голосовой — «|sample| > 100», причём не проверяет, что 440 Гц и 880 Гц не
перепутаны местами, — то есть баг перекрёстной маршрутизации пройдёт.

При этом ретрай-сетку (`ctest --repeat until-pass:3`) убрали одновременно с тем,
как тесты стали **сильнее** зависеть от реального времени (`screen_transport`
крутит цикл `sleep_for(33ms)` × 120). `rtc_cleanup_env.hpp` подключён только в
`test_room_isolation.cpp` — в двух новых интеграционных бинарях его нет, хотя
каждый создаёт по 3 `Transport` и 3 рантайма.

Юнит-тестов нет у `screen_router.cpp` (557 строк), `sfu_media_utils.cpp`,
`media_connections.cpp`.

---

## 3. CI и сборка

CI **не мигрирован вообще** (`.github/workflows/ci.yml` не в диффе):

- job `tests` зовёт `./scripts/build.sh --test`, который требует clang и
  падает в `cmake/GoogleWebRTC.cmake:26-35` — WebRTC-чекаут никто не собирает и
  не кэширует;
- ставится FFmpeg (удалён), не ставятся clang/lld/gnutls/nss/X11-расширения;
- `build-client` выгружает `.builds/cmake/qt-release/...`, актуальный путь —
  `qt-webrtc-release`;
- `build-windows-cross` зовёт `--windows`, который теперь безусловно `exit 1`.

Итого 3 из 5 job'ов красные по построению, ни один новый тест не выполняется.

Прочее по сборке:
- `depot_tools` клонируется без пина (`scripts/build_google_webrtc.sh:17-20`) —
  вместе с `use_sysroot=false` это делает артефакт невоспроизводимым между
  машинами; формулировка «воспроизводимая GN-сборка» в PLAN.md:22 завышена;
- патч `google-webrtc-libstdcxx.patch` под именем про libstdc++ протаскивает в
  продовый `//:webrtc` **тестовый** `test_audio_device_module` и меняет
  `enable_media` → `enable_media_with_defaults`;
- `--server --test` конфигурируется с `BUILD_CORE=ON`, то есть «серверные» тесты
  тянут Google WebRTC — тезис «server-only build независим» верен только для
  голого `--server`;
- интеграционные тесты линкуют libdatachannel с GnuTLS+NSS, а деплоящийся
  сервер — с OpenSSL: **тестируется не та DTLS/SRTP-конфигурация, что
  эксплуатируется**;
- `datachannel` в клиентской сборке стал `.so`, RPATH/упаковка не описаны —
  голый артефакт клиента больше не запускается;
- `NO_MEDIA=OFF` форсится глобально, включая клиентскую сборку, которой медиа-
  стек libdatachannel не нужен.

---

## 4. Мёртвый код и рассинхрон

| Что | Где |
|---|---|
| `screen_fps` парсится в `AppConfig::screenFps` и **не читается никем**; FPS берётся из комбобокса `ShareDialog.qml:343`. В README это единственная медиа-настройка | `client-qt/src/app/AppConfig.cpp:86` |
| Три источника истины по битрейту: `stream_defaults::kSystemAudioBitrateKbps`, литерал `4'000'000`, дефолты структур. `kVoiceBitrateKbps` не используется | `core/src/config.hpp`, `google_webrtc_client.cpp:292` |
| `config.hpp` мёртв на ~90%: `TurnServer` (ICE-серверы не настраиваются нигде), весь `sync_defaults`, `kScreenBufferMs`, `kVideoSendBufferLimitBytes` | `core/src/config.hpp` |
| `video_set_watching(true)` — молчаливый no-op; `screen_stream_volume()` читает громкость «последнего тронутого» пира | `core/src/driscord_core.cpp:289`, `:353-359` |
| Заглушки, которые UI зовёт: `audio_input_level`/`audio_output_level` → `0.0f`, `audio_set_noise_gate` → пусто, списки устройств → фейковый `System default` | `core/src/driscord_core.cpp:232-269` |
| `peer_left` не гасит `m_streamingPeers`/`m_watchedPeerIds`: `driscord_core.cpp:34-39` зовёт `remove_peer`, но не `on_streaming_peer_removed_cb_` → залипшая плитка | `client-qt/src/app/AppState.cpp:80-85` |
| `streamingStarted`/`newStreamingPeer` — теперь один и тот же сигнал из `driscord_core.cpp:41-49`; комментарий про «chunk-assembly path» описывает удалённый пайплайн | `AppState.cpp:140-146` |
| Мёртвый API моста: `setVideoWatching`, `streamVolume`; `[[nodiscard]]` результат `screen_start_sharing` отбрасывается через `(void)` | `DriscordBridge.cpp:171`, `:180`, `:194` |
| `PeerInfo::primary_open` выставляется в `ws_connected_` для каждого пира — реликт P2P-меша | `core/src/transport.cpp:260` |
| Два параллельных механизма колбэков в `Transport` (одиночный слот + вектор слушателей) | `core/src/transport.hpp:138-143` |
| MinGW-обвязка жива вопреки `CLAUDE.md:22`: `cmake/toolchain-mingw64.cmake`, `third_party/fetch_win_deps.sh`, `third_party/windows/`, комментарий `CMakeLists.txt:132` | — |
| `driscord_protocol` линкует `Boost::boost`, хотя Boost в `signaling_protocol.*` больше не используется | `CMakeLists.txt:149` |
| `transport.hpp:14` тянет `<rtc/rtc.hpp>` публично, `DriscordCore::transport` — публичное поле | — |
| ~54% строк `google_webrtc_voice_session.cpp` побайтово совпадают с screen-версией; копия уже разъехалась (в screen `start()` потерян лок вокруг `desired_*`) | `google_webrtc_screen_session.cpp:80,164,180` |

---

## 5. План правок

### Этап 1 — корректность (блокирует всё остальное)

1. `close()` обоих роутеров: снапшот треков под локом, отцепление снаружи.
   `voice_router.cpp:283`, `screen_router.cpp:504`.
2. Снять `Impl::mutex` со всех блокирующих вызовов WebRTC в
   `google_webrtc_client.cpp` (таблица в §1.2): под локом — только снапшот
   намерений, вызовы сессии — снаружи.
3. То же для `RemoteScreenSinks::set_audio_enabled` / `set_audio_volume`
   (`google_webrtc_screen_adapters.cpp:505-528`).
4. Хелпер `try_rtp_map(description, pt) -> RtpMap*` c try/catch; снять
   `noexcept` с `primary_rtp_format` / `mid_extension_id` либо гарантировать его
   честно. `sfu_media_utils.cpp:37,85`.
5. Маршалить `SetVolume` на сигнальный поток (голос и screen).
6. `on_rendered_audio`, `on_offer`, `on_candidate` → `weak_from_this()`.
7. `GoogleWebRtcPcmPlayout::stop()`: остановить продюсера до `queue_.reset()`.
8. Рерайтер: проверять бит `P`, либо снимать padding перед форвардом.
9. `ContentPanel.qml:164` → биндинг на `appState.watchedPeerIds`.
10. `peer_left` на клиенте должен чистить streaming/watched (или сервер должен
    слать `streaming_stop` перед `peer_left` — предпочтительнее, так как сервер
    и так делает `streaming_peers.erase` в `ws_server.cpp:663`).

### Этап 2 — пайплайн

11. **Адресная подписка.** `watch_start` несёт множество peer id; `ScreenRouter`
    назначает слоты только под запрошенных; клиент гасит видео-трек
    невыбранного слота. Убирает и лишний downlink, и лишний декод, и делает
    осмысленным поведение при publishers > slots.
12. **Состояние соединения.** Присвоить `session_callbacks.on_state`, пробросить
    в `DriscordCore` → `AppState`; на `Failed` — пересоздать сессию (сессии
    одноразовые by design), на реконнекте — заново слать `watch_start` и
    восстанавливать sharing. `deinit_screen` должен слать `streaming_stop`.
    На сервере — сносить PC и освобождать слоты по `Failed`/`Disconnected`
    (`media_connections.cpp:96`).
13. **Congestion control.** Минимум: потреблять RR подписчиков, считать
    минимальный доступный битрейт по слотам паблишера и слать REMB вверх через
    `RtcpReceivingSession::requestBitrate`. Либо, если решено оставить как есть,
    убрать `nack` из объявляемых на входной ноге, чтобы сервер не анонсировал
    неподдерживаемое.
14. **Слоты:** логировать исчерпание и сообщать клиенту; переиспользовать пары с
    протухшими треками (`screen_router.cpp:153`); связывать video/audio пары
    явным признаком, а не порядком m-строк; свести дублирующее состояние
    `Room::*` / `PeerState::*` к одному владельцу.
15. **Системный звук:** пробросить выбранный sink в `capture->start()`; подключить
    `screen_playout` к deafen/master volume; вынести `TestAudioDeviceModule` за
    флаг или заменить на честный custom ADM.

### Этап 3 — наблюдаемость и тесты

16. Юнит-тесты на `RtpSlotRewriter`: непрерывность seq/ts при переназначении
    слота, wrap-around, отбрасывание пакетов старой generation, padding.
17. Юнит-тесты на формирование пар в `ScreenRouter` и на `sfu_media_utils`
    (в т.ч. m-строка с PT без `a=rtpmap:` — регрессия на §1.4).
18. Интеграционный тест с потерями/переупорядочиванием на реальном RTP-пути
    (`scripts/net_emulate.sh` сейчас единственный инструмент и он ручной).
19. Голосовой тест должен различать тоны по mid (иначе перекрёстная маршрутизация
    незаметна); screen-тест — предзаполнять кадры вместо `sleep_for(33ms)`×120.
20. Подключить `rtc_cleanup_env.hpp` в оба новых интеграционных бинаря.
21. Вернуть медиа-счётчики в `/media_stats`; разложить screen-статистику по
    peer/mid; убрать захардкоженные нули из `StreamStatsOverlay.qml` (лучше
    убрать строки, чем показывать ложь).

### Этап 4 — CI и уборка

22. Переписать `.github/workflows/ci.yml`: clang/lld/gnutls/nss/X11-расширения,
    кэш собранного `libwebrtc.a` (иначе job неподъёмный), актуальные пути
    артефактов, снести `build-windows-cross`.
23. Запинить `depot_tools` на ревизию; либо честно убрать слово
    «воспроизводимая» из PLAN.md.
24. Разбить патч WebRTC на `libstdcxx.patch` и `build-graph.patch`, чтобы
    протаскивание тестового ADM в прод было видно из имени.
25. Удалить MinGW-обвязку и `third_party/windows`, привести `CMakeLists.txt:132`
    в соответствие с `CLAUDE.md:22`.
26. Вычистить `config.hpp` до реально используемого; свести битрейты к одному
    источнику; либо задействовать `screen_fps`, либо убрать его из README.
27. Убрать мёртвый API: `video_set_watching`, `screen_stream_volume` без пира,
    `active_stream_peer_`, `setVideoWatching`/`streamVolume` в мосту,
    дублирующий механизм колбэков `Transport`, `Boost::boost` у
    `driscord_protocol`. Заглушки аудио-устройств — либо честный «unsupported»,
    либо удалить из публичного API.
28. Вынести общую часть voice/screen сессий (`PeerSessionCore`: pc, mutex,
    флаги `started/start_attempted/close_requested`, `on_local_description`,
    `apply_answer`, `add_remote_candidate`, `get_stats<T>`), чтобы правки из
    этапа 1 не приходилось делать дважды. Делать **после** этапа 1, а не вместо.

---

## Verification

- `./scripts/build.sh --test` — весь набор зелёный; затем
  `ctest --repeat until-fail:20` по двум интеграционным тестам: сейчас они
  таймингозависимы, и после этапа 3 это должно перестать быть правдой.
- Дедлоки §1.1–1.3: сборка с `-fsanitize=thread`, прогон
  `test_google_webrtc_screen_transport` + принудительный `stop()` на тестовом
  потоке; до фикса TSan должен показать lock-order inversion.
- §1.4: юнит-тест с `Description::Media`, у которой в `m=`-строке есть PT без
  `a=rtpmap:` — до фикса `std::terminate`.
- §1.9: `./scripts/run.sh --server` + два клиента, начать трансляцию, кликнуть
  плитку — видео должно появиться без перезаполнения модели.
- §2.1: замерить downlink клиента, смотрящего одну трансляцию из трёх активных,
  до и после — ожидаемое падение примерно втрое.
- §2.4: включить шаринг системного звука и нажать deafen — звук трансляции
  должен замолчать; проверить отсутствие самовозбуждения при активном шаринге.
- CI: все job'ы зелёные на чистом форке, включая кэш `libwebrtc.a`.
