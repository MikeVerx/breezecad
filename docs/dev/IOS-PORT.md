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
- **Шаг 3** — полная программа с мобильной раскладкой: см. ниже.

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

## Шаг 3 — полная программа (ветка `feat/mobile`, PR #87 поверх #85)

Михаил, 06.11.2026: «делай общую мобильную ветку и в ней раздели по андроид и иос».

- **Общий слой.** `BZ_MOBILE` (Android и iOS) вместо `__ANDROID__` там, где
  речь о телефоне и планшете; `__ANDROID__` / `BZ_IOS` — только своё.
  `platform/Mobile.h` — интерфейс системы (папки, экран, клавиатура, файлы,
  «поделиться», сбои, буфер обмена): Android — `platform/Android.cpp`, iOS —
  `platform/Mobile_ios.mm`. Только Android — `platform/Android.h`, только
  iOS — `platform/Ios.h`. `android::X` в коде Android по-прежнему работает.
- **Application.** Общее — `Application_mobile.cpp` (между кадрами, клавиатура,
  фон, прошлый сбой, тестовые команды); своё — `StartMobile` и
  `FeedSystemInput` в `Application_android.cpp` / `Application_ios.mm`.
- **Окно iOS** — `platform/Window_ios.mm`: вид приложения (`CAEAGLLayer`, EAGL
  ES 3), свой framebuffer (на iOS нет framebuffer 0: `ReclaimDrawable`
  привязывает его обратно после 3D-вида). **Программа идёт на главном
  потоке** и крутит его run loop в `WaitEvents` (`CFRunLoopRunInMode`), как
  SDL на iOS: касания, клавиатура и ответы системы приходят между кадрами
  на том же потоке, без очередей между потоками.
- **Вход** — `app/ios/IosMain.mm`: UIKit-приложение, вид с пальцами и Apple
  Pencil (`UITouch` → `Window::Touch` → `mobile::PointerEvent`), системная
  клавиатура (`UIKeyInput`; Enter/Backspace — кодами Android, как в
  `Mobile.h`), уход в фон (никакого OpenGL в фоне), отчёт о сбое.
- **Файлы.** Папка проектов — «Документы» приложения: с `UIFileSharingEnabled`
  и `LSSupportsOpeningDocumentsInPlace` «Файлы» показывают её как
  «На iPad → BreezeCAD». Документы открываются и сохраняются на месте (без
  копий, как нужно на Android для SAF); «Открыть другой…» —
  `UIDocumentPicker` (копия), «Сохранить как» — в ту же папку под свободным
  именем.
- **Сбои.** Сторожевого процесса на iOS нет: отчёт пишет обработчик сигнала
  (`IosMain.mm`), следующий запуск показывает его (`CheckLastExit`).
- **Проверка в CI:** симулятор запускает программу трижды с тестовыми
  командами (`BZ_TEST_COMMANDS`: главная, пример, эскиз) — лог, снимок, жива ли.

## Что дальше

- Проверить на живом iPad: жесты, курсор-капля, нампад, клавиатура, Pencil,
  «Файлы», «поделиться».
- Аппаратная клавиатура iPad (`pressesBegan` → сочетания клавиш), наведение
  Pencil и трекпада (`UIHoverGestureRecognizer`).
- Скругление углов экрана и «вырез» iPhone — сейчас только безопасная зона.
- Metal: ANGLE (GLES поверх Metal) или собственный бэкенд — после шага 3.
- TestFlight: платный аккаунт Apple Developer, секреты подписи в CI, fastlane.
- Хвост: `HostInfo::Detect` на iOS говорит `os = "macos"` (как Android —
  `linux`): решать с каталогом модулей. На iOS скачиваемый нативный код
  запрещён — только модули-данные (ADR-0005).
