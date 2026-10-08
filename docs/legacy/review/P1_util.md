# BreezeCAD — Review: P1 / util

**Записей:** 1

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-211. `RequestDelete` — `Handle(TDataStd_Name)` без проверки типа

**Где:** `Application.cpp:1011`
```
if (label.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
    name = ExtToUtf8(nameAttr->Get());
}
```

`nameAttr->Get()` возвращает `TCollection_ExtendedString`. `ExtToUtf8` — из `util/Utf8`. BUG-A5 может проявиться.

**Фикс:** проверить `ExtToUtf8` на кириллицу.

---

---

