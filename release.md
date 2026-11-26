<!-- throned:lang=en -->
## What's new

Most of this release is a catch-up with Throne, the project Throned is forked from. Our own changes this time are about design.

### From Throned

- Menus, buttons, fields, drop-downs, checkboxes and tabs in every dialog are drawn in the application's own style instead of the stock Qt look.
- Routing by application is now in the Routing settings window: the new Apps and sites page lists the programs found on this computer and lets you send each one directly, through the VPN or through a specific server. It is the same screen simple mode has, saved with the window's Save button.
- That screen no longer flickers or jumps while it loads, and its cards and the switch between simple mode's pages are animated.
- The screens that came from Throne are fitted to Throned's design and translated.
- In simple mode's server list, right-clicking a group updates its subscription.

### From Throne

These arrived by merging the upstream project.

- The route profile editor's Advanced tab is rebuilt around one rule list with a rule editor beside it, quick lists, and a switch on every rule. Remote route profiles keep your local rules.
- DNS servers are edited as lists, with several servers per role. A rule tester shows which rule a domain, address or program matches.
- DNS tunnel profiles are supported.
- The scanner now finds working WARP endpoints and DNS resolvers. WARP settings are gathered on one Exit page.
- Profiles can be pinned, connection columns can be resized, download and upload speeds are coloured, and the tray icon shows the connection state.
- Added a service availability check, a subscription profile filter, more auto selector options and a program picker for process rules.
- On Windows the core is elevated instead of the whole application.
- The core moved to sing-box 1.15, with fixes for WireGuard, OpenVPN one-time passwords, Xray validation and macOS DNS cleanup.
- Removed: Tor, Psiphon and Juicity, the IP scanner section, the "Only Country" speed test mode, the option to disable traffic statistics, and the keyword log filters (one "hide lines matching" expression replaces them).
<!-- throned:lang=end -->

<details>
<summary>Русский</summary>

<!-- throned:lang=ru -->
## Что нового

Большая часть этого выпуска пришла из Throne, проекта, от которого ответвлён Throned. Наши собственные изменения в этот раз касаются оформления.

### От Throned

- Меню, кнопки, поля, выпадающие списки, флажки и вкладки во всех диалогах рисуются в стиле приложения, а не в стандартном виде Qt.
- Маршрутизация по приложениям теперь есть в окне настроек маршрутизации: новая страница «Приложения» показывает программы, найденные на компьютере, и позволяет пустить каждую напрямую, через VPN или через конкретный сервер. Это тот же экран, что в простом режиме; сохраняется общей кнопкой окна.
- Этот экран больше не мигает и не прыгает при загрузке, а карточки и переход между страницами простого режима стали плавными.
- Экраны, пришедшие из Throne, приведены к оформлению Throned и переведены.
- В списке серверов простого режима правый щелчок по группе обновляет её подписку.

### Из Throne

Это пришло при слиянии с исходным проектом.

- Вкладка «Расширенный» в редакторе профиля маршрутизации переделана: один список правил с редактором рядом, быстрые списки и выключатель у каждого правила. Удалённые профили маршрутизации сохраняют ваши локальные правила.
- DNS-серверы задаются списками, по несколько на роль. Тестер правил показывает, под какое правило попадает домен, адрес или программа.
- Поддерживаются профили DNS-туннелей.
- Сканер теперь ищет рабочие адреса WARP и DNS-резолверы. Настройки WARP собраны на одной странице «Выход».
- Профили можно закреплять, столбцы подключений — растягивать, скорости загрузки и отдачи выделены цветом, а значок в трее показывает состояние подключения.
- Добавлены проверка доступности сервисов, фильтр профилей подписки, новые параметры автовыбора и выбор программы для правил по процессам.
- В Windows с правами администратора запускается ядро, а не всё приложение.
- Ядро переведено на sing-box 1.15, с исправлениями для WireGuard, одноразовых паролей OpenVPN, проверки конфигурации Xray и очистки DNS на macOS.
- Убрано: Tor, Psiphon и Juicity, раздел IP-сканера, режим теста скорости «только страна», отключение статистики трафика и фильтры логов по ключевым словам (вместо них одно выражение «скрывать строки»).
<!-- throned:lang=end -->

</details>
