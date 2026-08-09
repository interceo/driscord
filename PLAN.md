# Миграция media engine на Google WebRTC

## Решение

Собственные аудио/video playout, packetization и codec orchestration заменяются
Google WebRTC на клиенте. Сервер остаётся лёгким SFU на `libdatachannel::Track`:
он завершает ICE/DTLS/SRTP и маршрутизирует RTP, но не декодирует медиа.

На клиент приходится два логических соединения:

- `voice`: один исходящий микрофон и несколько входящих голосовых tracks;
- `screen`: одна исходящая пара screen video/system audio и несколько входящих
  пар трансляций.

Обе модели используют Unified Plan: один transceiver на независимый track.
Screen video и соответствующий system audio всегда остаются в одном
PeerConnection/MediaStream, чтобы RTCP sync работал внутри одной временной базы.

## Текущий статус миграции (9 августа 2026)

- [x] Выбрана граница: Google WebRTC client + libdatachannel Track SFU.
- [x] Подтверждена поддержка нескольких video tracks и сведения нескольких
  входящих audio streams штатным `AudioMixer`.
- [x] Signaling использует обязательный `connection=voice|screen`; старое
  значение `legacy` и отсутствие поля отвергаются парсером.
- [x] Серверная зависимость libdatachannel обновлена до `v0.24.5`, RTP media
  support включён отдельной CMake-опцией.
- [x] Закреплённый checkout Google WebRTC
  `956083e9a9f487b9c2d0cdb96c64ba23cfc1ac76`: воспроизводимая GN-сборка
  полного `libwebrtc.a` проверена.
- [x] Узкий adapter target: типы Google WebRTC не выходят в публичные
  заголовки `driscord_core` и Qt-клиента.
- [x] Основа `voice`: один local audio track, bounded pool из нескольких
  recvonly transceivers, mute/reconnect и реальный Google WebRTC ↔
  libdatachannel ICE/DTLS/SRTP handshake.
- [x] Voice SFU routing: room-level назначение publisher → subscriber slot,
  стабильный SSRC, `mid -> peer_id`, локальный NACK cache и несколько
  одновременных участников без renegotiation.
- [x] Завершение `voice` vertical slice: штатный clocked
  `TestAudioDeviceModule` с bounded 10-ms PCM queue, два одновременно
  декодируемых remote track по независимым `mid`, spec-compliant RTCStats и
  явная congestion policy (64-kbit/s Opus ceiling, hop-local RR/NACK, без
  ложного объявления непроксируемых transport-cc/REMB).
- [x] `screen` vertical slice: DesktopCapturer/VideoTrackSource, несколько video
  sinks и связанный system-audio track.
- [x] Screen SFU RTP routing: связанные audio/video slots, локальный NACK cache,
  PLI upstream, сохранение track identity и непрерывная RTP timeline при
  переназначении slot другому publisher.
- [x] UI переведён с одного `watchedPeerId` на множество одновременно
  просматриваемых трансляций.
- [x] `watch_start/watch_stop` адресуют конкретный `peer_id`; SFU заполняет
  slots только выбранными publishers и не тратит downlink на остальные.
- [x] Legacy media pipeline удалён по принятому решению: больше нет собственных
  sender/receiver, Opus/FFmpeg orchestration, DataChannel media protocol,
  WSOLA, MediaClock и старых quality/benchmark harnesses.
- [x] Runtime-метрики экрана переведены на стандартный `RTCStatsReport`.
- [x] Screen RTCStats группируются по `mid -> peer_id`; каждая плитка получает
  собственные counters/delay/rate, а параллельные UI polls объединяются в один
  in-flight `GetStats` request. Stateful aggregation вынесена из client
  coordinator в отдельный `ScreenStatsTracker` и покрыта unit-тестами смены
  binding epoch и stale session callback.
- [x] Targeted screen subscriptions автоматически переотправляются после
  signaling reconnect; end-to-end тест подтверждает возобновление кадров без
  ручного повторного `joinStream`.
- [x] Reconnect/churn и детерминированные loss/reorder fault injection проходят
  на реальном RTP пути: voice декодируется при drop каждого 11-го пакета и
  reorder каждого 7-го, screen — при drop каждого 37-го и reorder каждого
  11-го; `RTCStats` подтверждает ненулевые реальные потери.
- [ ] Завершить reliability gate длительной soak-проверкой нескольких
  voice/screen publishers и достижением slot capacity.

Проверка реализованного среза:

- полный pinned `libwebrtc.a` и Qt-клиент с
  `DRISCORD_USE_GOOGLE_WEBRTC=ON` собираются Clang/lld;
- Google WebRTC ↔ libdatachannel handshake, три одновременных voice peer,
  независимые slot bindings и очистка slot при disconnect проходят; focused
  churn-прогон стабилен 10/10;
- два детерминированных PCM tone source проходят реальный
  encode → SRTP → SFU rewrite/fan-out → NetEq decode путь в два remote `mid`;
  outbound/inbound packets/bytes и jitter-buffer emission подтверждены через
  RTCStats, включая loss/reorder режим; focused-прогон стабилен 10/10;
- два одновременно публикуемых screen track проходят реальный
  encode → SRTP → SFU rewrite/fan-out → decode путь; связанные video/audio
  slot bindings сохраняют publisher identity, а video decode подтверждается
  и sink callback, и `RTCStatsReport`; замена publisher PeerConnection в той
  же signaling-сессии и новый RTP epoch проверены, focused-прогон 10/10;
- signaling room isolation и быстрый reconnect проверяются отдельно от media;
  полный актуальный набор 11/11 содержит только тесты реально существующей
  архитектуры, без удалённого legacy quality harness; voice и screen
  fault/reconnect прогоны стабильны 10/10 каждый.

Оставшиеся ограничения, которые нельзя маскировать совместимостью:

- pinned Google WebRTC artifact сейчас поддержан только на Linux x86_64 и
  требует Clang/lld; другие архитектуры, Windows и macOS core build
  заблокированы до появления воспроизводимых артефактов для этих платформ;
- UI пока честно показывает только `System default`: выбор audio device и
  input/output level нужно подключить к native ADM, не возвращая miniaudio
  capture pipeline;
- screen publisher пока отправляет одну video rendition: несколько трансляций
  работают, но simulcast/SVC и выбор low/high слоя SFU ещё не реализованы;
- сеть сейчас использует публичные host candidates SFU и исходящий UDP;
  TURN/TCP/TLS fallback для окружений, блокирующих UDP, ещё не реализован;

## Порядок перехода

1. **Инфраструктура без изменения поведения — выполнено.** Закрепить WebRTC revision и GN
   args, добавить CMake imported target, идентификатор соединения в signaling,
   включить media API libdatachannel на сервере.
2. **Voice end-to-end — выполнено.** Один `voice` PeerConnection на клиента,
   один upstream mic track, заранее выделенные recvonly audio transceivers и
   track binding `mid -> peer_id` от SFU.
3. **Несколько screen shares — выполнено.** Отдельный `screen` PeerConnection,
   bounded pool video/audio slots и подписка на конкретный `peer_id`.
   Simulcast/SVC и low/high layer selection остаются отдельным этапом.
4. **System audio default monitor — выполнено.** Custom ADM/PCM bridge остаётся
   изолированным адаптером; выбор конкретного monitor ещё нужно провести через
   UI. Приватные `cricket::*` API в публичный core не допускаются.
5. **Удаление legacy — выполнено по явному разрешению.** Удалены
   `AudioReceiver`, `VideoReceiver`, `Wsola`, `MediaClock`, `PlayoutPolicy`,
   Opus/FFmpeg orchestration и media DataChannels.

Fallback намеренно отсутствует: Google WebRTC является единственным client
media backend. Дальнейшие изменения обязаны сохранять собираемый вертикальный
срез и проверяться реальным client ↔ SFU integration path.

`GoogleWebRtcRuntime` остаётся классом, потому что владеет фабрикой, потоками и
порядком их остановки (RAII). Аналогов старых `AudioSender/AudioReceiver` и
`VideoSender/VideoReceiver` в новом backend не создаём: PCM/video входят через
штатные source/track API, выходят через sink API, а `VoiceSession` и
`ScreenSession` координируют только signaling, подписки и привязку `mid`.

Linux backend собирается Clang с `use_custom_libcxx=false`: WebRTC и остальной
процесс используют одну libstdc++ ABI. Для закреплённой ревизии хранится один
явный patch-файл с тремя узкими дельтами: `nullptr_t` → `std::nullptr_t`,
совместимый с libstdc++ вызов `optional::emplace` и включение штатных default
media factories вместе с официальным `TestAudioDeviceModule` в полный archive
target. Подключение libc++ рядом с Qt/core не допускается. В совместном
Google-WebRTC тестовом/клиентском процессе
libdatachannel собирается с GnuTLS: это исключает interposition одинаковых
OpenSSL/BoringSSL C-символов. Его libSRTP использует NSS для AES-GCM и
изолирован внутри `libdatachannel.so`, поэтому два набора process-global
`srtp_*`-символов также не смешиваются. Самостоятельный server build от этих
ограничений не зависит.

---

# Исторический план (не исполняется): legacy MVP проверки качества

Раздел ниже сохранён только как журнал причин миграции. Упомянутые в нём
`AudioReceiver`, `VideoReceiver`, `MediaClock`, custom network model и старые
benchmarks удалены и не являются частью текущей архитектуры или test gate.

## Context

Сейчас единственный способ узнать, что трансляция «поехала» — запустить клиент и слушать/смотреть глазами. Фундамент в коде есть: `AudioReceiver::Stats` / `VideoReceiver::Stats` (`conceal_count`, `late_count`, `p50/p95/p99_delay_ms`, `playout_ts_us`), детерминированные тесты `MediaClock`, `test_util::NetworkConditioner`, сквозной `test_av_sync.cpp::ReceiversStayAlignedEndToEnd`. Чего не хватает:

1. **Детерминизма.** `utils::MonoClock` статический (`core/src/utils/mono_clock.hpp:17`) → любой тест плейаута идёт в реальном времени. `test_av_sync` тратит ~3 с wall-clock, `scripts/build.sh` вынужден гонять ctest с `--repeat until-pass:3`, а `core/tests/CMakeLists.txt:87` — с `RUN_SERIAL TRUE`. Тест, который иногда падает, регрессии не ловит.
2. **Метрик качества.** Есть счётчики событий, нет ответа «насколько плохо».
3. **Воспроизводимости.** `NetworkConditioner` сеется из `std::random_device` (`net_cond.hpp:286`).
4. **Способа сравнить алгоритмы.** Расчёт `target_delay` зашит в `MediaClock::recompute` (`core/src/sync/media_clock.cpp:47-107`).

Цель MVP: детерминированные сценарии, прогоняющие минуты медиа за секунды через модель сети и выдающие числа — A/V-скос, PSNR/SSIM, freeze, expand_rate, глитчи — с бюджетами, падающими в CI.

### Принцип: берём готовое

Проверено на этой машине — обе библиотеки доступны:

| Задача | Готовое решение | Статус |
|---|---|---|
| PSNR / SSIM по видео | **libavfilter** фильтры `psnr`, `ssim` (FFmpeg уже зависимость проекта) | `pkg-config libavfilter` → 11.14.102, фильтры `psnr`/`ssim`/`libvmaf`/`identity` присутствуют |
| Альтернативный буфер джиттера для A/B | **speexdsp** `JitterBuffer` (`speex/speex_jitter.h`) — зрелая BSD-реализация адаптивного буфера | `pkg-config speexdsp` → 1.2.1 |
| Тест-фреймворк / бенчи / JSON | GTest 1.15.2, Google Benchmark 1.9.1, nlohmann/json 3.11.3 | уже в проекте |
| Эмуляция сети «в поле» | `scripts/net_emulate.sh` (tc/netem) | уже есть, остаётся для ручных проверок |

Пишем сами только то, для чего готового лёгкого варианта нет: виртуальные часы (~20 строк), детерминированная модель доставки пакетов, аудио-метрики (seg-SNR + детект разрывов). ViSQOL для аудио-MOS существует, но тянет Bazel и TFLite — вне MVP.

### Что взято из чужих подходов

- `webrtc::TimeController` (simulated mode), `GstTestClock` — виртуальное время как основа тестов джиттер-буфера.
- `DefaultVideoQualityAnalyzer` (WebRTC PC-level e2e) — матчинг 1:1 «снято ↔ отрендерено» по frame id. У нас `FrameHeader::frame_id` уже на проводе, матчинг бесплатный.
- NetEq `GetStats` — `expand_rate` / `accelerate_rate` / `preemptive_rate` как доли **сэмплов**, не событий.
- ITU-R BT.1359 — пороги lip-sync, уже зафиксированы в `test_av_sync.cpp:50-51`.

---

## Текущий статус (обновлено по ходу реализации)

### Готово и проверено

| Что | Проверка |
|---|---|
| `utils::TimeSource` + инъекция в `AudioReceiver`, `VideoReceiver`, `ScreenReceiver` | 18/18 нативных тестов зелёные **без единой правки в файлах тестов**; Qt-клиент собирается (exit 0) |
| `vector_view` → `std::span` | header и `test_vector_view.cpp` удалены, 15 файлов переведены |
| `byte_utils` → Boost.Endian (`load/store_little_u32/u64`) | header и `test_byte_utils.cpp` удалены; `test_protocol` (414 строк на wire-формат) зелёный — байты идентичны |
| `PcmRing` → `boost::circular_buffer<float>` | 73 строки удалены, осталось два хелпера по 5 строк в `audio.cpp` |
| Boost заведён в кросс-сборку MinGW | `fetch_win_deps.sh` тянет `mingw-w64-x86_64-boost-1.91.0-3`, тулчейн добавляет `third_party/windows/boost` в `CMAKE_FIND_ROOT_PATH`, `find_package(Boost)` переехал в корневой CMakeLists |
| Сэмпловые счётчики в `AudioReceiver::Stats` | `total_samples_out`, `conceal_samples`, `fec_samples`, `silence_samples`, `stretch_in/out_samples` |
| Шов `PlayoutPolicy` + `DefaultPlayoutPolicy` | логика перенесена без изменений; `test_media_clock` и `test_av_sync` зелёные без правок |
| `FrameComparator` на libavfilter `psnr`/`ssim` | отдельная проверка: идентичные кадры → PSNR 100 (клампится с `inf`) / SSIM 1.0; зашумлённые → 30.2 dB / 0.778 |
| Харнесс: `sim_clock`, `media_gen`, `net_model`, сеяный `NetworkConditioner` | — |
| Анализаторы `quality_metrics.hpp`, драйвер `quality_scenario.hpp` | — |
| `alt_policies.hpp`: `FixedDelayPolicy`, `EwmaSpikePolicy` (Ramjee alg-4 + Moon spike detection) | — |
| `test_media_quality.cpp`, `test_playout_policies.cpp`, хелпер `add_quality_test` | собираются и запускаются |
| **Детерминизм** | `MediaQuality.RunsAreReproducible` зелёный: два прогона с одним сидом дают побайтово равный JSON |
| **Скорость** | 20 с медиа за 2–4 с на сценарий |

### Починено попутно (пред-существующие поломки, не связанные с задачей)

- **`--bench` не собирался**: `bench_protocol` не линковал `driscord_protocol`. CI бенчмарки не гоняет, поэтому это не всплывало.
- **`--windows --test` не выполнял ни одного теста**: все 14 бинарников падали за 0.02 с на ненайденных `libgtest.dll` — каталоги с DLL из дерева сборки не попадали в `WINEPATH`, а путь к libgcc вычислялся как `.../16` при реальном каталоге `16.1.0`. После починки — 11/11 под Wine за 1.95 с. Из прогона под Wine исключены `test_video_codec`, `test_video_receiver` (программный FFmpeg не укладывается в таймаут) и `test_audio_receiver` (assert на wall-clock; нативно зелёный).

### В работе: бюджеты качества пока не проходят

Первый прогон дал числа, которые ловят ошибки **харнесса**, а не пайплайна. Диагноз (правки внесены, пересборка и перепроверка ещё не выполнены):

1. **`expand_rate ≈ 0.357` одинаков на clean, degraded, bad и voice_only.** Генератор делал паузы, «отправитель» их не слал (эмуляция noise gate), а приёмник до прихода следующего талкспёрта успевал выдать PLC. Поведение реальное, но метрика приписывала его потерям. → Источник сделан непрерывным; пробел теперь действительно означает потерю, а талкспёрты заслуживают отдельного теста.
2. **PSNR 18 dB / SSIM 0.33 на чистом линке.** `VideoEncoder::encode(кадр_i)` может вернуть битстрим более раннего кадра, а пакет штамповался текущим timestamp — контент и `frame_id` разъезжались, и PSNR сравнивал кадр с соседним. → Введена FIFO-очередь id: выдаются в порядке подачи, потребляются в порядке выхода.
3. **`max_abs_skew ≈ 1000 мс`.** Скос сэмплировался и во время фризов, когда последний показанный кадр стареет, а аудио идёт. → Сэмплируется только в момент показа нового кадра.
4. **Стартовый транзиент считался фризом.**

### Не начато

- `--quality` в `scripts/build.sh` и шаг в CI (`libavfilter-dev` в apt, выгрузка JSON артефактом).
- Финальная верификация: fault injection (`kMinDelayUs = 0`, отключённый FEC, target только по аудио) — проверка, что бюджеты действительно падают; `--repeat until-fail:20`.
- Замены `Wsola` → SoundTouch и `ReorderBuffer` → speexdsp (гейтятся готовым харнессом).

### Поправка к плану: `SpeexJitterPolicy`

Обёртки над speexdsp `JitterBuffer` в виде политики не будет. Её API владеет расписанием извлечения пакетов (`jitter_buffer_put`/`_get`/`_tick`), а шов `PlayoutPolicy` отвечает лишь на вопрос «какой должна быть общая задержка». Втиснуть одно в другое можно только теневым буфером, который дублирует поток пакетов ради одного числа. Полноценная подмена speexdsp — это и есть медиа-замена из пункта C, которая гейтится харнессом. Вместо неё в A/B добавлен `EwmaSpikePolicy` — Ramjee'94 alg-4 со spike-детектором Moon'98, published-алгоритм, а не самодельный.

---

## Порядок работ

Замены самописных утилит на библиотечные идут вокруг харнесса, а не вместо него:

**A. Безрисковые замены — сделано.**

| Своё | Замена | Итог |
|---|---|---|
| `utils::vector_view` | `std::span` | ✅ header + 62 стр. тестов удалены |
| `utils::byte_utils` | Boost.Endian (`load/store_little_u32/u64`) | ✅ header + 121 стр. тестов удалены |
| `PcmRing` | `boost::circular_buffer<float>` | ✅ 73 стр. удалены, осталось 2 хелпера по 5 строк |
| `avsync::DelayEstimator` | — | ❌ откачено, см. ниже |

Boost пришлось завезти в кросс-сборку: тулчейн MinGW ставит `CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY`, поэтому хостовый `BOOST_ROOT` из CI ей не виден. Решено в стиле проекта — `fetch_win_deps.sh` тянет `mingw-w64-x86_64-boost` (там же лежит `BoostConfig.cmake`), тулчейн добавляет `third_party/windows/boost` в `CMAKE_FIND_ROOT_PATH`. `find_package(Boost)` переехал из `backend/signaling_server/` в корневой `CMakeLists.txt`, `driscord_protocol` линкует `Boost::boost` публично.

**Почему `DelayEstimator` остался своим.** У `boost::accumulators` нет ни rolling-минимума, ни rolling-перцентиля — только `rolling_mean`/`rolling_variance`/`rolling_sum`; `tag::min` и `extended_p_square` считают по всей истории и не забывают, что ломает `WindowForgetsOldSamples`. Вариант на `boost::circular_buffer` + `nth_element` прошёл все 10 тестов, но замерился так:

| Бенчмарк | Своё | circular_buffer |
|---|---|---|
| `BM_MediaClock_Observe` | 17.3 нс | 5890 нс (×340) |
| `BM_DelayEstimator_Percentile` | 18.2 нс | 1622 нс (×89) |

Гистограмма выходит из цикла на первом ведре, где набрался p95; полный скан окна на каждый пакет этого не умеет. `MediaClock::observe` зовётся на каждый пакет, так что размен «40 строк кода за ×340 по горячему пути» невыгоден.

Попутно починен `--bench`: `bench_protocol` не линковал `driscord_protocol` и не собирался; CI бенчмарки не гоняет, поэтому это не всплывало.

**B. Харнесс качества** — шаги 1–4 ниже.

**C. Медиа-замены, гейтятся харнессом.** `Wsola` → **SoundTouch** 2.4.1, `ReorderBuffer` → **speexdsp** `JitterBuffer` 1.2.1. Каждая — с отчётом до/после по `expand_rate`, `glitch_count` и A/V-скосу. Обе тянут компилируемые библиотеки: нужен `libsoundtouch-dev` / `libspeexdsp-dev` в CI и решение для `third_party/windows`.

Почему не наоборот: `Wsola` держит A/V-сцепку через бюджет растяжения, а speexdsp сам владеет решением о задержке и заберёт таймлайн, на котором построена синхронизация с видео. Без харнесса «стало не хуже» проверить нечем.

Не заменяем: `utils::BoundedQueue` (write-queue сессии в `ws_server.cpp` пишется несколькими потоками libdatachannel — не SPSC), `MonoClock`, `SpinLock`, `match`, `Expected`.

---

## Шаг 1. Швы в прод-коде (минимально необходимые)

### 1.1 Источник времени — `core/src/utils/mono_clock.hpp`

```cpp
namespace utils {

// Монотонный источник микросекунд. Существует затем, чтобы плейаут можно
// было прогнать в симулированном времени.
class TimeSource {
public:
    virtual ~TimeSource() = default;
    virtual int64_t now_us() const noexcept = 0;
};

const TimeSource& system_time_source() noexcept; // поверх MonoClock, живёт вечно

} // namespace utils
```

Прокинуть последним параметром конструктора (по умолчанию `system_time_source()`), хранить как `const TimeSource*`:

- `AudioReceiver` (`core/src/audio/audio.hpp:100`) → `push_packet` (`audio.cpp:257`), `playout_step` (`audio.cpp:306`).
- `VideoReceiver` (`core/src/video/video.hpp:71`) → `push_video_packet` (`video.cpp:197`), `update` (`video.cpp:280`), **и** `utils::Now()`-бухгалтерия `last_packet_` / `last_calc_` / `last_keyframe_req_` — иначе `measured_kbps` и троттлинг keyframe-запросов останутся недетерминированными.
- `ScreenReceiver` / `ScreenSession` — проброс в создаваемые ресиверы.

Цена: один виртуальный вызов на пакет и на `read()`. На фоне Opus-декода — шум.

**Вне MVP:** `VideoSender::encode_loop`, `AudioSender::on_capture`, `AudioMixer::on_playback`. Сценарии кодируют медиа напрямую через `OpusEncode`/`VideoEncoder` (как уже делает `test_av_sync.cpp:346-382`), отправители не нужны.

### 1.2 Сэмпловые счётчики в `AudioReceiver::Stats` (`core/src/audio/audio.hpp:134`)

Для NetEq-подобных долей нужны сэмплы, а не события:

```cpp
uint64_t total_samples_out = 0;   // всё, что ушло в read()
uint64_t conceal_samples = 0;     // из PLC
uint64_t fec_samples = 0;
uint64_t silence_samples = 0;     // underrun-тишина
uint64_t stretch_in_samples = 0;  // вход WSOLA
uint64_t stretch_out_samples = 0; // выход WSOLA
```

Отсюда `expand_rate`, `accelerate_rate`, `preemptive_rate`.

### 1.3 Шов для политики плейаут-задержки

Новый `core/src/sync/playout_policy.hpp` / `.cpp`:

```cpp
namespace avsync {

struct StreamObservation {
    bool ready = false;
    int64_t min_owd_us = 0;
    int64_t variation_us = 0; // p95 из DelayEstimator
};

// Параметры дефолтной политики: sync_defaults — constexpr, а свипы должны
// идти без перекомпиляции.
struct DelayParams {
    int64_t min_delay_us = sync_defaults::kMinDelayUs;
    int64_t max_delay_us = sync_defaults::kMaxDelayUs;
    int64_t margin_us = sync_defaults::kDelayMarginUs;
    int64_t decay_step_us = sync_defaults::kDelayDecayStepUs;
    int64_t decay_interval_us = sync_defaults::kDelayDecayIntervalUs;
    int64_t video_floor_us = stream_defaults::kScreenBufferMs * 1000;
};

class PlayoutPolicy {
public:
    virtual ~PlayoutPolicy() = default;
    virtual int64_t target_delay_us(const StreamObservation& audio,
        const StreamObservation& video, bool video_active, int64_t now_us) = 0;
    virtual void reset() noexcept = 0;
};

std::unique_ptr<PlayoutPolicy> make_default_policy(DelayParams = {});

} // namespace avsync
```

`MediaClock` получает опциональный параметр конструктора `std::unique_ptr<PlayoutPolicy>`. Тело `recompute` (`media_clock.cpp:68-107`) переезжает в `DefaultPolicy` **без изменения поведения**; расчёт `offset` (`media_clock.cpp:47`) остаётся в `MediaClock` — это свойство пары машин, а не политика.

Критерий неизменности: `test_media_clock` и `test_av_sync` проходят без правок.

---

## Шаг 2. Тестовый харнесс — `core/tests/support/`

Каталог уже на include-пути тестов и бенчмарков (`TEST_INCLUDE_DIRS`, `BENCH_INCLUDE_DIRS`).

### 2.1 `sim_clock.hpp`
`test_util::SimClock : utils::TimeSource` — `now_us()`, `advance(us)`, `advance_to(us)`. Однопоточный, без атомиков.

### 2.2 `media_gen.hpp` — общие генераторы
Синус 220 Гц сейчас скопирован в 4 файла, `make_bgra` — в 3, `SuppressLogs` — в 3. Свести в один заголовок и переключить существующие тесты/бенчи:

- `sine(hz, frames, channels, amp)` — из `test_wsola.cpp:14`.
- `speech_like(frames, seed)` — гармоники с огибающей и паузами, чтобы `kTalkspurtStart` и подавление PLC на границе талкспёрта реально работали.
- `bgra_pattern(frame_idx, w, h)` — **движущийся** градиент + детерминированный псевдошум. Критично: на однотонном кадре (как в `test_video_codec.cpp:16`) PSNR бесконечен и метрика бессмысленна.
- `encode_audio_packet()`, `wrap_video_packet()` — поднять из `test_av_sync.cpp:280-319`.
- `SuppressLogs`.

### 2.3 `net_model.hpp` — детерминированная доставка
`net_cond.hpp` не трогаем (он нужен интеграционным тестам поверх настоящего транспорта). Рядом — версия без потоков и wall-clock, переиспользующая существующий `NetProfile` и его пресеты `clean/degraded/bad/terrible`:

```cpp
class NetworkModel {
public:
    NetworkModel(NetProfile p, uint64_t seed);   // сид обязателен
    void send(int64_t now_us, const uint8_t* data, size_t len, uint64_t id);
    template <class F> void deliver_due(int64_t now_us, F&& on_packet);
    void set_profile(NetProfile);
    Stats stats() const;
};
```

Плюс явный сид в `NetworkConditioner` вместо `std::random_device` (с дефолтом — существующие тесты не заметят).

**Вне MVP:** capacity/queue-limit (token bucket), Gilbert—Elliott burst loss, replay трейса из CSV.

### 2.4 `video_quality.hpp` — PSNR/SSIM через libavfilter

Обёртка над готовым фильтрграфом, ничего своего не считаем:

```cpp
// buffer(ref) ─┐
//              ├─> psnr ─> ssim ─> buffersink   (метрики в AVFrame side data)
// buffer(dist)─┘
class FrameComparator {
public:
    FrameComparator(int w, int h);          // строит граф "psnr,ssim"
    void add_pair(const uint8_t* ref_bgra, const uint8_t* dist_rgba);
    Result finish();  // psnr_avg_db, psnr_p05, ssim_all, ssim_p05, n
};
```

Читаем `lavfi.psnr.psnr_avg` и `lavfi.ssim.All` из метаданных выходных кадров (`av_dict_get(frame->metadata, ...)`). Конверсия BGRA/RGBA → YUV — уже используемым `swscale`.

Сборка: добавить `libavfilter` в pkg-config-блок `core/CMakeLists.txt`, линковать **только к тестовому таргету**, `driscord_core` не трогаем.

### 2.5 `quality_metrics.hpp` — остальные анализаторы

- **`VideoQualityAnalyzer`** — матчинг 1:1 по `frame_id`, отдаёт пары в `FrameComparator`, плюс сам считает то, чего в libavfilter нет: `freeze_count` / `freeze_total_ms` / `max_freeze_ms` (интервал между рендерами > 3× номинала), `harmonic_fps`, `p95_inter_frame_ms`, `dropped_ratio` (снято, но не отрендерено), `time_to_first_frame_ms`, `transport_ms` p50/p95.
- **`AudioQualityAnalyzer`** — выравнивание эталона и выхода кросс-корреляцией по окну лагов (плейаут-задержка заранее неизвестна), затем `seg_snr_db` (mean/p05), `glitch_count` — скачок между соседними сэмплами выше порога относительно локального RMS (техника уже есть в `test_wsola.cpp`, тесты `*IntroducesNoDiscontinuity`), `silence_ratio`, и `expand_rate` / `accelerate_rate` / `preemptive_rate` / `fec_recovery_rate` / `underrun_rate` из расширенного `Stats`.
- **`SyncAnalyzer`** — `audio.stats().playout_ts_us` против `sender_ts_us` последнего отрендеренного кадра: `median_skew_ms`, `p05`, `p95`, `max_abs_drift_ms`, `recovery_ms` (возврат в допуск после фазы-шторма). Пороги — ITU-R BT.1359 из `test_av_sync.cpp:50-51`.

### 2.6 `quality_scenario.hpp` — драйвер

```cpp
struct Phase { int64_t duration_us; NetProfile audio_net, video_net; };
struct Scenario {
    std::string name;
    std::vector<Phase> phases;
    bool video_enabled = true;
    int width = 320, height = 240, fps = 30;
    uint64_t seed = 1;
};
QualityReport run_scenario(const Scenario&,
    const std::function<std::unique_ptr<avsync::PlayoutPolicy>()>& policy = {});
```

Один поток, один `SimClock`, шаг 1 мс:
1. Оба потока кодируются один раз заранее (как `test_av_sync.cpp:356-382`), чтобы стоимость энкодера не попала в модель.
2. На шаге: `net.deliver_due(now)` → `push_packet` / `push_video_packet`; «звуковая карта» тянет `frames_due = (now - start) * 48000 / 1e6` и вызывает `read()`; `video.update(cb)` на частоте кадров; анализаторы получают эталон и выход.
3. `QualityReport::to_json()` через nlohmann/json — метрики, имя сценария, сид, профиль.

### 2.7 `alt_policies.hpp` — альтернативы для A/B
- `FixedDelayPolicy(ms)` — наивная база, ~15 строк.
- `SpeexJitterPolicy` — обёртка над **speexdsp** `JitterBuffer` (`jitter_buffer_init` / `_put` / `_get` / `_update_delay`), берём её оценку задержки как target. Готовая реализация, свой NetEq не пишем.

Обе — только в тестовом коде.

---

## Шаг 3. Тесты — `core/tests/quality/`

В `core/tests/CMakeLists.txt` — новый хелпер (существующие `add_unit_test`/`add_core_test` хардкодят префикс `unit/`):

```cmake
function(add_quality_test name)
    add_executable(${name} quality/${name}.cpp)
    target_include_directories(${name} PRIVATE ${TEST_INCLUDE_DIRS})
    target_compile_options(${name} PRIVATE ${TEST_WARNING_FLAGS})
    target_link_libraries(${name} PRIVATE GTest::gtest_main driscord_core
        PkgConfig::LIBAVFILTER)
    add_test(NAME ${name} COMMAND ${name})
endfunction()
```

Намеренно **без** гейта `BUILD_SERVER` и **без** `RUN_SERIAL` — тесты детерминированные и параллелятся.

### `test_media_quality.cpp`
Сценарии MVP: `clean`, `degraded`, `bad`, `terrible`, `delay_spike` (ступень +300 мс и возврат), `voice_only`.

Бюджеты — одной таблицей в начале файла, чтобы ужесточение было однострочным диффом. Ориентир для `clean`:

| Метрика | Бюджет |
|---|---|
| `expand_rate` | < 0.005 |
| `glitch_count` | 0 |
| `psnr_avg_db` | > 35 |
| `freeze_count` | 0 |
| `median_skew_ms` | \|·\| < 20 |
| `p95_delay_ms` (audio) | < 90 |

Для `bad`/`terrible` бюджеты ослабляются, но остаются жёсткими на том, что не должно ломаться никогда: `max_abs_drift_ms`, `recovery_ms`, отсутствие монотонного роста задержки.

### `test_playout_policies.cpp`
Один сеяный трейс через `DefaultPolicy`, `FixedDelayPolicy(60/120/200)`, `SpeexJitterPolicy`. Утверждение: **дефолт не доминируется** — нет альтернативы, которая на том же трейсе даёт одновременно и меньший `expand_rate + glitch_count`, и меньшую среднюю задержку. Плюс печать таблицы: тест работает и как инструмент принятия решений.

### Переезд
`test_av_sync.cpp::ReceiversStayAlignedEndToEnd` (`core/tests/integration/test_av_sync.cpp:329-470`) становится сценарием `clean` и удаляется из integration. Первые шесть тестов (чистая симуляция `MediaClock`) остаются — они уже детерминированные. Это убирает главный источник flake в CI.

---

## Шаг 4. Сборка и CI

- `scripts/build.sh`: действие `--quality` (третья ось рядом с `--test`/`--bench`) — собирает и гоняет `test_media_quality` + `test_playout_policies`, печатает таблицу, пишет JSON.
- Бинарники принимают `--quality-report=<path>`.
- `core/CMakeLists.txt`: `libavfilter` в pkg-config-блоке; `speexdsp` — опционально (`if(NOT SPEEXDSP_FOUND)` → `SpeexJitterPolicy` выключается, тест политик её пропускает).
- `.github/workflows/ci.yml`: в apt-список job `tests` добавить `libavfilter-dev libspeexdsp-dev`; шаг `./scripts/build.sh --quality`.
- `--windows --test`: добавить `test_media_quality|test_playout_policies` в `-E`-регэксп — прибилды в `third_party/windows/ffmpeg` не гарантируют avfilter, а speexdsp под MinGW нет.

---

## Явно вне MVP (следующая итерация)

Capacity/queue-limit и Gilbert—Elliott в модели сети; replay трейса из CSV (аналог `neteq_rtpplay`); ViSQOL/VMAF для перцептивных MOS; швы в `AudioSender`/`AudioMixer`/`VideoSender`; параметрические свипы `DelayParams`; загрузка JSON-отчётов артефактами CI и сравнение с baseline; починка пути кэша FetchContent в CI (`linux-release/_deps` vs реально используемый `linux-test`).

---

## Критические файлы

**Прод:** `core/src/utils/mono_clock.hpp`; `core/src/audio/audio.{hpp,cpp}`; `core/src/video/video.{hpp,cpp}`; `core/src/video/screen.hpp`, `screen_session.hpp`; `core/src/sync/media_clock.{hpp,cpp}`; `core/src/sync/playout_policy.{hpp,cpp}` (новый); `core/CMakeLists.txt`.

**Тесты:** `core/tests/support/{sim_clock,media_gen,net_model,video_quality,quality_metrics,quality_scenario,alt_policies}.hpp`; `core/tests/quality/{test_media_quality,test_playout_policies}.cpp`; `core/tests/CMakeLists.txt`; `scripts/build.sh`; `.github/workflows/ci.yml`; `PLAN.md`.

**Переиспользуем как есть:** `utils::ReorderBuffer`, `avsync::DelayEstimator`, `Wsola`, `PcmRing`, `VideoEncoder`/`VideoDecoder`, `NetProfile` и его пресеты, `wait_helpers.hpp`, `utils::Counter`/`Gauge`.

---

## Verification

1. **Регрессии нет.** `./scripts/build.sh --test` — все существующие тесты зелёные **без правок в них**. Это и есть доказательство, что инъекция времени и вынос политики поведение не изменили.
2. **Детерминизм.** `ctest -R test_media_quality --repeat until-fail:20`; `diff` JSON-отчётов двух прогонов пустой. Расхождение = где-то остался wall-clock или несеяный RNG.
3. **Скорость.** `test_media_quality` целиком — единицы секунд при ~60 с симулированного медиа на сценарий. Дольше → модель где-то спит.
4. **Метрики ловят реальные поломки.** Проверить на заведомо сломанном коде (временные правки, откатить после):
   - `kMinDelayUs = 0` → `expand_rate` и `underrun_rate` пробивают бюджет на `degraded`;
   - отключить FEC-ветку в `AudioReceiver::decode_into` (`audio.cpp:518`) → `fec_recovery_rate` в ноль, `glitch_count` вверх на `bad`;
   - считать `target_delay` только по аудио → `median_skew_ms` вылетает за BT.1359.
   Если бюджет не падает — метрика или сценарий бесполезны; чинить их, а не ослаблять порог.
5. **A/B работает.** `./scripts/build.sh --quality` печатает таблицу политик; `FixedDelayPolicy(60)` проигрывает по глитчам, `FixedDelayPolicy(200)` — по задержке. Одинаковые числа у всех → политика не подключилась.
6. **Кросс-платформенно.** `./scripts/build.sh --windows --test` собирается и проходит.
7. **Живой клиент работает.** `./scripts/run.sh --server` + `./scripts/run.sh`, поделиться экраном, посмотреть строку `stream_stats` (`core/src/video/screen_session.cpp:74-121`) — счётчики в тех же диапазонах, что до изменений.
