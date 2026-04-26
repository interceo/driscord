# План: улучшение качества аудио в ядре Driscord

## Контекст

Оценка ядра показала, что медиапайплайн в целом зрелый (Opus/Opus-FEC-decode,
джиттер-буфер с PLC, A/V-sync по wall-ts, H.264/HEVC с chunking), но
**аудио-DSP на захвате полностью отсутствует**: нет шумоподавления, нет
эхоподавления, VAD подменён статическим noise-gate, а Opus-FEC работает только
на приём. На практике это означает, что фоновый шум, эхо из динамиков и
пакетные потери портят звонок сильнее необходимого.

Решение:
- Трек **A — качество медиа** (VAD, RNNoise, AEC, Opus FEC send) как первый.
- Допустимо тянуть **новые зависимости** через CMake FetchContent.
- **Обратная совместимость** wire-протокола не важна — клиенты обновятся вместе.

## Что будет сделано

Четыре под-фичи, реализуются **в порядке зависимостей**:

### 1. RNNoise — шумоподавление + источник VAD (A1)

**Зависимость**: `xiph/rnnoise` через FetchContent (BSD-3, C, ~200 KB + встроенная модель). CMake-совместим.

**Интеграция**: новый RAII-враппер `core/src/audio/rnnoise_denoiser.{hpp,cpp}` с API:
```cpp
class RnnoiseDenoiser {
public:
    RnnoiseDenoiser();
    // in/out: 480 float samples (10ms @ 48 kHz mono), возвращает VAD prob [0..1]
    float process(const float* in, float* out);
};
```
Встроить в `AudioSender` (`core/src/audio/audio.cpp`) перед Opus encode:
- В `on_capture()` накопитель `capture_buf_` собирает 960 float-сэмплов (20 ms).
- Перед энкодом два прохода RNNoise по 480 сэмплов, VAD-probs усредняем.
- Пропускаем очищенные сэмплы в Opus.
- Добавляем toggle: `AudioSender::set_noise_suppression(bool)` (дефолт on).

**Тонкости**:
- MiniAudio отдаёт float32 (`ma_format_f32`), проверить и подтвердить.
- RNNoise ожидает амплитуду в диапазоне ±32767 (он внутренне нормирует), нужна конвертация если пишем в нормализованном [-1..1].
- Не делать на мьют/deaf — экономия CPU.

### 2. VAD с гистерезисом вместо статического noise-gate (A3)

**Использует VAD prob из RNNoise** — бесплатно с A1.

**Изменения** в `AudioSender` (`core/src/audio/audio.cpp`):
- Удалить текущее поле `noise_gate_threshold_` как основной механизм.
- Добавить state-machine: `VadState { Closed, Opening, Open, Hangover }`.
- Параметры: `open_threshold=0.6`, `close_threshold=0.3`, `open_frames=2`, `hangover_ms=200`.
- Когда `Closed` → Opus encode пропускается, DataChannel send не выполняется (экономим трафик).
- API: `set_vad_enabled(bool)`, `set_vad_thresholds(open, close)` — пробросить в JNI и Qt bridge.
- Сохранить текущий `set_noise_gate(float)` как legacy fallback, если VAD выключен.

### 3. Opus in-band FEC на передаче (A4)

**Правки в `RobustOpusEncoder`** (`core/src/audio/audio.cpp` или аналог):
- При создании энкодера вызвать `opus_encoder_ctl(enc, OPUS_SET_INBAND_FEC(1))`.
- Выставить начальный `OPUS_SET_PACKET_LOSS_PERC(10)` (10% — разумный дефолт).
- Добавить `set_expected_loss(int pct)` — динамически обновляемый через control-канал в будущем. На текущем шаге достаточно статики.
- В `AudioHeader` можно не трогать — inband FEC уносится в Opus-пакете.

**Проверка**: receiver уже умеет декодировать FEC (`RobustOpusDecoder` делает fec=1 path) — без изменений.

### 4. Echo cancellation через Speex DSP (A2)

**Почему Speex, а не WebRTC APM**: WebRTC AEC3 качественнее, но это многофайловый C++ модуль из tree WebRTC — сложно vendor'ить. Speex DSP — одна библиотека (~50 KB), MIT, есть в package-менеджерах, FetchContent возможен через стабильную мирру.

**Зависимость**: `xiph/speexdsp` (MIT).

**Ограничение Speex AEC**: работает на 16 kHz лучше всего; на 48 kHz качество хуже, но приемлемо. Альтернатива — down/upsample только в AEC-тракте.

**Архитектура**:
- Новый класс `core/src/audio/echo_canceller.{hpp,cpp}`:
  ```cpp
  class EchoCanceller {
  public:
      EchoCanceller(int sample_rate, int frame_size, int filter_length_ms);
      // capture: что взял микрофон; reference: что только что отдали динамику
      void process(const int16_t* capture, const int16_t* reference, int16_t* out);
  };
  ```
- `AudioMixer` (`core/src/audio/audio_mixer.cpp`) должен **публиковать последний смикшированный буфер** как reference-сигнал в thread-safe slot (одинарный ring-buffer или SPSC очередь на `utils::slot_ring`).
- `AudioSender` в `on_capture()` забирает reference-буфер, прогоняет capture+ref через AEC **до** RNNoise.
- Делать **opt-in**: `set_aec_enabled(bool)` (дефолт off), т.к. при использовании наушников AEC только ухудшает звук.

**Порядок обработки в on_capture()**:
```
mic samples → AEC (если enabled) → RNNoise (если enabled) → VAD gate → Opus encode
```

---

## Критические файлы

- `CMakeLists.txt` (root) — FetchContent для rnnoise и speexdsp, линковка к `driscord_core`.
- `core/src/audio/audio.hpp` — расширить AudioSender: флаги, thresholds, отчёт VAD-level наружу.
- `core/src/audio/audio.cpp` — интеграция всех четырёх модулей в `on_capture()`.
- `core/src/audio/audio_mixer.hpp` / `.cpp` — публикация reference-буфера.
- `core/src/audio/rnnoise_denoiser.{hpp,cpp}` — **новый**.
- `core/src/audio/echo_canceller.{hpp,cpp}` — **новый**.
- `core/src/audio/vad_gate.{hpp,cpp}` — **новый** (state-machine, использует VAD prob).
- `client-qt/src/app/DriscordBridge.{h,cpp}` — Q_INVOKABLE для NS/VAD/AEC.

## Что переиспользуем (не изобретаем заново)

- `utils::Expected` для ошибок инициализации (`core/src/utils/expected.hpp`).
- `utils::slot_ring` для SPSC reference-буфера AEC (`core/src/utils/slot_ring.hpp`).
- `utils::ma_device` для захвата (`core/src/utils/ma_device.hpp`) — без изменений.
- Существующий `RobustOpusEncoder`/`RobustOpusDecoder` — только флаги FEC, без рефакторинга.

---

## Верификация

### Юнит-уровень
- Добавить микро-тесты в `core/test/` (если папка есть — проверить) на:
  - RnnoiseDenoiser: подать синус + белый шум, замерить SNR до/после.
  - VadGate: последовательность VAD-probs, проверить ожидаемые переходы состояний.
  - EchoCanceller: скормить известный reference + задержанную копию, проверить что хвост подавляется.
- Прогон: `./scripts/build.sh --test`.

### Интеграционный прогон
1. Собрать: `./scripts/build.sh --qt`.
2. Запустить signaling: `./scripts/run.sh --server`.
3. Два клиента: `./scripts/run.sh --qt` × 2.
4. **NS**: на одной стороне включить фен/вентилятор рядом с микрофоном, на другой подтвердить, что с NS вкл шум снят, с выкл — слышен.
5. **VAD**: включить VAD, проверить в `audio_transport` stats, что packets_sent_ не растёт при молчании.
6. **Opus FEC**: через `tc qdisc add dev lo root netem loss 10%` — сравнить разборчивость речи до/после A4.
7. **AEC**: без наушников, с колонками. Говорящий A слышит собственный голос из микрофона B? Проверить с AEC off → on.

### Ручная проверка UI-поверхности
- В Qt: QML должен видеть новые методы (`setNoiseSuppression`, `setVad`, `setAec`) через `DriscordBridge`.
- Тоггл в каждую сторону, проверить что stats отражают изменение (VAD — packets_sent_ замирает при молчании, FEC — bytes-per-packet немного растёт).

---

## Порядок работы

1. CMake + FetchContent для `rnnoise`, `speexdsp`; убедиться что линкуется на Linux (основная цель) и Windows (кросс-компил).
2. RnnoiseDenoiser + интеграция (A1).
3. VadGate + замена статического noise_gate (A3).
4. Opus FEC флаги (A4) — самый короткий.
5. EchoCanceller + reference-буфер в AudioMixer (A2).
6. Проброс всех toggle'ов в Qt bridge.
7. Тесты и интеграционная валидация.

Каждый шаг — отдельный коммит, чтобы легко откатить при регрессии.
