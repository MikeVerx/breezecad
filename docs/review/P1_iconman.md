# BreezeCAD — Review: P1 / iconman

**Записей:** 5

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-227. `IconManager::CreateTexture` — нет проверки `w`, `h`

**Где:** `IconManager.cpp:65-75`
Если lunasvg вернёт пустой bitmap (w=0 или h=0) — `glTexImage2D` с нулевыми размерами. Некорректная текстура.

**Фикс:** проверка.

---

---

### CR-228. `IconManager::RasterizeToTexture` — предупреждение при NOT FOUND на каждый вызов

**Где:** `IconManager.cpp:225-228`

```
if (foundPath.empty()) {
    std::cerr << "IconManager: NOT FOUND " << realName << ".svg (alias for " << name << ")\n";
    myCache[key] = 0;
    return 0;
}
```

**Кэшируется ноль**, но `std::cerr` пишется **один раз** на ключ (потому что потом идёт `myCache.find(key)` → hit). Ок. Но при **рестарте приложения** — снова пишет. **5 NOT FOUND иконок (TECH-DEBT-004) × каждого запуска.**

**Фикс:** убрать или понизить до TRACE.

---

---

### CR-229. `IconManager::RasterizeToTexture` — `bitmap` не проверяется на `data() != nullptr`

**Где:** `IconManager.cpp:155`

```
const uint8_t* src = bitmap.data();
```

Если bitmap создан, но `data()` = nullptr (редкий случай) — краш в цикле конвертации.

**Фикс:** `if (!src) return 0;`.

---

---

### CR-230. `IconManager` — `std::stringstream buf` — можно `std::string` + resize

**Где:** `IconManager.cpp:106-109`

```
std::stringstream buf;
buf << file.rdbuf();
std::string svgData = buf.str();
```

`stringstream` делает **двойную копию**. Для 105 иконок × ~2 КБ — не критично, но можно через `std::istreambuf_iterator` или `std::string((istreambuf_iterator<char>(file)), {})`.

**Оценка:** 5 минут.

---

---

### CR-231. `IconManager::PreloadDefaults` — `static const std::vector<std::string>` создаётся один раз

Но если функция вызывается на **каждом Application** — инициализация `std::vector<std::string>` **статическая локальная** — thread-safe-init C++11. Ок.

Но `PreloadAll` вызывается **на каждый Application**. Если Application пересоздаётся — грузит **заново** (в новый GL-контекст), но старые текстуры — утечка (см. RAM-H1). **Связано.**

---

---

