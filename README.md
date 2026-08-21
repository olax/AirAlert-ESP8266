# AirAlert-ESP8266

Автономний сигналізатор повітряних тривог на NodeMCU v3 (ESP8266):
API alerts.in.ua → реле/сирена + LED-індикація + Web UI. Без Raspberry Pi,
Home Assistant, MQTT-брокера чи хмарного сервера.

**Статус: 0.9 (pre-release).** Firmware-фази 0–9 завершені й перевірені на
живій платі. Залишилось: hardware commissioning з реальним реле/LED
(docs/COMMISSIONING.md) і реліз 1.0.

## Можливості

- всі 5 типів загроз alerts.in.ua + невідомі (forward-compatible)
- кілька локацій одночасно, ієрархічний matching (область ⊃ район)
- часткові тривоги з окремою політикою LED/сирени
- state machine з підтвердженнями: старт швидкий (1), відбій обережний (2)
- відсутність API ≠ відбій: stale-стан утримує останні достовірні дані
- сирена патернами (start/reminder/end, на тип загрози), пріоритети, черга
- фізичні MUTE (until-clear / snooze) і TEST, safety-ліміт реле 30 с
- Web UI українською: дашборд, локації, профілі, журнал, діагностика, OTA
- provisioning: перший бут → AP + captive portal
- OTA: upload або HTTPS URL + SHA-256

## Швидкий старт

```bash
pio test -e native                  # 86 тестів бізнес-логіки
pio run -e nodemcuv2_prod           # прошивка
py -3 -m esptool --port COM3 --baud 921600 write-flash 0x0 \
    .pio/build/nodemcuv2_prod/firmware.bin
```

Перший запуск: пристрій підніме AP `AirAlert-XXXX` (пароль — у серіал-лозі,
115200), відкрийте http://192.168.4.1/ → введіть Wi-Fi, токен
[alerts.in.ua](https://alerts.in.ua/api-request), пароль адміністратора.

## ⚠ Безпека

- Сирену підключати ТІЛЬКИ після процедури docs/COMMISSIONING.md.
- Реле стартує вимкненим і має апаратний ліміт безперервної роботи.
- Web UI — лише в довіреній локальній мережі (docs/SECURITY.md).

## Документація

`docs/`: SPEC (повне ТЗ) · RESEARCH · ARCHITECTURE · HARDWARE · API ·
CONFIGURATION · SECURITY · OTA · TESTING · COMMISSIONING · TROUBLESHOOTING ·
RELEASE

## Джерело даних

[alerts.in.ua](https://alerts.in.ua) — дотримуйтесь лімітів API
(вбудований мінімум 10 с між запитами). Ліцензія: MIT.
