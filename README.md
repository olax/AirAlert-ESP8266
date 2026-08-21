# AirAlert-ESP8266

Автономний сигналізатор повітряних тривог на NodeMCU v3 (ESP8266): API
alerts.in.ua → реле/сирена + RGB-індикація. Без Raspberry Pi, Home Assistant
чи хмарного сервера.

**Статус:** у розробці. Phase 0–2 з 11 завершено (див. docs/SPEC.md §195–206).

## Збірка

```bash
pio test -e native        # unit-тести бізнес-логіки (host)
pio run -e nodemcuv2_dev  # dev-прошивка
pio run -e nodemcuv2_prod # production
```

Прошивка з WSL2 — через Windows esptool: див. `docs/RESEARCH.md`.

## Безпека

- Реле стартує у OFF і має апаратний ліміт безперервного ввімкнення.
- Сирену фізично підключати лише після hardware commissioning (docs/SPEC.md §205).
- API-токен зберігається тільки на пристрої (LittleFS), у git його немає.

## Документація

- `docs/SPEC.md` — повне ТЗ (216 розділів)
- `docs/RESEARCH.md` — Phase 0: перевірені факти про плату/API/пам'ять
