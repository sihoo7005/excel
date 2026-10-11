#include "ui.h"
#include <QApplication>
#include <QFontDatabase>
#include <QMessageBox>
#include <QPixmap>
#include <QStyleFactory>
#include <QTimer>
#include <iostream>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("CellBook");
    QApplication::setOrganizationName("CellBook");
    QApplication::setApplicationVersion("1.0.0");
    app.setStyle(QStyleFactory::create("Fusion"));
    const auto fontPath = qEnvironmentVariable("CELLBOOK_FONT_PATH");
    if (!fontPath.isEmpty())
        QFontDatabase::addApplicationFont(fontPath);
    CellBookWindow window;
    auto args = app.arguments();
    int shot = args.indexOf("--screenshot");
    if (shot >= 0 && args.size() > shot + 2) {
        if (!window.loadFile(args[shot + 1], false))
            return 1;
        window.show();
        QTimer::singleShot(150, &window, [&] { app.exit(window.grab().save(args[shot + 2]) ? 0 : 1); });
        return app.exec();
    }
    if (app.arguments().contains("--self-test")) {
        try {
            window.selfTest();
            std::cout << "GUI checks passed\n";
            return 0;
        } catch (const std::exception &e) {
            std::cerr << e.what() << '\n';
            return 1;
        }
    }
    window.show();
    if (app.arguments().size() > 1)
        window.loadFile(app.arguments().at(1));
    return app.exec();
}
