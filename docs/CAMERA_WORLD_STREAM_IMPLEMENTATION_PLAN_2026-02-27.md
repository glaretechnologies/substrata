# ПЛАН ВНЕДРЕНИЯ ВНУТРИМИРОВОЙ КАМЕРЫ И ТРАНСЛЯЦИИ (Metasiberia)

Дата: 2026-02-27
Область: только планирование, без изменений кода в этом документе.

## 1) Цель

Реализовать систему внутриworld-камеры, чтобы:

- пользователь мог добавить объект камеры через меню Edit (`Add Camera`);
- направление камеры задавалось трансформом объекта (позиция/поворот);
- изображение камеры показывалось на объекте-экране в мире;
- в трансляции было именно текущее состояние мира Metasiberia (не внешнее видео);
- текущее рабочее поведение продакшена не ломалось.

## 2) Что уже есть сейчас (подтверждено в коде)

- Типы объектов: generic/hypercard/voxel/spotlight/webview/video/text/portal/seat.
  - `shared/WorldObject.h`
- Поток создания объектов через `Add ...` уже есть и стабилен.
  - `gui_client/MainWindow.ui`, `gui_client/MainWindow.h`, `gui_client/MainWindow.cpp`
- Есть per-type данные объектов (spotlight + seat) через length-prefixed type-data.
  - `shared/WorldObject.cpp` (`writeWorldObjectPerTypeData`, `readWorldObjectPerTypeData`)
- Рендер-движок уже умеет camera transform и framebuffer target.
  - `glare-core/opengl/OpenGLEngine.h`, `glare-core/opengl/OpenGLEngine.cpp`
- Уже есть паттерны динамической подмены текстуры (video/webview/animated texture).
  - `gui_client/AnimatedTextureManager.cpp`, `gui_client/WebViewData.cpp`
- Серверный `CreateObject` реализован обобщенно и с проверками прав.
  - `server/WorkerThread.cpp`

## 3) Варианты архитектуры

### Вариант A: внешний стриминг-сервис + существующий `Video` объект

- Вывод камеры уходит во внешний RTMP/WebRTC сервис, затем в мире проигрывается URL через `Video`.
- Плюсы: минимум изменений в движке.
- Минусы: это не нативная внутренняя камера мира, добавляется внешняя инфраструктура, задержки и сложность авторизации.

### Вариант B: серверный dynamic texture updater (существующий XML-скрипт)

- Использовать текущий pipeline `<dynamic_texture_update>`.
- Плюсы: уже существует.
- Минусы: период обновления примерно часовой, не realtime; для живой трансляции непригодно.

### Вариант C (рекомендуется): нативный in-engine render-to-texture

- Каждый клиент локально рендерит вид с камеры из состояния мира и применяет результат к материалу экрана.
- Плюсы: реальная картина "что происходит в Metasiberia", без серверного видеотрафика и внешних зависимостей.
- Минусы: нужны новые типы объектов, менеджер рендера и ограничения производительности.

## 4) Рекомендуемая форма фичи (MVP)

Использовать два новых типа объектов для чистого UX и чтобы не перегружать семантику WebView/Video:

- `ObjectType_Camera` (источник)
- `ObjectType_CameraScreen` (приемник/экран)

MVP-поток пользователя:

1. Нажать `Add Camera`.
2. Клиент создает два объекта: `Camera` и `CameraScreen` (каждый получает свой `UID` от сервера).
3. После получения `UID` камеры клиент автоматически записывает его в `source_camera_uid` у экрана.
4. Экран сразу показывает поток с созданной камеры.

Опционально в следующей фазе: добавить режим ручного создания `Add Camera Screen` (advanced), если нужен отдельный экран без автосоздания пары.

## 5) Изменения модели данных

## 5.1 `WorldObject` enum и число типов

- Добавить в `shared/WorldObject.h`:
  - `ObjectType_Camera`
  - `ObjectType_CameraScreen`
- Увеличить `NUM_OBJECT_TYPES`.

## 5.2 Per-type структуры (только POD)

В `shared/WorldObject.h` внутри `TypeData` union:

- `CameraTypeData`
  - `float fov_y_rad`
  - `float near_dist`
  - `float far_dist`
  - `uint16 render_width`
  - `uint16 render_height`
  - `uint8 max_fps`
  - `uint8 enabled`
- `CameraScreenTypeData`
  - `uint64 source_camera_uid`
  - `uint16 material_index`
  - `uint8 enabled`

Примечание: в union использовать POD-примитивы, не `UID`-класс.

## 5.3 Сериализация и значения по умолчанию

Обновить в `shared/WorldObject.cpp`:

- `objectTypeString()` / `objectTypeForString()`
- `writeWorldObjectPerTypeData()`
- `readWorldObjectPerTypeData()`
- `setWorldObjectPerTypeDataDefaults()`
- XML persistence в `serialiseToXML()` / `loadFromXMLElem()` для полей камеры

Также расширить `WorldObject::test()` для roundtrip-проверки новых type-data.

## 6) Стратегия протокола и совместимости

Так как сервер деплоится отдельно, а типы объектов сохраняются в БД:

- Поднять протокол до `50` в `shared/Protocol.h`.
- Для `Add Camera` проверять `server_protocol_version >= 50`.
- Если сервер старее, показывать явную ошибку (по аналогии с проверкой seat).

Рекомендуемый порядок релиза:

1. Сначала деплой сервера с поддержкой протокола 50.
2. Затем релиз нового инсталлятора клиента.
3. После этого включать camera-объекты в прод-миры.

## 7) Изменения UI клиента

## 7.1 Пункты меню

Файлы:

- `gui_client/MainWindow.ui`
- `gui_client/MainWindow.h`
- `gui_client/MainWindow.cpp`

Добавить действия:

- `Add Camera`

Хендлеры создания должны повторять стиль `Add Seat`:

- проверка подключения;
- проверка версии протокола;
- проверка прав записи в parcel;
- создание `Camera` с default-полями и отправка `Protocol::CreateObject`;
- ожидание реального `UID` камеры от сервера;
- создание `CameraScreen`, установка `source_camera_uid` в UID камеры, отправка `Protocol::CreateObject`.

## 7.2 Группы в Object Editor

Файлы:

- `gui_client/ObjectEditor.ui`
- `gui_client/ObjectEditor.h`
- `gui_client/ObjectEditor.cpp`

Добавить группы:

- `Camera`:
  - enabled
  - FOV
  - near/far
  - preset разрешения
  - max FPS
- `Camera Screen`:
  - enabled
  - source camera UID
  - material index

Обновить оба направления:

- `setFromObject()`
- `toObject()`
- переключение видимости по object type (в том же стиле, что Seat/Spotlight)

## 8) Реализация рендера (client-side)

## 8.1 Новый manager-класс

Добавить файлы:

- `gui_client/WorldCameraManager.h`
- `gui_client/WorldCameraManager.cpp`

Назначение:

- держать GPU render targets по camera UID;
- планировать обновления по `max_fps`;
- рендерить вид камеры в текстуры;
- привязывать итоговую текстуру к материалам связанных экранов.

Интеграция:

- `gui_client/GUIClient.h` (member + наборы объектов)
- `gui_client/GUIClient.cpp` (init/process/cleanup)
- `gui_client/CMakeLists.txt` (новые cpp/h)

## 8.2 Где запускать рендер

Выполнять camera render pass в main thread с текущим GL context, внутри GUI frame loop после обновления мира и до финального кадра на экран.

Рекомендуемая точка интеграции: путь `GUIClient::timerEvent(...)`, где context уже активен через `MainWindow::timerEvent`.

## 8.3 Как считать transform камеры

Для каждого активного `ObjectType_Camera`:

- `cam_pos = ob.pos`;
- извлечь базис из `obToWorldMatrix` (right/forward/up);
- собрать matrix world->camera с рядами `(right, forward, up)`;
- вызвать `OpenGLEngine::setPerspectiveCameraTransform(...)`.

Параметры брать из `CameraTypeData`, с clamp к безопасным пределам.

## 8.4 Render target и привязка к экрану

Для каждого camera UID:

- держать double-buffered текстуры (A/B) + framebuffer, чтобы не читать и писать в одну и ту же текстуру;
- рендерить новый кадр в back buffer;
- назначать front buffer всем связанным `CameraScreen` материалам (обычно emission texture);
- вызывать `opengl_engine->materialTextureChanged(...)` для затронутых материалов.

## 8.5 Ограничители производительности

Пределы MVP:

- default разрешение: `512x288`;
- default max FPS: `10`;
- hard max разрешение: `1280x720`;
- hard max числа камер, рендеримых за кадр: настраиваемый cap (например, 2).

Рендерить только камеры, которые реально нужны хотя бы одному активному экрану в process distance/visible set.

## 9) Ветки загрузки объектов

Обновить `GUIClient::loadModelForObject(...)` в `gui_client/GUIClient.cpp`:

- добавить ветку для `ObjectType_Camera`
  - простая camera mesh + physics shape
- добавить ветку для `ObjectType_CameraScreen`
  - screen mesh (в стиле image-plane/cube), материал подготовлен под emission texture

Обновить cleanup-пути:

- при удалении объекта убирать ссылки camera/screen;
- при server reconnect и очистке мира сбрасывать ресурсы `WorldCameraManager`.

## 10) Стратегия мешей

Допустимы два пути:

- быстрый: procedural mesh в `gui_client/MeshBuilding.cpp` (`makeCameraMesh`, `makeCameraScreenMesh`);
- asset-путь: добавить модель камеры в ресурсы (`.bmesh`) и грузить как spotlight/portal.

Рекомендация для MVP: сначала procedural mesh (меньше зависимостей при деплое), ресурсную модель добавить позже как косметическое улучшение.

## 10.1 Текущее расположение модели камеры (факт на 2026-02-27)

- runtime-модель камеры (использовать в коде загрузки): `C:\programming\substrata\resources\camera.bmesh`
- исходниковая копия ассета: `C:\programming\substrata\source_resources\models\camera.bmesh`
- источник, откуда взят конвертированный файл: `C:\Users\densh\AppData\Roaming\Cyberspace\resources\camera_glb_8812442614189813304.bmesh`

Примечание:

- для встроенного объекта камеры основным считается путь из `resources`;
- копия в `source_resources/models` хранится как исходниковый артефакт проекта.

## 11) Безопасность и антирегресс

- Не трогать логику seat и существующее поведение Object Editor вне camera-веток.
- Держать camera-логику изолированной в новом manager-классе.
- Делать clamp невалидных camera type-data на load/update.
- В режиме screenshot slave (`--screenshotslave`) отключить world camera streaming pass.

## 12) План тестирования

## 12.1 Локальный функционал

1. `Add Camera` -> объект появляется.
2. Автоматически появляется связанный `CameraScreen`.
3. У экрана автоматически установлен `source_camera_uid`.
4. Повернуть/переместить камеру -> обновляется ракурс.
5. Изменить FOV/разрешение/FPS -> эффект виден и стабилен.
6. Редактирование материалов экрана (opacity/hologram и т.д.) продолжает работать.

## 12.2 Мультиплеер/prod-like

1. Два клиента в одном мире, одни и те же camera+screen объекты.
2. Оба клиента видят согласованное поведение потоков.
3. После reconnect сохраняются связи и настройки.
4. После рестарта сервера настройки камеры и связи не теряются.

## 12.3 Производительность

1. Влияние на FPS: 1 camera + 1 screen.
2. Стресс: 3 камеры + 3 экрана.
3. Проверка утечек GPU-памяти на циклах create/delete.

## 12.4 Сборка

Использовать стандартную команду проекта:

- `powershell -ExecutionPolicy Bypass -File C:\programming\qt_build.ps1`

## 13) Фазы внедрения

- Фаза 1: модель данных + создание объектов + editor (без live-render, placeholder-экран).
- Фаза 2: render manager + живая привязка текстуры.
- Фаза 3: performance caps + polish + crash hardening.
- Фаза 4: опционально добавить ручной `Add Camera Screen` (advanced) и дополнительные UX-настройки.

## 14) Риски и меры

- Риск: высокая GPU-нагрузка при большом числе камер.
  - Мера: жесткие cap, FPS-throttling на камеру, visibility gating.
- Риск: несовпадение протокола при отдельном деплое сервера.
  - Мера: gating на протокол 50 в UI и порядок rollout "server first".
- Риск: проблемы persistence, если старый сервер не понимает новые type string.
  - Мера: не разрешать создание camera-объектов при server protocol < 50.
- Риск: регресс существующих object type.
  - Мера: изоляция camera-веток и целевой регрессионный чеклист.

## 15) Ответы на ключевые вопросы

1. Делать один объект или два: для MVP надежнее два (`Camera` + `CameraScreen`), это проще в редакторе и безопаснее для текущей архитектуры.
2. Нужно ли сразу добавлять модель камеры в ресурсы: можно начать с procedural mesh, но в текущем состоянии уже подготовлен `camera.bmesh` в `resources` и `source_resources/models`.
3. Как "забирать изображение": рендерить сцену с точки камеры в offscreen framebuffer (FBO) в texture.
4. Как "поместить на экран": эту texture назначать в материал `CameraScreen` (обычно в emission), затем вызывать `materialTextureChanged(...)`.
5. Добавлять ли два объекта одной кнопкой: да, в MVP `Add Camera` сразу создает пару (`Camera` + `CameraScreen`) и автоматически связывает их по `source_camera_uid`.

## 16) Что вне MVP

- сетевое кодирование/транспорт видео между клиентами;
- аудиозахват с camera-объекта;
- сложные пост-эффекты на камеру;
- parity для webclient в первой итерации.

