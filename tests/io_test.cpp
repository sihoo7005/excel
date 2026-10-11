#include "io.h"
#include <QApplication>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
using namespace grid;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QTemporaryDir dir;
    Workbook b;
    b.sheets[0].cells[{0, 0}].raw = "12";
    b.sheets[0].cells[{1, 0}].raw = "=A1*2";
    auto &c = b.sheets[0].cells[{0, 1}];
    c.raw = "'0012";
    c.style.bold = true;
    c.style.background = "#ffeeaa";
    b.sheets[0].cells[{2, 1}].raw = "'한글,\"값\"\n다음";
    b.sheets.push_back({"두 번째"});
    b.sheets[1].cells[{0, 0}].raw = "=Sheet1!A2";
    b.sheets[0].widths[0] = 140;
    b.sheets[0].merges.push_back({{4, 0}, {4, 2}});
    b.sheets[0].charts.push_back({"예시", 0, {0, 0}, {1, 1}});
    for (auto ext : {"cellbook", "xlsx"}) {
        auto path = dir.filePath(QString("roundtrip.") + ext);
        auto error = saveWorkbook(path, b);
        if (!error.isEmpty())
            throw std::runtime_error(error.toStdString());
        Workbook read;
        error = loadWorkbook(path, read);
        if (!error.isEmpty())
            throw std::runtime_error(error.toStdString());
        check(read.sheets.size() == 2, "sheet count");
        Calculator calc(read);
        check(calc.get(1, {0, 0}).number == 24, "cross formula persisted");
        check(calc.get(0, {0, 1}).display() == "0012", "text persisted");
        check(read.sheets[0].cells.at({0, 1}).style.bold, "bold persisted");
        check(read.sheets[0].merges.size() == 1, "merge persisted");
    }
    auto path = dir.filePath("data.csv");
    check(saveWorkbook(path, b).isEmpty(), "csv save");
    Workbook read;
    check(loadWorkbook(path, read).isEmpty(), "csv load");
    Calculator calc(read);
    check(calc.get(0, {2, 1}).display() == "한글,\"값\"\n다음", "csv escaped");
    std::cout << "File roundtrip checks passed\n";
}
