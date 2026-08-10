# Сборка и выпуск бинарей (Linux + Windows) и автосборка на homelab-машине

## Context

Предыдущий этап (см. `PLAN.md`) закрыл авторизацию signaling, дыры в `/updates`,
порядок `track_binding`, наблюдаемость SFU и мёртвый API. Побочный результат
ревью: **на этой машине нельзя собрать клиент** — каталог
`.cache/google-webrtc` отсутствует, поэтому любые правки в `core/` и
`client-qt/` уходят в CI непроверенными. Это же блокирует последний пункт
прошлого этапа (перевод видеопути на `QVideoSink`).

Цель этапа: получить воспроизводимые релизные бинари под Linux и Windows,
собираемые здесь же, и автосборку при обновлении `main`.

---

## 0. Инвентарь машины (проверено)

| Что | Значение |
|---|---|
| CPU / RAM | 32 потока / 62 ГБ |
| Диски | `/` 276 ГБ свободно, `/mnt/raid1` 1.6 ТБ свободно |
| ОС | Arch Linux, ядро 7.1.4, glibc **2.43** |
| Docker | 29.6.2, overlayfs, data-root на `/var/lib/docker` (то есть на `/`) |
| k3s | установлен и работает (там уже крутятся API и signaling) |
| Qt | системный **6.11.1** (только `qt6-base`), CI использует **6.7.3** |
| Нет | `gh`, `podman`, `wine`, `aqt`, libvirt/qemu, actions-runner |
| Репозиторий | `github.com/interceo/driscord`, публичный, ветка `main` |
| WebRTC pin | `956083e9…`, depot_tools pin `921e61b3…` |

---

## 1. Что мешает прямо сейчас

1. **Артефакт WebRTC собран только под Linux x86_64.** `cmake/GoogleWebRTC.cmake`
   падает на любой другой платформе намеренно, `build.sh --windows` завершается
   с ошибкой. Windows-клиента нет не из-за кода, а из-за отсутствия
   `libwebrtc.lib`.
2. **Линуксовый бинарь непереносим.** `scripts/build_google_webrtc.sh` собирает с
   `use_sysroot=false`, а линковка идёт против системной libstdc++/glibc. На
   Arch это glibc 2.43 — такой бинарь не запустится ни на Ubuntu 24.04 (2.39),
   ни на Debian 12 (2.36). Сейчас «релизных» бинарей и нет, но как только они
   появятся, это станет первым багрепортом.
3. **Qt не зафиксирован.** Хост — rolling 6.11.1, CI — 6.7.3. Релиз должен
   собираться против одной пиннутой версии, иначе «у меня собирается» перестаёт
   что-то значить.
4. **MinGW закрыт осознанно** (`CLAUDE.md`): Google WebRTC не поддерживает
   MinGW, а прошлый MinGW-слой был удалён как фальшивая совместимость. Возвращать
   его нельзя — это не вариант решения, а известный тупик.
5. **Docker data-root на `/`.** Сборка WebRTC в контейнере требует ~35 ГиБ, плюс
   чекаут ~30 ГиБ; 276 ГБ хватит ненадолго при нескольких образах и кэшах.

---

## 2. Windows: выбор способа

| Вариант | Суть | Оценка |
|---|---|---|
| **A. Настоящая кросс-сборка** Linux → Windows: GN `target_os="win"` + clang-cl + MSVC/Windows SDK, упакованные `depot_tools/win_toolchain/package_from_installed.py` | Google так собирает Chrome на своих ботах | Работает только с уже добытой лицензионной копией MSVC+SDK, публично не поддерживается, Qt для Windows всё равно придётся либо собирать самим, либо линковать официальные MSVC-сборки через `lld-link`. Самый хрупкий путь |
| **B. Windows-VM на этой машине (KVM/QEMU), нативная сборка** | 16 vCPU / 32 ГБ / 400 ГБ на raid1, внутри — VS 2022 Build Tools, depot_tools, Qt, CMake, Ninja, OpenSSH | Штатные тулчейны, ничего не подделываем. Стоит: лицензия Windows, диск, разовая настройка |
| **C. GitHub `windows-latest`** | Сборка на hosted-раннере | Не тянет WebRTC: 14 ГБ диска и 6 ч лимита при потребности ~35 ГиБ. Годится только для потребления **готового** архива |
| **D. MinGW / llvm-mingw** | — | Запрещено `CLAUDE.md`, WebRTC не поддерживает. Не рассматривается |

**Решение: B, с приёмом из C.** Windows-артефакт WebRTC собирается в VM **один
раз на пин ревизии**, складывается в локальное хранилище артефактов с ключом
`sha256(revision + depot_tools_revision + patches)`, и дальше сборки клиента его
только потребляют — ровно та схема, что уже работает для Linux в CI.

Вариант A остаётся записан как возможная оптимизация «потом»: когда VM уже
настроена, из неё же можно вызвать `package_from_installed.py` и попробовать
кросс-сборку, ничего не теряя.

Формулировка «кросскомпиляция под Windows» в исходной задаче честно
переформулируется как: **сборка Windows-бинарей на этой машине** — через
виртуализацию, а не через кросс-тулчейн.

---

## 3. Linux: не кросс, а «сборка на нижней границе glibc»

Собирать релиз в контейнере, чьи glibc/libstdc++ старше целевых систем:

- база **debian:12** (glibc 2.36) → покрывает Debian 12+, Ubuntu 22.04+,
  Fedora 37+; clang/lld 19 из apt.llvm.org (в main только 14, для WebRTC мало);
- **весь** конвейер внутри одного образа: и `build_google_webrtc.sh`, и CMake —
  иначе финальная линковка снова подхватит хостовые символы;
- Qt 6.7.3 `gcc_64` через `aqtinstall` в образ, а не из дистрибутива;
- выход: `driscord-client-linux-x86_64.tar.gz` = `driscord_client`,
  `libdatachannel.so*`, Qt-библиотеки и плагины, `qt.conf`, launcher.
  `RUNPATH=$ORIGIN` для libdatachannel уже настроен, раскладка ложится на это
  без переделки. AppImage — отдельным шагом позже, если понадобится.

Файлы: `ci/linux-release.Dockerfile`, `ci/build-linux-release.sh`.

---

## 4. Автосборка при обновлении `main`

**Self-hosted GitHub Actions runner** — workflow'ы уже написаны под это:
`runs-on: ${{ vars.RUNNER_LABEL || 'ubuntu-24.04' }}`, достаточно завести
переменную репозитория.

Установка:

- отдельный пользователь `driscord-ci` **без sudo**, HOME на
  `/mnt/raid1/ci/driscord-ci`, work-dir `/mnt/raid1/ci/_work`;
- `actions/runner`, регистрация с лейблами `self-hosted,linux,x64,driscord-builder`,
  systemd-юнит через `svc.sh install driscord-ci`;
- переменная репозитория `RUNNER_LABEL=driscord-builder`.

**Безопасность (репозиторий публичный — это главное ограничение):**

- self-hosted раннер **никогда** не исполняет workflow из форка. Все
  `pull_request`-джобы остаются на GitHub-hosted; на self-hosted переезжают
  только `push:main`, `workflow_dispatch` и теги;
- в настройках репозитория: *Fork pull request workflows → Require approval for
  all external contributors*;
- у пользователя раннера нет доступа к `~/.kube`, ключу sops и к рабочему клону
  `/mnt/raid1/homelab/repos/driscord` (деплой остаётся ручным);
- секреты (`DRISCORD_UPDATE_TOKEN`) видны только релизной джобе на push в main.

**Кэши на раннере** (переменные окружения уже поддержаны скриптом):

```
DRISCORD_DEPOT_TOOLS_DIR=/mnt/raid1/ci/cache/depot_tools
DRISCORD_WEBRTC_CHECKOUT_ROOT=/mnt/raid1/ci/cache/google-webrtc
DRISCORD_WEBRTC_OUT_DIR=/mnt/raid1/ci/cache/google-webrtc/src/out/driscord-release
```

Чекаут переживает джобы, поэтому пересборка WebRTC случается только при смене
пина — на hosted-раннере это стоило бы 35 ГиБ кэша и десятков минут.

Плюс `ccache` для CMake-части и предварительно распакованный Qt в
`/mnt/raid1/ci/cache/qt/6.7.3`.

Перед этим: перенести docker data-root на raid1
(`/etc/docker/daemon.json → {"data-root": "/mnt/raid1/docker"}`).

---

## 5. Версии и публикация

- Единственный источник версии — git-тег: `git describe --tags --always`.
  Сейчас `0.3.0` захардкожен в двух местах (`CMakeLists.txt:5`,
  `client-qt/src/main.cpp:34`) — свести к одному сгенерированному заголовку.
- `/updates/check` разбирает версию как числа через точку, значит теги вида
  `v0.4.0`, а `v` срезается при публикации.
- Релизная джоба на push в `main`:
  1. собирает Linux (контейнер) и Windows (VM);
  2. `gh release` с обоими архивами;
  3. `POST /updates/upload` c `Authorization: Bearer $DRISCORD_UPDATE_TOKEN` —
     это тот самый admin-gated эндпоинт из прошлого этапа; аккаунту публикации
     нужно один раз выставить `users.is_admin` в БД.
- Клиент сейчас `/updates/check` не дёргает вообще — подключение апдейтера
  остаётся отдельным пунктом после того, как публикация заработает.

---

## 6. Этапы

**E1. Инфраструктура раннера (полдня).** Пользователь, каталоги на raid1,
docker data-root, runner + systemd, `RUNNER_LABEL`, разделение workflow'ов
`ci.yml` (hosted, PR) / `release.yml` (self-hosted, main). *Готово, когда*
push в main запускает существующие джобы здесь, а PR из форка — нет.

**E2. Linux-релиз в контейнере (1–2 дня).** `ci/linux-release.Dockerfile`,
сборка WebRTC внутри образа, tarball с Qt. *Готово, когда* архив запускается
на чистой Ubuntu 22.04 и Debian 12 (проверять в docker-контейнерах без
dev-пакетов), а `ldd` не показывает ничего с хоста.

**E3. Windows-VM (1–2 дня).** libvirt/qemu на хосте, гость Windows 11 Pro,
VS 2022 Build Tools + Windows 11 SDK, Python, depot_tools
(`DEPOT_TOOLS_WIN_TOOLCHAIN=0`), Qt 6.7.3 msvc2019_64, CMake, Ninja, OpenSSH
Server. *Готово, когда* с хоста по ssh выполняется команда сборки в госте.

**E4. Windows-артефакт WebRTC (1–3 дня, много неизвестных).**
`scripts/build_google_webrtc.ps1` с теми же GN-аргументами, кроме
Linux-специфичных (`use_custom_libcxx` на Windows не трогаем — там MSVC STL,
что как раз совпадает с Qt/MSVC). Патчи `google-webrtc-libstdcxx.patch`
относятся к libstdc++ и на Windows не применяются — разложить условно.
*Готово, когда* `libwebrtc.lib` собран и закэширован по ключу пина.

**E5. Windows-клиент (2–4 дня, ожидается гниль).** `cmake/GoogleWebRTC.cmake`
учит windows-ветку; `system_audio_capture_win.cpp` не собирался очень давно —
считать, что он сломан; `windeployqt --qmldir` для упаковки; отдельный вопрос —
чем на Windows собирать libdatachannel (на Linux он вынесен в DSO с GnuTLS
из-за коллизии символов с BoringSSL; на Windows символы разрешаются по модулям,
поэтому статическая сборка с OpenSSL, вероятно, безопасна — **проверить
эмпирически**, а не предположить). *Готово, когда* zip запускается на чистой
Windows 11 и проходит голос+экран против нашего SFU.

**E6. Релизный конвейер (полдня).** Версия из тега, `gh release`, публикация в
`/updates`. *Готово, когда* тег `v0.4.0` даёт два архива в релизе и
`/updates/check` отвечает клиенту.

Порядок: E1 → E2 → E6(Linux-часть) → E3 → E4 → E5 → E6(Windows-часть).
E2 полезен сам по себе и разблокирует локальную проверку `core/` — именно его
не хватало на прошлом этапе.

---

## 7. Что поставить

**Хост (Arch):** `libvirt qemu-full virt-install edk2-ovmf dnsmasq iptables-nft`
(VM), `github-cli`, `ccache`, `python-pipx` + `pipx install aqtinstall`,
`libicu` (зависимость раннера). Docker уже есть.

**Контейнер Linux-релиза:** `debian:12` + apt.llvm.org clang-19/lld-19, `cmake`,
`ninja-build`, `git`, `python3`, `pkg-config`, `libgnutls28-dev`, `libnss3-dev`,
`libpulse-dev`, X11-dev (`libx11 libxcomposite libxdamage libxext libxfixes
libxrandr libxtst libxinerama libxcursor libxi`), `libwayland-dev`,
`libxkbcommon-dev`, `libgl1-mesa-dev`, `libegl1-mesa-dev`, Boost headers, Qt
6.7.3 через aqtinstall.

**Гость Windows:** VS 2022 Build Tools (Desktop development with C++ + Windows
11 SDK), Git for Windows, Python 3.11, CMake, Ninja, 7-Zip, Qt 6.7.3
msvc2019_64, OpenSSH Server.

---

## 8. Чего сознательно не делаем

- не возвращаем MinGW и `third_party/windows` (см. `CLAUDE.md`);
- не собираем Qt из исходников — берём официальные бинари под обе платформы;
- macOS вне этапа: нет ни артефакта WebRTC, ни железа;
- автодеплой в k3s не привязываем к сборке: `scripts/deploy.sh` остаётся ручным,
  пока не будет отдельного решения;
- не публикуем архив WebRTC в публичный релиз (лицензия и 1+ ГБ) — он живёт
  локальным кэшем.

---

## 9. Риски

- **Лицензия Windows** для VM — блокирующий вопрос к владельцу машины.
- **Гниль Windows-кода**: `system_audio_capture_win.cpp` и WIN32-ветки CMake не
  собирались с момента миграции на Google WebRTC. Оценка E5 в 2–4 дня — с этим
  допущением.
- **Диск**: чекаут WebRTC ×2 платформы + образы + VM ≈ 400–500 ГБ на raid1
  (свободно 1.6 ТБ, запас есть, но docker data-root перенести обязательно).
- **Публичный репозиторий + self-hosted runner** — единственная по-настоящему
  опасная часть. Если разделение hosted/self-hosted не сделать первым шагом,
  любой PR получает выполнение кода на этой машине.

---

## 10. Verification

1. `push` в `main` → джоба стартует на `driscord-builder`; PR из форка — на
   ubuntu-24.04 и без секретов.
2. `docker run --rm -v $PWD/dist:/d ubuntu:22.04 /d/driscord_client --version`
   и то же на `debian:12` — запускается, `ldd` без «not found».
3. Пересборка на неизменном пине WebRTC не трогает `.cache` и укладывается в
   минуты (проверка, что кэш действительно переиспользуется).
4. Windows-zip на чистой Windows 11: вход, голос в обе стороны, демонстрация
   экрана со звуком против нашего SFU.
5. Тег `v0.4.0` → GitHub Release с двумя архивами; `GET /updates/check?version=0.3.0&platform=linux`
   отдаёт `update_available: true`.
6. `./scripts/build.sh --server --test` и `--api --test` продолжают проходить на
   раннере (это уже настроено в CI прошлым этапом).
