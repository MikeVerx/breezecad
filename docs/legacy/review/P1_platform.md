# BreezeCAD — Review: P1 / platform

**Записей:** 1

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-273. `OcctView::Init` — порядок `glfwMakeContextCurrent` до `new Aspect_DisplayConnection`

**Где:** `OcctView.cpp:40-44`

```
glfwMakeContextCurrent(myGlfwWindow);   // ← сначала
myDisplayConnection = new Aspect_DisplayConnection();
```

Работает. Но `Aspect_DisplayConnection` на Windows не требует GL — он про окно. Ок.

**WONTFIX.**

---

---

