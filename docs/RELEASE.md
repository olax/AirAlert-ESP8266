# Реліз

## Чекліст перед тегом (SPEC 206, 210)

- [ ] `pio test -e native` — 86/86
- [ ] `pio run -e nodemcuv2_prod` — розмір < 900 КБ
- [ ] Чистий флеш: erase-flash → прошивка → provisioning AP → повний цикл
      налаштування → тривога симулюється → OTA на ту саму версію
- [ ] Секретів немає в git: `git grep -i` по токенах/паролях
- [ ] CHANGELOG.md оновлено
- [ ] Документація відповідає коду

## Артефакти (SPEC 186)

```bash
pio run -e nodemcuv2_prod
cp .pio/build/nodemcuv2_prod/firmware.bin dist/airalert-<ver>.bin
sha256sum dist/*.bin > dist/SHA256SUMS
```

manifest.json для URL-OTA:

```json
{"version":"1.0.0","firmware":"https://.../airalert-1.0.0.bin","sha256":"...","min_schema":1}
```

## Версіонування

SemVer, тег `vX.Y.Z`, версія в platformio.ini (AIRALERT_VERSION).
