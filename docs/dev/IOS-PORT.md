# iPhone и iPad — передача между сессиями

Читать вместе с `MOBILE-PORT.md` (Android) и `docs/ADR/0004-mobile-touch-ui.md`:
мобильный интерфейс общий, iOS повторяет шаги Android. Ветка — `feat/ios`, PR в `next`.

## Статус

- **Шаг 1 (ветка `feat/ios`)**: ядро и `bz_tests` собираются под iOS
  (arm64, iOS 16.3+), тесты идут в симуляторе iPad в CI.
- **Шаг 2 (там же)**: `BreezeCAD.app` с 3D-видом детали-примера, жесты
  пальцами; `.ipa` для устройств — в пре-релизе `ios-build`. Проверено на
  iPad Михаила (06.11.2026): ставится через Sideloadly, крутится пальцами.
- **Ловушка статического OCCT**: саморегистрирующиеся фабрики (сеточник
  `BRepMesh_IncrementalMeshFactory`) линкер выбрасывает — без сеточника видны
  только рёбра. Сеточник регистрируется в `IosMain.mm` вручную; на шаге 3
  проверить так же провайдеры обмена данными (STEP/STL через DE).
- Дальше — шаг 3: полное приложение с мобильной раскладкой (см. «Что дальше»).

## Решения (Михаил, 06.11.2026)

| Тема | Решение |
|---|---|
| Графика | Пока **OpenGL ES 3** через EAGL — как на Android, тот же код рендера и OCCT `TKOpenGles`. Apple пометила GLES устаревшим; **потом переезжаем на Metal** |
| Объём захода | Шаги 1 и 2 сразу: ядро + тесты на симуляторе и `.ipa` с 3D-видом |
| Распространение | Сначала `.ipa` для своих устройств (Sideloadly/AltStore с Apple ID); TestFlight — когда будет платный аккаунт разработчика |
| Работа с репо | Ветка `feat/ios`, PR в `next`, Merge жмёт автор |
| Порядок дальше | Сначала шаг 3 (полное приложение на GLES), Metal — после |
| Как делать Metal | Не решено. Предложение: ANGLE (GLES поверх Metal, как в Chrome/Safari) + небольшой патч OCCT, чтобы на iOS он брал контекст EGL, а не EAGL; свой бэкенд Metal для OCCT — месяцы работы |

## Как устроено

- Без Xcode-проекта: та же сборка CMake + Ninja, пресеты `ios-device`
  (`iphoneos`) и `ios-simulator` (`iphonesimulator`), только arm64,
  `CMAKE_OSX_DEPLOYMENT_TARGET=16.3` (ниже libc++ не даёт `std::format`).
- Зависимости: `scripts/ios/build-deps.sh <sdk> <prefix>` — FreeType 2.13.3 +
  OCCT 8.0.1 (как на Android), **статические** (в приложении нельзя
  держать отдельные `.dylib` без фреймворков). ~1 ч на SDK, кэш Actions
  (ключ — SDK + версия Xcode + хэш скрипта).
- `BZ_IOS` — макрос кода, который отличается на iOS (ставится в корневом
  `CMakeLists.txt`, `TargetConditionals.h` не нужен). Правки общего кода:
  `render/Viewport.cpp` (часы анимации без GLFW), `render/CMakeLists.txt`
  (без GLFW), `tests/modules/ModuleTests.cpp` (тест с `std::system` выключен:
  на iOS его нет).
- Приложение — `src/ios/IosMain.mm` (UIKit, ARC): `UIView` со слоем
  `CAEAGLLayer`, контекст EAGL ES 3, свой framebuffer. OCCT получает этот
  контекст и оборачивает привязанный framebuffer как своё «окно»
  (`OpenGl_Window_1.mm`, ветка iOS); вид рисует в свой framebuffer, кадр
  копируется в окно `glBlitFramebuffer` — как на Android. Рисует
  `CADisplayLink`, только когда есть что рисовать. В фоне — ни одного вызова
  OpenGL (iOS за это закрывает приложение).
- Жесты (курсор выкл.): 1 палец — вращение, 2 — сдвиг + щипок, двойное
  касание — показать всё. Телефон — всегда альбомная, планшет — любая.
- Пакет: `scripts/ios/package-app.sh` — `BreezeCAD.app` (исполняемый файл,
  `resources/ios/Info.plist.in`, пример `examples/bracket.bzc`, иконка из
  `assets/brand/breezecad.icns`), временная подпись (ad-hoc); для устройств —
  `BreezeCAD-ios.ipa`. Bundle id — `io.github.mikeverx.breezecad`, как у APK.
- Лог — `Library/Application Support/logs/output.log` в контейнере приложения
  и stderr (в CI — `simctl launch --stderr`).
- CI: `.github/workflows/ios.yml`, отдельно от `build.yml` и `android.yml`.
  Симулятор: `scripts/ios/simulator.sh tests` (тесты: симулятор видит файлы
  Мака, пути `BZ_SOURCE_DIR` работают как есть) и `smoke` (установка,
  запуск, деталь построена, снимок экрана, процесс жив).

## Установка на свой iPhone/iPad (без Мака)

1. Скачать `BreezeCAD-ios.ipa` из пре-релиза `ios-build`.
2. На Windows: iTunes + Sideloadly; устройство кабелем, «Доверять этому компьютеру».
3. Перетащить `.ipa` в Sideloadly, ввести Apple ID (лучше отдельный), Start.
4. На устройстве: Настройки → Конфиденциальность и безопасность → Режим
   разработчика (пункт появляется после первой установки); Основные →
   в самом низу «VPN и управление устройством» → почта Apple ID → Доверять.
   iMazing и iTunes не подходят: они не переподписывают `.ipa`.
5. Бесплатный Apple ID: приложение работает 7 дней, до 3 таких приложений,
   до 10 App ID за 7 дней. Ставить заново поверх (не удалять — пропадут данные).

## Что дальше

- **Шаг 3 — полное приложение.** Мобильная раскладка (`app/mobile`) общая, но
  обвязка Android (`app/android`, `platform/*_android.cpp`, `platform/Android.h`)
  завязана на `__ANDROID__` (~70 мест). Предложение автору: общий макрос
  «мобильная версия» и интерфейс платформы (файлы, клавиатура, вырезы,
  буфер обмена, «поделиться»), под ним Android (JNI) и iOS (UIKit).
  Обсудить до начала.
- Окно iOS для `Application`: `platform/Window_ios.mm` (EAGL, как
  `Window_android.cpp`), цикл программы — на своём потоке, события UIKit — в
  очередь (как `android_native_app_glue`).
- Файлы — `UIDocumentPickerViewController` (аналог SAF), «Папка проектов» —
  security-scoped bookmark.
- Metal: ANGLE (GLES поверх Metal) или собственный бэкенд — после шага 3.
- TestFlight: платный аккаунт Apple Developer, секреты подписи в CI, fastlane.
- Хвост: `HostInfo::Detect` на iOS говорит `os = "macos"` (как Android —
  `linux`): решать с каталогом модулей. На iOS скачиваемый нативный код
  запрещён — только модули-данные (ADR-0005).
