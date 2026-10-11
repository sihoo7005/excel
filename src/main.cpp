#include "ui.h"
#include <QApplication>
#include <QMessageBox>
#include <QStyleFactory>
#include <QTimer>
#include <iostream>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("CellBook");
    QApplication::setOrganizationName("CellBook");
    QApplication::setApplicationVersion("1.0.0");
    app.setStyle(QStyleFactory::create("Fusion"));
    CellBookWindow window;
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
