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
| `GET/PATCH/DELETE /servers/{id}` | да | Получить / изменить / удалить сервер |
| `GET /servers/{id}/members` | да | Участники |
| `POST/DELETE /servers/{id}/members` | да | Вступить / выйти напрямую |
| `POST /servers/{id}/members/{user_id}` | владелец | Добавить пользователя в сервер |
| `POST/GET /servers/{id}/channels/` | да | Создать / перечислить каналы |
| `GET/PATCH/DELETE /servers/{id}/channels/{channel_id}` | да | Операции с каналом |
| `POST/GET /servers/{id}/invites/` | да | Создать / перечислить инвайты |
| `DELETE /servers/{id}/invites/{code}` | да | Отозвать инвайт |
| `POST /invites/{code}` | да | Принять инвайт |
| `GET /updates/check?version=...&platform=linux` | нет | Проверка обновления |
| `POST /updates/upload` | да | Загрузить метаданные/файл релиза |
| `GET /updates/download/{platform}/{version}/{filename}` | нет | Скачать релиз |

Изменять сервер и его каналы может только владелец. Создать инвайт может любой
участник, перечислить все инвайты — только владелец. Сейчас любой
аутентифицированный пользователь может напрямую вступить в сервер через
`POST /servers/{id}/members`, то есть инвайт не является обязательным барьером.

`POST /updates/upload` требует лишь обычной авторизации, отдельной роли
администратора нет. Имя загруженного файла не очищается перед объединением с
путём. До публикации API этот endpoint следует ограничить и усилить проверку
пути.

## Пример

```bash
curl -X POST http://127.0.0.1:9002/auth/register \
  -H 'Content-Type: application/json' \
  -d '{"username":"alice","email":"alice@example.org","password":"change-me"}'
```

Ответ содержит `access_token`, `refresh_token`, `user_id` и данные профиля.
