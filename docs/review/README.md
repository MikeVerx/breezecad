# BreezeCAD — Code Review (индекс)

**Обновлено:** 2026-09-27 (после объединения дубликатов)

**Источник:** `_archive/_merged.md` (результат объединения 761 → 548)

## Как пользоваться

1. **Работай с файлами в порядке P0 → P1 → P2 → P3.**
2. Каждый файл — один модуль (ribbon, dialogs, theme, ...).
3. После фикса: статус `OPEN` → `FIXED (коммит XXXX)`, запись переносится в `FIXED.md`.
4. Обновлять таблицу прогресса внизу.

## Легенда

- **P0** — критично (функциональный баг, RULES-нарушение, слоение, утечка). Фиксить сейчас.
- **P1** — серьёзно (архитектура, производительность). До Фазы 2.
- **P2** — стиль (magic numbers, читаемость).
- **P3** — мелочи.
- **RAM** — утечки памяти/GPU (гипотезы).

## Файлы по приоритетам

### P0 — 417 записей

- `P0_app.md` — 35
- `P0_cmake.md` — 14
- `P0_dialogs_part1.md` — 39
- `P0_dialogs_part2.md` — 21
- `P0_doc.md` — 22
- `P0_home.md` — 22
- `P0_iconman.md` — 10
- `P0_misc.md` — 13
- `P0_occt_part1.md` — 47
- `P0_occt_part2.md` — 6
- `P0_panels.md` — 24
- `P0_platform.md` — 17
- `P0_qat.md` — 18
- `P0_ribbon.md` — 14
- `P0_status.md` — 12
- `P0_theme.md` — 35
- `P0_util.md` — 21
- `P0_viewcube.md` — 30
- `P0_widgets.md` — 17

### P1 — 89 записей

- `P1_app.md` — 16
- `P1_cmake.md` — 4
- `P1_dialogs.md` — 12
- `P1_doc.md` — 2
- `P1_iconman.md` — 5
- `P1_misc.md` — 7
- `P1_occt.md` — 9
- `P1_panels.md` — 11
- `P1_platform.md` — 1
- `P1_ribbon.md` — 16
- `P1_status.md` — 5
- `P1_util.md` — 1

### P2 — 23 записей

- `P2_app.md` — 4
- `P2_cmake.md` — 1
- `P2_dialogs.md` — 1
- `P2_doc.md` — 1
- `P2_misc.md` — 9
- `P2_occt.md` — 1
- `P2_panels.md` — 1
- `P2_ribbon.md` — 4
- `P2_viewcube.md` — 1

### P3 — 15 записей

- `P3_app.md` — 3
- `P3_misc.md` — 3
- `P3_ribbon.md` — 9

### RAM — 4 записей

- `RAM_ram.md` — 4

## Прогресс

| Severity | Записей | FIXED | Осталось |
|---|---|---|---|
| P0 | 417 | 0 | 417 |
| P1 | 89 | 0 | 89 |
| P2 | 23 | 0 | 23 |
| P3 | 15 | 0 | 15 |
| RAM | 4 | 0 | 4 |
| **Всего** | **548** | **0** | **548** |

## После закрытия всех P0+P1

1. Архив `_archive/` остаётся как история.
2. P2/P3 — по мере свободного времени в Фазе 2.
3. `docs/review/` можно удалить или архивировать.
