# REST API

База: `http://<ip>/api/v1/`. Всі запити (крім login і setup при першому
запуску) вимагають заголовок `X-Auth: <token>` із відповіді login.
Формат помилок: `{"ok":false,"error":{"code":"...","message":"..."}}`.

## Автентифікація

| Метод | Шлях | Тіло | Відповідь |
|---|---|---|---|
| POST | /login | `{"password":"..."}` | `{"token":"<32hex>"}`; 5 невдач → 429 на 60с |

## Читання

| GET | Що повертає |
|---|---|
| /status | стан пристрою: wifi, api{online,stale,ever_synced,token_present}, alerts{active,muted,types[]}, relay{active,tripped} |
| /config | повний конфіг (без секретів) |
| /events | журнал подій, NDJSON |
| /system | heap/flash/fs/rssi/версії |
| /scan | Wi-Fi мережі поруч (без auth — потрібен порталу) |

## Дії

| POST | Ефект |
|---|---|
| /mute, /unmute | глушіння сирени (тривога лишається активною) |
| /test/relay | пульс реле на config.relay.test_ms |
| /relay/off | ручне вимкнення: стоп патернів + реле OFF |
| /reboot | перезавантаження |
| /wifi/forget | скинути Wi-Fi → provisioning AP |
| /setup | первинне налаштування (портал); без auth тільки поки не задано пароль |
| /ota/upload | multipart firmware.bin |
| /ota/url | `{"url":"https://...","sha256":"<64hex>"}` — sha256 обовʼязковий |

## Запис

| PUT | Тіло |
|---|---|
| /config | повний або частковий конфіг (як у GET) |
| /locations | `{"locations":[{"uid":14,"type":"oblast"},...]}` |
| /secrets/api-token | `{"token":"..."}` — write-only, назад не читається |
