# BreezeCAD — Review: P0 / home

**Записей:** 22

**Легенда:** `OPEN` / `FIXED (коммит XXXX)` / `DEFERRED` / `WONTFIX`

---

### CR-432. `HomeContext` — `char* searchBuffer` + `searchBufferSize` — 4-й дубль

**Где:** `HomeScreen.h:33-34`
**Статус:** OPEN

То же, что CR-380 (QAT) и CR-356 (SvgButton). Один и тот же антипаттерн в 4 местах.

**Фикс:** общий `util::SearchBuffer` (либо `std::string`, либо `std::array<char,N>`).

**Оценка:** 40 минут (пакетно во всех 4 местах).

---

---

### CR-433. `HomeContext` — `bool* showHomeScreen` — косвенное состояние

**Где:** `HomeScreen.h:30`
**Статус:** OPEN

```
bool* showHomeScreen = nullptr;
```

HomeScreen **сам управляет флагом** — включает/выключает свой показ через указатель. Обычно это делает **вызывающий** (Application) по клику колбэка.

**Проблема:** HomeScreen и Application оба пишут в один флаг. Race-условие по дизайну.

**Фикс:** убрать `showHomeScreen` из ctx. Application решает, вызвать ли `DrawHomeScreen`.

**Оценка:** 15 минут.

---

---

### CR-434. `HomeContext` — `double ramMB` — почему home знает про RAM?

**Где:** `HomeScreen.h:39`

**По UI_LAYOUT** — телеметрия в QAT (FPS/RAM/Obj). Зачем home ещё одну показывает?

**Проверить в .cpp** — возможно, дублирование.

**Оценка:** 5 минут + убрать поле.

---

---

### CR-435. `HomeContext` — 7 колбэков без default + нет проверок

**Где:** `HomeScreen.h:42-48`

`onSwitchDoc`, `onCloseDoc`, `onCreateDoc`, `onOpenFile`, `onShowSettings`, `onNotImplemented`, `onQuit` — все `std::function`, могут быть пустыми.

**Фикс:** в `DrawHomeScreen` — ранние проверки или `assert`.

**Оценка:** 10 минут.

---

---

### CR-436. `Thumbnails::CaptureCurrentThumbnail` — параметр `activeDocIndex` не нужен?

**Где:** `Thumbnails.h:22`

```
void CaptureCurrentThumbnail(OcctView& view, int activeDocIndex,
                             std::vector<std::string>& thumbs);
```

**Проверить в .cpp** — используется ли `activeDocIndex` внутри? Если да — ок, если нет — удалить.

---

---

### CR-437. `Thumbnails::CaptureCurrentThumbnail` — сохраняет в `.bmp` (устаревшее)

**Где:** лог: `Thumbnail saved: .../thumb_0.bmp`

По UI_LAYOUT §2.1 — превью PNG. BMP — **без сжатия**, миниатюра 256×256 = **256 KB** на каждый документ.

**Фикс:** использовать PNG через `stb_image_write` (уже в проекте есть `stb_image`).

**Оценка:** 30 минут.

---

---

### CR-438. `Thumbnails` — magic 10 в комментарии «max 10»

**Где:** `Thumbnails.h:18`

```
/// Добавить файл в начало списка недавних (без дублей, max 10).
```

**Где эта константа в коде?** Проверить в .cpp — если magic `10` — вынести в `constexpr int kMaxRecentFiles = 10;`.

---

---

## 🔴 P0 — ui/home (Волна 11, часть 2: HomeScreen.cpp)

---

### CR-439. 🔴 HomeScreen напрямую работает с OpenGL — нарушение слоя UI

**Где:** `HomeScreen.cpp:15, 214-224`
**Статус:** OPEN
**Категория:** архитектура, нарушение `ARCHITECTURE.md §2`

```
#include <GL/gl.h>
...
GLuint texId = 0;
glGenTextures(1, &texId);
glBindTexture(GL_TEXTURE_2D, texId);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
```

**UI-модуль создаёт GL-текстуры напрямую.**

**Почему это плохо:**
- Если завтра перейдём на Metal/Vulkan для macOS — этот код сломается.
- UI-модуль не должен знать, что под ним OpenGL.
- Кэш текстур живёт **вне** `IconManager` — непонятно, кто владелец.
- `stb_image` используется в UI-модуле для декодирования — тоже слой не тот (декодирование — `util`).

**Фикс:**
1. Создать `util/ThumbnailCache.{h,cpp}` — управляет текстурами превью.
2. `util/ImageLoader` — обёртка над `stb_image`.
3. `HomeScreen` вызывает `ThumbnailCache::Get(path)` — не знает ни про GL, ни про stb.

**Оценка:** 2-3 часа (правильно).

---

---

### CR-440. 🔴 `static std::map<std::string, ImTextureID> s_thumbs;` — кэш никогда не очищается

**Где:** `HomeScreen.cpp:207`
**Статус:** OPEN
**Категория:** RAM, GPU-утечка

```
static std::map<std::string, ImTextureID> s_thumbs;
auto cacheIt = s_thumbs.find(thumbPath);
if (cacheIt == s_thumbs.end()) {
    // создаём текстуру, добавляем в кэш
    ...
    s_thumbs[thumbPath] = (ImTextureID)(intptr_t)texId;
}
```

**Проблемы:**
1. `static` в функции — общий для всех вызовов, никогда не освобождается.
2. **Никогда не вызывает `glDeleteTextures`** — текстуры остаются в VRAM до конца процесса.
3. **Не проверяет, изменился ли файл миниатюры** — если пользователь обновил `thumb_0.bmp`, старая текстура останется.
4. `std::map` — **O(log N)** вместо `unordered_map` O(1).
5. **Бесконтрольный рост** — 100 документов = 100 текстур 320×240 = ~30 МБ GPU.

**Связано с RAM-H1 (IconManager).**

**Фикс:**
- Хранить в `HomeScreen` **не static**, а в `Application::myThumbnailCache`.
- `~Application` вызывает `Clear()`.
- LRU-лимит (например, 50 текстур).
- `unordered_map`.

**Оценка:** 1.5 часа.

---

---

### CR-448. HomeScreen — `std::tolower` не работает для кириллицы

**Где:** `HomeScreen.cpp:334-335, 368-369, 405-406`

```
std::string searchLower = ctx.searchBuffer;
for (auto& ch : searchLower) ch = (char)std::tolower((unsigned char)ch);
...
for (auto& ch : lname) ch = (char)std::tolower((unsigned char)ch);
```

**Проблема:** `std::tolower` работает только для ASCII. Для кириллицы (`А-Я` → `а-я`) — **не работает**, возвращает исходный символ.

**Что ломается:** поиск «деталь» не найдёт «Деталь».

**Фикс:**
- `util/Utf8::ToLowerInPlace(std::string&)` — Unicode-aware.
- Или ICU (тяжёлый).
- Или явная обработка диапазонов UTF-8 (для RU — 2 байта на символ).

**Оценка:** 1 час.

---

---

### CR-449. HomeScreen — поиск пересчитывает lowercase **на каждый кадр**

**Где:** `HomeScreen.cpp:334-335, 405-406`

`searchLower` пересчитывается **на каждый кадр**, хотя меняется только при вводе. При активном поиске — 60 FPS × 15 команд = **900 lowercase-итераций/сек**.

**Фикс:** кэшировать lowercase, пересчитывать только при изменении буфера.

**Оценка:** 20 минут.

---

---

### CR-450. HomeScreen — дублирование кода создания текстуры в 2 ветках

**Где:** `HomeScreen.cpp:211-227` (плитки), `HomeScreen.cpp:329-340` (список — уже через статик кэш).

**В списке** для миниатюр нет — только текст. Но для **обоих режимов** — повторение кода поиска lowercase.

**Фикс:** общий helper `MatchesSearch(name, queryLower)`.

**Оценка:** 10 минут.

---

---

### CR-451. HomeScreen — `onSwitchDoc` не закрывает home для плиток правильно

**Где:** `HomeScreen.cpp:302-303`

```
if (ImGui::Button("Открыть", ...)) {
    if (ctx.onSwitchDoc) ctx.onSwitchDoc(i);
    (*ctx.showHomeScreen) = false;      // ← косвенно
}
```

**Связано с CR-433.** HomeScreen **сам** управляет `showHomeScreen`. Если Application решит оставить home открытым (например, ошибка при switch) — уже поздно.

**Фикс:** `showHomeScreen` убрать из ctx. Application решает по колбэку.

**Оценка:** 20 минут.

---

---

### CR-452. HomeScreen — `ImGui::Selectable(...)` без проверки на одиночный клик в списке

**Где:** `HomeScreen.cpp:420-425`

```
bool sel = ImGui::Selectable(nm.c_str(), false,
    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick);
if (sel && ImGui::IsMouseDoubleClicked(0)) {
    if (ctx.onSwitchDoc) ctx.onSwitchDoc(i);
    (*ctx.showHomeScreen) = false;
}
```

**Работает** только на двойной клик. **Одиночный клик** — ничего. По UX-паттернам — ок, но пользователь может не догадаться.

**Фикс:** добавить подсказку «Двойной клик — открыть» или одиночный клик для select + Enter/двойной — для open.

**Оценка:** 10 минут.

---

---

### CR-454. HomeScreen — 449 строк **одной функцией**

**Где:** весь `DrawHomeScreen`

**Разложение:**
- Топ-полоса Деталь/Сборка/Открыть.
- Левая колонка (лого, версия, 5 кнопок).
- Правая область (заголовок, тумблер, поиск, плитки/список).

**Фикс:** 5-6 приватных helper-функций + структура `HomeLayout`.

**Оценка:** 2 часа.

---

---

### CR-458. `CaptureCurrentThumbnail` — сохраняет в BMP вместо PNG

**Где:** `Thumbnails.cpp:26-30`

```
std::snprintf(filename, sizeof(filename), "thumb_%d.bmp", activeDocIndex);
...
if (view.CaptureToPixmap(320, 240, path)) {
```

**По UI_LAYOUT §2.1** — превью PNG.

**BMP:** без сжатия. Миниатюра 320×240 = **~300 KB**. Для 100 документов = **30 МБ мусора** на диске.

**Плюс:** `CaptureToPixmap(width, height, ...)` **игнорирует** width/height (CR-302). То есть размер не 320×240, а **размер вьюпорта** — может быть 1920×1080 = 8 МБ на одну миниатюру!

**Фикс:**
1. Реализовать `CaptureToPixmap` с реальным размером.
2. Сохранять как PNG через `stb_image_write`.

**Оценка:** 2 часа (правильно).

---

---

### CR-459. `CaptureCurrentThumbnail` — `thumbs.resize(activeDocIndex + 1)` с риском

**Где:** `Thumbnails.cpp:32-34`

```
if ((int)thumbs.size() <= activeDocIndex) {
    thumbs.resize(activeDocIndex + 1);
}
thumbs[activeDocIndex] = path;
```

**Если activeDocIndex = 5** и размер thumbs = 2 → resize до **6**, индексы 2, 3, 4 становятся **пустыми строками**.

**Что это значит:** `GetThumbnailPath(thumbs, 3)` вернёт `""` — правильно. Но в списке `thumbs` есть **пропуски**. Если Application ожидает **плотный** массив — сломается.

**Фикс:** либо плотный массив (всегда поддерживать размер = documentCount), либо `std::map<int, string>`.

**Оценка:** 20 минут.

---

---

### CR-460. `AddRecentFile` — magic 10

**Где:** `Thumbnails.cpp:18`

```
if (recent.size() > 10) recent.resize(10);
```

**Фикс:** `constexpr int kMaxRecentFiles = 10;`.

**Оценка:** 2 минуты.

---

---

### CR-461. `AddRecentFile` — O(N) erase с `std::remove`

**Где:** `Thumbnails.cpp:15-17`

```
recent.erase(std::remove(recent.begin(), recent.end(), path), recent.end());
recent.insert(recent.begin(), path);
```

Для 10 элементов — ок. Но `std::vector::insert(begin(), ...)` — **O(N)** сдвиг.

**WONTFIX** — 10 элементов, не критично.

---

---

### CR-462. `CaptureCurrentThumbnail` — `std::cout` в hot-path

**Где:** `Thumbnails.cpp:37`

```
std::cout << "Thumbnail saved: " << path << "\n";
```

Вызывается на **каждое переключение документа** (из `SwitchToDoc`). Связано с CR-133.

**Оценка:** входит в пакет CR-133.

---

---

### CR-463. `CaptureCurrentThumbnail` — `activeDocIndex` проверяется только на `< 0`

**Где:** `Thumbnails.cpp:20`

```
if (activeDocIndex < 0) return;
```

**Что если `activeDocIndex = 999`**, а документов 3? Превью создастся для несуществующего индекса, `thumbs.resize(1000)` — **пустые строки** в массиве.

**Фикс:** `if (activeDocIndex < 0 || activeDocIndex > MAX) return;`.

**Оценка:** 5 минут.

---

---

### CR-464. `GetThumbnailPath` — не проверяет, что файл существует

**Где:** `Thumbnails.cpp:10-13`

```
std::string GetThumbnailPath(const std::vector<std::string>& thumbs, int docIndex) {
    if (docIndex < 0 || docIndex >= (int)thumbs.size()) return "";
    return thumbs[docIndex];
}
```

Возвращает путь как есть. Проверка `std::filesystem::exists` — **у вызывающего** (`HomeScreen.cpp:206`).

**Фикс:** проверять здесь — тогда `HomeScreen` не делает лишний вызов.

**Оценка:** 5 минут.

---

---

## 🔴 P0 — ui/dialogs (Волна 12, часть 1: RenameDialog)

---

