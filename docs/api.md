# REST API

API — FastAPI-приложение без общего префикса `/api`. Интерактивная актуальная
спецификация создаётся самим приложением на `/docs` и `/openapi.json`.

Защищённые методы ожидают заголовок:

```http
Authorization: Bearer <access_token>
```

Access token по умолчанию живёт 30 минут, refresh token — 7 дней. Токены JWT
подписываются `SECRET_KEY`; его обязательно заменить.

## Маршруты

| Метод и путь | Авторизация | Назначение |
|---|---|---|
| `GET /health` | нет | Проверка API |
| `POST /auth/register` | нет | Регистрация и выдача пары токенов |
| `POST /auth/login` | нет | Вход по username/password |
| `POST /auth/refresh` | refresh token в body | Обновить пару токенов |
| `GET/PATCH /users/me` | да | Свой профиль |
| `GET /users/` | да | Каталог публичных профилей пользователей |
| `GET /users/lookup?username=...` | нет | Найти публичный профиль |
| `GET /users/{id}` | нет | Публичный профиль |
| `GET /users/{id}/avatar` | нет | Получить аватар |
| `PUT /users/{id}/avatar` | владелец | Загрузить JPEG/PNG/GIF/WebP до 5 MiB |
| `PATCH /users/{id}` | владелец | Изменить display name |
| `POST/GET /servers/` | да | Создать сервер / получить свои серверы |
| `GET /servers/{id}` | участник | Получить сервер |
| `PATCH/DELETE /servers/{id}` | владелец | Изменить / удалить сервер |
| `GET /servers/{id}/members` | участник | Участники |
| `POST /servers/{id}/members` | да | Всегда 403; старый прямой join закрыт |
| `DELETE /servers/{id}/members` | участник | Выйти из сервера |
| `POST /servers/{id}/members/{user_id}` | владелец | Добавить пользователя в сервер |
| `POST/GET /servers/{id}/channels/` | да | Создать / перечислить каналы |
| `GET/PATCH/DELETE /servers/{id}/channels/{channel_id}` | да | Операции с каналом |
| `POST/GET /servers/{id}/invites/` | да | Создать / перечислить инвайты |
| `DELETE /servers/{id}/invites/{code}` | да | Отозвать инвайт |
| `POST /invites/{code}` | да | Принять инвайт |
| `GET /updates/check?version=...&platform=linux` | нет | Проверка обновления |
| `POST /updates/upload` | администратор | Загрузить метаданные/файл релиза |
| `GET /updates/download/{platform}/{version}/{filename}` | нет | Скачать релиз |

Изменять сервер и его каналы может только владелец. Создать инвайт может любой
участник, перечислить все инвайты — только владелец. Вступление возможно только
через `POST /invites/{code}` либо при явном добавлении пользователя владельцем;
знание числового id сервера не даёт доступ к его данным или voice-каналам.

`POST /updates/upload` публикует файл, который скачивают и запускают все
клиенты, поэтому требует флага `users.is_admin`. Self-service способа его
получить нет: оператор выставляет колонку в базе. Имя файла берётся как
basename, а все сегменты пути релиза проверяются на вложенность в `data/releases`
(`storage_paths.contained_path`) — `..` в `platform`, `version` или `filename`
даёт 400, а не чтение произвольного файла.

## Пример

```bash
curl -X POST http://127.0.0.1:9002/auth/register \
  -H 'Content-Type: application/json' \
  -d '{"username":"alice","email":"alice@example.org","password":"change-me"}'
```

Ответ содержит `access_token`, `refresh_token`, `user_id` и данные профиля.
