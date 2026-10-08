# BreezeCAD — Review: RAM / ram

**Записей:** 4

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### RAM-H2. `IconManager` держит все SVG в памяти 🔴

**Подозрение:** 105 иконок предзагружены (лог: `IconManager: preloaded 105/105`). Возможно, держит и исходные SVG-буферы, и растеризованные RGBA.

**Проверить:**
```
grep -an "mySvgBuffers\|myRawData\|myCache" src/app/IconManager.h
```

**Вероятность:** 🟠 средняя.

---

---

### RAM-H6. `TRACE` пишет в файл с `fflush` каждый кадр

**Подозрение:** даже при `BREEZECAD_TRACE=0` — мёртвый код? Проверить.

**Вероятность:** 🟢 низкая.

---

---

### RAM-H7. `myCommands` — вектор не чистится

**Подозрение:** `InitCommands` вызывается один раз? Или каждый doc?

**Проверить:**
```
grep -an "InitCommands" src/app/Application.cpp
```

**Вероятность:** 🟡 низкая.

---

---

### RAM-H8. Утечка в ImGui — drawList не сбрасывается

**Подозрение:** если какой-то `Begin*` без парного `End*` — ImGui копит.

**Проверить визуально:** в debug-сборке ImGui выдаёт assert.

**Вероятность:** 🟡 низкая (упало бы).

---

---

