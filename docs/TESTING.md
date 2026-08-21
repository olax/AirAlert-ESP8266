# Тестування

## Хостові тести (основні)

```bash
pio test -e native            # всі 86
pio test -e native -f test_engine   # один suite
```

| Suite | Покриває |
|---|---|
| test_matcher | ієрархічний matching (SPEC 135) |
| test_engine | state machine, підтвердження, escalation (SPEC 136) |
| test_pattern | патерни, guard, черга, mute, millis wrap (SPEC 137) |
| test_api | ISO8601, backoff-драбина, stale (SPEC 8-10) |
| test_parser | фікстури API, битий JSON (SPEC 138, 142) |
| test_config | round-trip, валідація, міграція, reboot-loop guard |
| test_notify | сирена-політика, mute-сценарії (SPEC 50-54) |
| test_integration | наскрізні сценарії (SPEC 204) |

## Фікстури і mock

- `test/fixtures/*.json` — зліпки відповідей API, включно з битими.
- `tools/mock_alerts_server.py --fixture ... [--status 429]` — локальний
  mock з Last-Modified/304. Плата ходить у нього лише в dev-збірці з
  `-D AIRALERT_MOCK_URL='"http://host:8787/v1/alerts/active.json"'`.

## На платі (dev-збірка)

Серіал-консоль 115200: `sim air_raid full|partial|none` — інʼєкція snapshot
в обхід API; `test`, `mute`, `status`, `log`. Переходи реле видно як
`[RELAY] ON/OFF`.
