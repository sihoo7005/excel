# Third-party components

CellBook's own source is MIT licensed. It is an independent application and is not Microsoft Excel.

- Qt 6.8.3: Qt Core, Gui, Widgets, PrintSupport and platform/image plugins. These are dynamically linked. Qt is available under the GNU Lesser General Public License version 3 (LGPL-3.0), with module-specific notices. License texts are in `licenses/`. Qt source: https://download.qt.io/archive/qt/6.8/6.8.3/submodules/ . CellBook makes no modifications to Qt. Users may replace its Qt DLLs with compatible modified libraries. Application source and build instructions are provided for rebuilding and relinking; debugging modified Qt libraries is permitted.
- QXlsx 1.4.9: MIT license. Source is fetched at commit `3192b808a283f8eb18c97b9a087bf085054e9bb0` from https://github.com/QtExcel/QXlsx . License is included in `licenses/QXlsx-MIT.txt`.
- Microsoft Visual C++ redistributable runtime: Microsoft runtime DLLs are included in Windows bundles under the Microsoft Visual Studio redistribution terms, https://visualstudio.microsoft.com/license-terms/ . They are distinct from CellBook and Qt licensing.

Qt's included libraries have additional notices available in Qt source `LICENSES` directories and the Qt distribution. Consult the corresponding source package when redistributing altered bundles.
