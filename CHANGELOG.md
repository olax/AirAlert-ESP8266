# Changelog

## [Unreleased]

### 2026-08-22

- Спрощена індикація (рішення власника): вбудований LED = стан системи,
  D0 = вихід тривоги (LED або реле-індикація з лампою, режим без блимань);
  RGB-бекенд збережено як майбутню опцію
- Commissioning сценарію A на живому залізі: реле KY-019 на D5
  (ACTIVE_HIGH), індикатор KY-009 на D0, повний цикл START/END перевірено
- Емулятор alerts.in.ua (`tools/alerts_emulator.py`): динамічне керування,
  сценарії, режими помилок, журнал запитів; runtime `setmock` у dev-збірці;
  робочий шлях плата→емулятор — Cloudflare-тунель
- Інтегровано зовнішнє code review (Codex): атомарний запис секретів з
  бекапом і rollback, ApiSnapshotCache (захист від хибного 304 після зміни
  токена/локацій), валідація вводу серіал-команд, pytest-контракти
- Тести: 100 native + 8 pytest

### 0.9.0 — 2026-08-21

Перша повнофункціональна pre-release збірка. Фази 0–9 за docs/SPEC.md.

- Ядро: AlertEngine (підтвердження 1/2, escalation, multi-location),
  LocationMatcher (ієрархія, каталог 155 локацій), PatternScheduler,
  NotificationEngine (черга, пріоритети, mute-політики), RelayGuard,
  StartupPolicy (анти-reboot-loop), 100 native-тестів
- API-клієнт: BearSSL + GTS trust anchors, TLS session resumption,
  If-Modified-Since/304, backoff 15→120с, стрімінг-парсинг з фільтром
- Персистентність: атомарний конфіг зі схемою і міграцією, циркулярний
  журнал подій 2×16КБ, state-snapshot для перезапуску під час тривоги
- Web UI (uk): дашборд зі stale-станом, локації, профілі сигналів, реле,
  журнал, діагностика, OTA; сесії X-Auth, lockout на login
- Provisioning: AP + captive portal, recovery AP+STA, wifi/forget
- OTA: upload + HTTPS URL з обовʼязковим SHA-256
- Прошивка: 528 КБ (51% IROM), RAM 47%, heap у роботі ~27 КБ
