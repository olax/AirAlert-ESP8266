# Phase 0 — Research (SPEC §195)

Дата: 2026-08-21. Усе нижче звірено запуском на цільовій машині та платі, не з пам'яті.

## Плата

| Параметр | Значення | Як перевірено |
|---|---|---|
| Чіп | ESP8266EX, кварц 26 МГц | `esptool flash-id` |
| Флеш | 4 МБ (mfr 0x5e, dev 0x4016) | `esptool flash-id` |
| USB-UART | CH340 (VID 1A86, PID 7523), COM3 на Windows-хості | Win32_PnPEntity |
| MAC | 98:f4:ab:xx:xx:xx | `esptool read-mac` |
| Стокова прошивка | Espressif AT v1.3.0.0 / SDK 2.0.0 | AT+GMR |
| Бекап флешу | `../stock-backup/nodemcu-stock-4M.bin` + SHA256 | `read-flash 0x0 0x400000` |

## Середовище збірки

- WSL2 Debian, **без USB**: прошивка й монітор ідуть через Windows
  (`py -3 -m esptool`, COM3). Детально: `../../BUILD-CLI.md` §10.2.
- PlatformIO Core 6.1.19, platform **espressif8266@4.2.1** → Arduino core **3.1.2**
  (SDK 2.2.2-dev, lwIP 2.1.3, BearSSL b024386 — з боот-банера).
- Хостові тести: `pio test -e native`, g++ 12 (Debian), Unity.
- Прошивка: `pio run -e nodemcuv2_dev && py -3 -m esptool --port COM3 --baud 921600 write-flash 0x0 <bin>`.

## Флеш-розкладка

`eagle.flash.4m2m.ld`: 1019 КБ скетч + ~2 МБ LittleFS + 16 КБ EEPROM + RF cal.
OTA-бюджет: образ має вміщатися у вільний сектор → практична межа ~500 КБ
(free sketch space ≥ image size). Скелет = 269 КБ (26%), запас є, але Web UI
(gzip PROGMEM) і TLS з'їдять помітну частину. CI має size-gate (SPEC §146).

## Пам'ять (виміряно)

- Скелет: RAM 34.9% (28.6 КБ), heap після boot **51.7 КБ** вільно.
- Порожній скетч з усіма include (WiFiManager+ArduinoJson+BearSSL+WebServer+LittleFS)
  на arduino-cli давав **91% IRAM** — IRAM, а не флеш, буде вузьким місцем.
  Рішення: `BEARSSL_SSL_BASIC` уже в build_flags; уникати `IRAM_ATTR` без потреби.
- BearSSL TLS handshake ≈ 20+ КБ heap. `setBufferSizes(4096, 512)` обов'язково.

## alerts.in.ua API (звірено з devs.alerts.in.ua)

- Base: `https://api.alerts.in.ua`, runtime endpoint: `/v1/alerts/active.json`.
- Ліміти: м'який 8–10 req/хв, жорсткий 12 req/хв → 429; систематично → бан IP+token.
  Default poll 15 с (SPEC §7) — вкладається з запасом 3×.
- UID приклади: м. Київ = 31, Київська область = 14.
- Каталог локацій: runtime endpoint відсутній — офіційна таблиця UID на
  devs.alerts.in.ua, генератор `tools/update_locations.py` (Phase 5/6).

## Бібліотеки (pinned)

| Бібліотека | Версія | Причина |
|---|---|---|
| ArduinoJson | ^7.4.3 | stream parsing + filter (SPEC §111) |
| Unity | (test_framework) | host unit tests |

WiFiManager — рішення відкладено до Phase 7: можливий конфлікт з власним Web UI
(SPEC §191); якщо власний captive portal виявиться <200 рядків — обійдемося без нього.

## Рішення

1. **PlatformIO**, не arduino-cli — native tests + pinned deps (SPEC §107).
2. Полярність реле невідома → ACTIVE_HIGH-припущення, `forceRelayOff()` до
   будь-якого init, фізичне підключення сирени тільки в Phase 10 (SPEC §39).
3. Піни: D5=relay, D6=MUTE, D7=TEST, D1/D2=I2C→MCP23017 (SPEC §61).
4. Ядро (lib/core) — чистий C++17 без Arduino, DI-інтерфейси (SPEC §109–110).

## Додаток (Phase 7-8): heap-урок

8 кореневих сертифікатів у X509List на poll-шляху = StoreProhibited під час
TLS handshake (~27 КБ вільного heap не вистачає на парсинг 8 RSA-4096 TA +
handshake). Правило проєкту: **alerts-клієнт тримає рівно 4 GTS-корені**;
широкий бандл (8 CA) — тільки в OTA-режимі, коли все інше зупинено.
Робочий heap після Phase 8: ~27 КБ вільно, TLS-пік залишає ~5 КБ запасу.
