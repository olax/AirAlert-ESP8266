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

## Фікстури та емулятор API

- `test/fixtures/*.json` — зліпки відповідей API, включно з битими.
- `tools/alerts_emulator.py` — динамічний емулятор alerts.in.ua:

```bash
python3 tools/alerts_emulator.py --port 8787 --log req.jsonl
# керування наживо (curl, браузер або stdin):
curl 'http://HOST:8787/ctl?cmd=start+air_raid+14'     # тривога в області 14
curl 'http://HOST:8787/ctl?cmd=start+chemical+123+14' # громада в області
curl 'http://HOST:8787/ctl?cmd=stop+air_raid+14'
curl 'http://HOST:8787/ctl?cmd=mode+429'              # ok|401|403|429|500|invalid|slow
curl 'http://HOST:8787/ctl?cmd=status'
curl 'http://HOST:8787/log'                           # журнал запитів (NDJSON)
```

- Сценарії з таймлайном: `--scenario tools/scenarios/demo.txt`
  (є demo.txt і failures.txt). Формат: команди + `sleep N`.
- Кожен запит логгується: час, source IP, токен (обрізаний), IMS,
  код відповіді — у консоль і в `--log` JSONL для аналізу.
- Плата ходить в емулятор у збірці `pio run -e nodemcuv2_mock`
  (див. AIRALERT_MOCK_URL у platformio.ini; хост відредагувати під себе).
- Груповий аналіз: емуляторний JSONL + серіал-лог плати зіставляються
  за часом (обидва в UTC).

> Урок: якщо емулятор запущено через конвеєр (`... | head`), блокування
> stdout зупиняє відповіді — запускайте detached з виводом у файл.

## На платі (dev-збірка)

Серіал-консоль 115200: `sim air_raid full|partial|none` — інʼєкція snapshot
в обхід API; `test`, `mute`, `status`, `log`. Переходи реле видно як
`[RELAY] ON/OFF`.
