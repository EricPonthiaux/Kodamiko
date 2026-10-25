---
name: qt-widgets
description: Write or change the Qt 6 Widgets code of this project (windows, dialogs, widgets, signals and slots, layouts, models and views) and its CMake build. Use for any user interface work in this Qt Widgets application.
---

# Qt Widgets in this project

The project is a Qt 6 Widgets application built with CMake
(`qt_add_executable`, `qt_standard_project_setup`). Its sources are in `src/`.

## Writing the code

- One class per pair of files (`src/SettingsDialog.h`, `src/SettingsDialog.cpp`),
  added to the `qt_add_executable` list in `CMakeLists.txt`.
- A `QObject` subclass declares `Q_OBJECT`; CMake's AUTOMOC (turned on by
  `qt_standard_project_setup`) compiles it, nothing to add by hand.
- Ownership: a widget created with `new` and a parent belongs to its parent;
  never `delete` it. Anything else goes in a `std::unique_ptr`.
- Connect with the pointer-to-member syntax:
  `connect(button, &QPushButton::clicked, this, &MainWindow::save);` never the
  `SIGNAL()`/`SLOT()` macros.
- Lay widgets out with `QVBoxLayout`, `QHBoxLayout`, `QGridLayout` or
  `QFormLayout`; no fixed positions.
- User-visible strings go through `tr()`.
- Another Qt module (`Network`, `Sql`, `Svg`...): add it to
  `find_package(Qt6 REQUIRED COMPONENTS ...)` and to
  `target_link_libraries`.

## Checking the change

Build with the project's "Build debug" run configuration (or the command in
`.duet/run.json`) and fix every warning and error before saying you are done.
