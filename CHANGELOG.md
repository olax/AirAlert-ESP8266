# Changelog

## [Unreleased]

### 0.9.0 — 2026-08-21

Перша повнофункціональна pre-release збірка. Фази 0–9 за docs/SPEC.md.

- Ядро: AlertEngine (підтвердження 1/2, escalation, multi-location),
  LocationMatcher (ієрархія, каталог 155 локацій), PatternScheduler,
  NotificationEngine (черга, пріоритети, mute-політики), RelayGuard,
  StartupPolicy (анти-reboot-loop), 86 native-тестів
- API-клієнт: BearSSL + GTS trust anchors, TLS session resumption,
  If-Modified-Since/304, backoff 15→120с, стрімінг-парсинг з фільтром
- Персистентність: атомарний конфіг зі схемою і міграцією, циркулярний
  журнал подій 2×16КБ, state-snapshot для перезапуску під час тривоги
- Web UI (uk): дашборд зі stale-станом, локації, профілі сигналів, реле,
  журнал, діагностика, OTA; сесії X-Auth, lockout на login
- Provisioning: AP + captive portal, recovery AP+STA, wifi/forget
- OTA: upload + HTTPS URL з обовʼязковим SHA-256
- Прошивка: 528 КБ (51% IROM), RAM 47%, heap у роботі ~27 КБ
