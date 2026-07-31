# Развёртывание API, signaling и STUN/TURN

## Рекомендуемая топология

На обычном Linux/VPS разместите PostgreSQL, FastAPI и signaling. coturn можно
оставить на OpenWrt, если у роутера есть публичный адрес и достаточно CPU/канала,
либо перенести на тот же VPS. TURN передаёт весь трафик тех пользователей, у
которых не установилось P2P, поэтому его канал и задержка критичны.

DNS-пример:

- `api.example.org` → API;
- `signal.example.org` → signaling;
- `turn.example.org` → публичный адрес coturn.

## API

Подготовьте PostgreSQL, persistent `DATA_DIR` и `.env`, как описано в
[локальном запуске](development.md). Production-процесс можно запускать так:

```bash
cd backend/api
.venv/bin/uvicorn main:app --host 0.0.0.0 --port 9002 --workers 1
```

Несколько workers/экземпляров требуют общего файлового хранилища для аватаров и
обновлений. Перед API стоит поставить reverse proxy, TLS, лимиты тела запроса и
резервное копирование PostgreSQL плюс `DATA_DIR`.

## Signaling

```bash
DRISCORD_PORT=9001 /opt/driscord/driscord_server
```

Процесс stateless: комнаты находятся в памяти одного экземпляра. Нельзя просто
поставить несколько экземпляров за round-robin балансировщиком — клиенты одной
комнаты могут попасть на разные процессы. Нужен один экземпляр либо sticky
маршрутизация и общий room/pub-sub слой, которого сейчас нет.

Текущий сервер слушает обычный TCP WebSocket и сам TLS не поддерживает. Более
того, `AppConfig` всегда строит адрес с `ws://` и `http://`; указать `wss://` или
`https://` через JSON нельзя. Следовательно, текущий клиент пригоден для LAN или
тестового HTTP-развёртывания. Для безопасного публичного развёртывания сначала
нужно изменить конфигурацию клиента так, чтобы она принимала полные URL, затем
терминировать TLS на reverse proxy.

## coturn как STUN/TURN

Минимальный long-term credentials конфиг `/etc/turnserver.conf`:

```ini
listening-port=3478
fingerprint
lt-cred-mech
realm=turn.example.org
user=driscord:replace-with-a-long-random-password

# Публичный IP сервера. Если coturn за NAT: PUBLIC/PRIVATE.
external-ip=203.0.113.10

min-port=49160
max-port=49200
no-multicast-peers
no-cli
```

Если сам OpenWrt-роутер имеет публичный WAN-адрес, `external-ip` — этот адрес.
Если coturn находится за ещё одним NAT, нужен статический проброс и форма
`external-ip=<public-ip>/<local-ip>`. При динамическом WAN IP конфиг необходимо
обновлять вместе с DDNS и перезапускать coturn.

Откройте/пробросьте:

- UDP 3478 — основной STUN/TURN;
- TCP 3478 — fallback соединения с TURN;
- UDP 49160–49200 — relay range из конфига;
- при настройке TLS также TCP 5349 и сертификаты.

На OpenWrt установите пакет coturn из репозитория вашей версии прошивки, включите
службу в автозапуск и внесите эквивалентные параметры в поставляемый ею конфиг.
Имена init-скрипта и UCI-полей зависят от сборки пакета; итог проверяйте по
реально запущенной команде и логам, а не только по UCI. Не публикуйте web/admin
интерфейсы coturn, если они не нужны.

Клиентская конфигурация:

```json
{
  "server": "signal.example.org:9001",
  "api": "api.example.org:9002",
  "screen_fps": 60,
  "turn_servers": [
    {
      "url": "turn:turn.example.org:3478",
      "user": "driscord",
      "pass": "replace-with-a-long-random-password"
    }
  ]
}
```

Статический пароль окажется на каждом клиенте и может быть извлечён. Для
публичного сервиса лучше выдавать краткоживущие TURN REST credentials через API,
но текущий клиент и API этого ещё не реализуют.

## Проверка

Сначала проверьте процессы независимо:

```bash
curl http://api.example.org:9002/health
curl http://signal.example.org:9001/presence
turnutils_uclient -u driscord -w 'password' -p 3478 turn.example.org
```

Затем запустите два клиента из разных сетей (например, домашний интернет и
мобильная точка), войдите в один voice channel и проверьте звук/экран. В логах
клиента ICE candidate с типом `relay` подтверждает использование TURN. Проверка
только внутри одной LAN недостаточна: там обычно побеждает host candidate и TURN
вообще не задействуется.

## Production-чеклист

- заменить `SECRET_KEY`, пароль PostgreSQL и TURN credentials;
- не выставлять PostgreSQL наружу;
- ограничить `/presence` и привязать signaling к JWT/membership;
- закрыть прямое вступление в сервер, если membership должен быть invite-only;
- ограничить `/updates/upload` административной ролью;
- добавить HTTPS/WSS в клиент и reverse proxy;
- настроить firewall, журналирование, health checks и резервные копии;
- проверить TURN из внешней сети и заложить трафик на relay.
