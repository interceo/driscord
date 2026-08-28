# Автоматизированное тестирование качества медиа

Программа тестов качества трансляции без ручного просмотра: объективные
метрики видео/аудио, A/V-рассинхрон, деградация сети, soak/capacity.
Трекинг: DRISCORD-16 (фазы DRISCORD-17…23). Фазы 1–3 реализованы; 4–7 —
дорожная карта с зафиксированным дизайном.

## Архитектура

Три контура + общая библиотека проб:

- **Контур 0 — per-PR, детерминированный, in-process.** Реальный путь
  encode → SRTP → libdatachannel → decode через `SignalingServerFixture`;
  сеть эмулирует fault-стадия SFU (`RtpFaultConfig`): Gilbert–Elliott
  burst-loss + link-модель (задержка/джиттер/полоса/очередь/blackout,
  паттерн WebRTC `SimulatedNetwork`). Гейты — жёсткие инварианты,
  монотонность по лестнице потерь и конвертные пороги.
- **Контур 1 — nightly, статистический** (Ф4/Ф5): полная матрица профилей,
  бейзлайны в репо, медиана из 3 прогонов, гейт по p95; netem-контур на
  unprivileged user+netns для реальных сокетов/ядра/ICE.
- **Контур 2 — offline/soak** (Ф6/Ф7): ViSQOL/OCR/VMAF-анализаторы над
  WAV/Y4M-дампами; churn-soak с наклоном RSS/FD; capacity через рой
  headless probe-клиентов.

## Библиотека проб (`core/tests/support/`)

| Компонент | Назначение |
|---|---|
| `media_metrics.hpp` | метрики над stats-структурами (concealment rate, harmonic framerate, freeze ratio, playout skew, repair ratio), перцентили, `sample_stats()`, гейт-репортер `gate_le/gate_ge` (JSON-line на каждый гейт; `DRISCORD_QUALITY_ENFORCE=soft` — burn-in режим) |
| `net_scenarios.hpp` | профили сети в авторских терминах (`wifi_good/wifi_burst/lte_good/lte_edge/congested/awful/blackout`), конвертер (loss%, avg_burst) → GE, таймлайны `run_scenario_timeline()` |
| `frame_marker.{hpp,cpp}` | Gray-code блок 8×3 ячеек по 16 px c индексом кадра + чексуммой; переживает кодек и 2× даунскейл; штампуется в исходник → сравнение marker-to-marker без смещения PSNR |
| `video_quality.{hpp,cpp}` | `I420Frame`, `bgra_to_i420`, PSNR (cap 48 dB)/SSIM через libyuv (те же `I420Psnr/I420Ssim`, что зовёт апстримный `rtc_tools/frame_analyzer`), `ReferenceFrameStore`, `VideoQualityAccumulator` — только то, что дают маркеры: psnr/ssim mean/min, dropped по спану индексов, glass-to-glass delay; freeze/harmonic fps — не тестовая математика, а `VideoReceiveStats` самого libwebrtc (W3C-формулы) через `media_metrics.hpp` |
| `media_dump.{hpp,cpp}` | `Y4mWriter`/`WavWriter` за `DRISCORD_MEDIA_DUMP_DIR`: аккумулятор пишет выровненные по маркерам пары `<label>.ref.y4m`/`<label>.recv.y4m` (кадры в порядке сравнения — позиционные фильтры ffmpeg видят выровненные потоки), av_sync — `<label>.ref.wav`/`<label>.rendered.wav` (фид и post-NetEq playout); без переменной хуки инертны |
| `screen_content.{hpp,cpp}` | детерминированные генераторы: `scrolling_text` (глифоподобные штрихи), `sliding_blocks` (слайды), `static_terminal`, `noise_window` (видео-в-окне) |
| `audio_probe.{hpp,cpp}` | чирп 1→4 kHz 20 ms (Hann), `mix_chirp`, `ChirpDetector` — matched filter над `on_rendered_audio` (post-NetEq playout tap) |
| `av_sync.hpp` | `AvSyncCorrelator`: события «маркер-кадр + чирп в один момент захвата» → знаковые оффсеты, p50/p95/max, audio-lead |

Серверная link-модель: `sfu_media_utils.{hpp,cpp}` (`LinkModelConfig`,
чистая `schedule_packet_departure()` — детерминирована по seed), таймеры —
в роутерах (`voice_router.cpp`, `screen_router.cpp`) на io_context сервера,
мутация в рантайме — `WebSocketServer::update_fault_config()` /
`SignalingServerFixture::set_fault_config()`. RTCP не повреждается никогда
(инвариант закреплён юнит-тестами); честная задержка RTCP — зона
netem-контура Ф5. Продакшн-конфиг нулевой: identity-путь без таймеров
(закреплено `test_link_model`).

## Тесты (реализовано, per-PR в `core-tests`)

| Тест | Что закрепляет |
|---|---|
| `test_media_metrics`, `test_link_model`, `test_frame_marker`, `test_chirp_detector`, `test_video_quality_accumulator` | чистая математика; label `unit;media`, идут и под Wine |
| `test_google_webrtc_degraded_voice` | лестница GE {0,2,8}%: монотонность concealment-rate (дельта после ramp-in NetEq), тон распознан до 2%, tripwire clean <1% |
| `test_google_webrtc_degraded_screen` | та же лестница: декод не останавливается при 8%, монотонные packets_lost/PLI/freeze, clean freeze==0 |
| `test_google_webrtc_network_scenarios` | wifi_burst выживает; lte_good: jitter-buffer target растёт против clean без остановки playout; blackout 4 s: media-only обрыв → декод возобновляется ≤10 s через PLI, соединение не падает |
| `test_google_webrtc_screen_quality` | clean скроллирующий текст: SSIM ≥0.95, PSNR mean ≥36/min ≥28 dB, dropped ≤2%, декод маркеров ≥99.5%; слайды: harmonic fps ≥24; freeze==0 и harmonic — из inbound-rtp stats приёмника, не из тестового кода |
| `test_google_webrtc_av_sync` | маркерная ground truth + stats-skew в одном прогоне; жёсткие гейты на конверте BT.1359 (max ≤125 ms, audio-lead ≤45 ms, p95 ≤80/100 ms), тонкие цели — observe-only |

Расширенные stats: `parse_voice_stats`/`parse_screen_stats` поднимают
`RtpReceiveStats`/`AudioReceiveStats`/`VideoReceiveStats`
(`google_webrtc_media_types.hpp`) — freeze/pause, jitter, nack/pli/fir,
concealment-семейство, audio energy, `estimated_playout_timestamp_ms`,
outbound-энкодер (frames_encoded, target_bitrate, quality_limitation),
available_outgoing_bitrate.

## Офлайн-дампы и ffmpeg-кросс-чек

`DRISCORD_MEDIA_DUMP_DIR=<dir>` включает запись дампов в тестах качества
(без переменной — ноль накладных расходов); это вынесенная вперёд часть
`media_recorder` из Ф7. Разбор любого упавшего гейта стандартными
инструментами:

```bash
DRISCORD_MEDIA_DUMP_DIR=/tmp/driscord-dumps \
    ctest --test-dir .builds/core-tests -R 'screen_quality|av_sync'
scripts/media_metrics_crosscheck.sh /tmp/driscord-dumps
```

Скрипт гоняет ffmpeg `psnr`/`ssim` по парам Y4M (per-frame логи — рядом,
`*.psnr.log`/`*.ssim.log`) и `ebur128` по WAV. Пары уже выровнены маркерами —
позиционное сравнение ffmpeg честно; сам по себе ffmpeg выравнивание при
дропах не решает (поэтому он кросс-чек, а не первичный инструмент гейта).
Дампы же — вход для Ф7-анализаторов (ViSQOL, VMAF-дельта, OCR) и для
`rtc_tools/frame_analyzer` из пиненого чекаута.

Калибровка libyuv↔ffmpeg (2026-08-28, clean scrolling text, 300 пар):
per-frame PSNR идентичен (`psnr_min` 38.623787 == ffmpeg `min` 38.623787);
mean расходится только семантикой агрегации (наш — среднее per-frame
значений с cap 48 dB, ffmpeg `average` — глобальная MSE последовательности:
45.69 vs 45.42). SSIM: 0.999188 vs ffmpeg All 0.998983 — систематический
сдвиг ~2e-4 из-за взвешивания плоскостей (libyuv 0.8/0.1/0.1, ffmpeg
4/6/1/6/1/6); сравнивать по Y-плоскости. Аудио clean: reference −18.0 LUFS
vs rendered −18.1 LUFS — уровень через Opus/NetEq сохранён.

## Зафиксированные наблюдения (2026-08-28)

- `estimatedPlayoutTimestamp` доходит через SFU для ОБОИХ треков screen-пары
  (SR-цепочка libdatachannel работает) — stats-метод A/V-sync применим;
  у voice в коротких прогонах поле может не успеть появиться.
- Clean-loopback A/V-оффсет ≈ +40 ms (аудио позади видео): NetEq держит
  target-delay ~40–60 ms при почти пустом видеобуфере. Направление
  безопасное по BT.1359; тонкая цель p50 ≤25 ms остаётся observe-only до
  бейзлайнов Ф4.
- Расхождение методов (маркеры vs stats-skew) ≈ 28 ms — stats-метод
  сглаживает; ground truth — маркерный.
- NetEq time-stretch под burst-loss ломает часть чирпов: для деградированных
  профилей детектор работает с порогом 0.30 и флором «половина событий».
- Concealment на clean-линке ≈1.2% кумулятивно — это ramp-in старта потока;
  метрики деградации считаются дельтой после установившегося потока.

## Гейты и борьба с флейком

- Монотонность по лестнице потерь — главный детектор регрессий (менее
  шумный, чем абсолютные пороги). Абсолютные tripwire — только на clean.
- Каждый гейт печатает JSON-line (`{"gate":...,"value":...,"pass":...}`) —
  логи CI работают потоком метрик для трендов (Ф7: Prometheus/Grafana).
- `DRISCORD_QUALITY_ENFORCE=soft` переводит гейты в log-only — режим
  burn-in новых порогов.
- Новые интеграционные тесты проходили burn-in 10–20 подряд локально.

## Дорожная карта (Ф4–Ф7, дизайн зафиксирован в DRISCORD-20…23)

- **Ф4** — nightly-матрица профилей × {voice,screen}, лестница монотонности,
  congested/ramp-сценарии CC, per-subscriber egress-фолты
  (`OneBadSubscriber`), статистика реконнектов (10 попыток, success ≥8/10,
  p95 recovery ≤30 s), бейзлайны `core/tests/baselines/*.json` (обновление —
  ревьюимый коммит), воркфлоу `media-nightly.yml` (перед включением cron —
  пул `driscord-builder` в replicas:2), exclude-label `soak|nightly` в
  пресетах.
- **Ф5** — headless probe-бинарь `core/tools/media_probe` (ядро без Qt,
  JSONL-метрики, exit code = гейты) + `scripts/netem_harness.sh`:
  unprivileged `unshare -rn`, veth, egress netem с seed (child pfifo под
  netem — иначе джиттер даёт скрытый reorder), сценарии wifi_burst/lte_edge/
  blackout (реальный ICE recovery)/ramp; `check_unshare_netns.sh` — graceful
  skip. Поглощает осиротевший `scripts/net_emulate.sh`.
- **Ф6** — `test_soak_churn` под готовый ярлык `soak` (30 мин, churn каждые
  ~20 s, наклон RSS/FD/threads ≈ 0 после прогрева), ASan-вариант через
  `DRISCORD_SOAK_MINUTES`; `capacity_run.sh` — breaking point = «p95 freeze
  ratio >2% или пол MOS-прокси» → `baselines/capacity.json`.
- **Ф7** — рекордер частично вынесен вперёд: Y4M/WAV-дампы уже пишутся через
  `DRISCORD_MEDIA_DUMP_DIR` (см. «Офлайн-дампы»); остаётся нарезка клипов
  (8–10 s ±0.5 s тишины, ≥5 клипов/сценарий) и образ `ci/analyzers/` (ViSQOL v3
  speech 16 kHz / audio 48 kHz, гейт по среднему vs baseline−0.1 MOS;
  Tesseract OCR-CER ≤2% при глифах ≥11 px; VMAF только как дельта,
  `vmaf_v0.6.1neg`, harmonic mean + p5, log-only — на скринконтенте VMAF
  ненадёжен абсолютно), сборка kaniko → supply-store (раннер офлайн);
  тренды: JSONL → pushgateway/VictoriaMetrics + Grafana (контракт driscord —
  JSONL на stdout + `PUSHGATEWAY_URL`).

## Пороги (справочно)

A/V-sync: заметность +45/−125 ms, приемлемость +90/−185 ms (ITU-R
BT.1359-1, асимметрия — «аудио впереди» хуже); идеал ±22 ms. Freeze:
`max(3×avg_interframe, avg+150 ms)`, окно 30 кадров (W3C freezeCount).
Индустриальные конверты: loss <1/1–5/>5 %, jitter <30/30–100/>100 ms,
freeze ratio <1 %/сессии. Скринконтент: SSIM/PSNR + (Ф7) OCR-CER; VMAF —
только дельта к бейзлайну.
