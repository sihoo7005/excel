#include "io.h"
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>
#include <xlsxcell.h>
#include <xlsxcellformula.h>
#include <xlsxcelllocation.h>
#include <xlsxcellrange.h>
#include <xlsxchart.h>
#include <xlsxdocument.h>
namespace grid {
namespace {
QString q(const std::string &s) {
    return QString::fromUtf8(s.data(), int(s.size()));
}
std::string utf(const QString &s) {
    return s.toUtf8().toStdString();
}
QJsonObject styleJson(const Style &s) {
    return {{"font", q(s.font)},
            {"size", s.size},
            {"foreground", q(s.foreground)},
            {"background", q(s.background)},
            {"numberFormat", q(s.numberFormat)},
            {"alignment", s.alignment},
            {"bold", s.bold},
            {"italic", s.italic},
            {"underline", s.underline},
            {"border", s.border},
            {"wrap", s.wrap}};
}
Style styleFromJson(const QJsonObject &o) {
    Style s;
    s.font = utf(o.value("font").toString("Malgun Gothic"));
    s.size = std::clamp(o.value("size").toInt(11), 6, 96);
    s.foreground = utf(o.value("foreground").toString("#202c38"));
    s.background = utf(o.value("background").toString());
    s.numberFormat = utf(o.value("numberFormat").toString());
    s.alignment = std::clamp(o.value("alignment").toInt(), 0, 3);
    s.bold = o.value("bold").toBool();
    s.italic = o.value("italic").toBool();
    s.underline = o.value("underline").toBool();
    s.border = o.value("border").toBool();
    s.wrap = o.value("wrap").toBool();
    return s;
}
QXlsx::Format xformat(const Style &s) {
    QXlsx::Format f;
    f.setFontName(q(s.font));
    f.setFontSize(s.size);
    f.setFontBold(s.bold);
    f.setFontItalic(s.italic);
    f.setFontUnderline(s.underline ? QXlsx::Format::FontUnderlineSingle : QXlsx::Format::FontUnderlineNone);
    f.setFontColor(QColor(q(s.foreground)));
    if (!s.background.empty()) {
        f.setFillPattern(QXlsx::Format::PatternSolid);
        f.setPatternBackgroundColor(QColor(q(s.background)));
    }
    f.setNumberFormat(q(s.numberFormat));
    f.setTextWrap(s.wrap);
    f.setVerticalAlignment(QXlsx::Format::AlignVCenter);
    if (s.alignment)
        f.setHorizontalAlignment(s.alignment == 1   ? QXlsx::Format::AlignLeft
                                 : s.alignment == 2 ? QXlsx::Format::AlignHCenter
                                                    : QXlsx::Format::AlignRight);
    if (s.border) {
        f.setBorderStyle(QXlsx::Format::BorderThin);
        f.setBorderColor(QColor("#a5b2bf"));
    }
    return f;
}
Style xstyle(const QXlsx::Format &f) {
    Style s;
    if (!f.fontName().isEmpty())
        s.font = utf(f.fontName());
    if (f.fontSize() > 0)
        s.size = f.fontSize();
    s.bold = f.fontBold();
    s.italic = f.fontItalic();
    s.underline = f.fontUnderline() != QXlsx::Format::FontUnderlineNone;
    if (f.fontColor().isValid())
        s.foreground = utf(f.fontColor().name());
    if (f.fillPattern() != QXlsx::Format::PatternNone) {
        auto color = f.patternBackgroundColor();
        if (!color.isValid())
            color = f.patternForegroundColor();
        if (color.isValid())
            s.background = utf(color.name());
    }
    s.numberFormat = utf(f.numberFormat());
    s.wrap = f.textWrap();
    auto a = f.horizontalAlignment();
    s.alignment = a == QXlsx::Format::AlignLeft      ? 1
                  : a == QXlsx::Format::AlignHCenter ? 2
                  : a == QXlsx::Format::AlignRight   ? 3
                                                     : 0;
    s.border = f.bottomBorderStyle() != QXlsx::Format::BorderNone ||
               f.topBorderStyle() != QXlsx::Format::BorderNone ||
               f.leftBorderStyle() != QXlsx::Format::BorderNone ||
               f.rightBorderStyle() != QXlsx::Format::BorderNone;
    return s;
}
QString validateName(const QString &s) {
    if (s.isEmpty() || s.size() > 31 || s.contains(QRegularExpression("[\\\\/*?:\\[\\]]")))
        return "잘못된 시트 이름입니다.";
    return {};
}
} // namespace
QString loadWorkbook(const QString &path, Workbook &book) {
    auto ext = QFileInfo(path).suffix().toLower();
    Workbook out;
    out.sheets.clear();
    if (ext == "xlsx") {
        QXlsx::Document doc(path);
        if (!doc.load())
            return "Excel 파일을 읽을 수 없습니다.";
        size_t count = 0;
        for (auto name : doc.sheetNames()) {
            doc.selectSheet(name);
            auto *w = doc.currentWorksheet();
            if (!w)
                continue;
            Sheet s;
            s.name = utf(name);
            int mr = 0, mc = 0;
            auto locations = w->getFullCells(&mr, &mc);
            count += size_t(locations.size());
            if (count > 1000000)
                return "셀 수가 너무 많습니다. 한 통합문서에 최대 1,000,000개의 저장된 셀을 지원합니다.";
            s.rows = std::min(MaxRows, std::max(1000, mr + 50));
            s.cols = std::min(MaxCols, std::max(100, mc + 10));
            std::map<int, std::pair<Position, std::string>> shared;
            for (auto &loc : locations)
                if (loc.cell->hasFormula()) {
                    auto f = loc.cell->formula();
                    if (f.formulaType() == QXlsx::CellFormula::SharedType && !f.formulaText().isEmpty())
                        shared[f.sharedIndex()] = {{loc.row - 1, loc.col - 1}, "=" + utf(f.formulaText())};
                }
            for (auto &loc : locations) {
                Cell c;
                auto cell = loc.cell;
                c.style = xstyle(cell->format());
                auto v = cell->value();
                if (cell->hasFormula()) {
                    auto f = cell->formula();
                    c.raw = "=" + utf(f.formulaText());
                    if (c.raw == "=" && f.formulaType() == QXlsx::CellFormula::SharedType) {
                        auto i = shared.find(f.sharedIndex());
                        if (i != shared.end())
                            c.raw = translateFormula(i->second.second, loc.row - 1 - i->second.first.row,
                                                     loc.col - 1 - i->second.first.col);
                        else
                            c.raw = "=#REF!";
                    }
                } else if (cell->cellType() == QXlsx::Cell::BooleanType)
                    c.raw = v.toBool() ? "TRUE" : "FALSE";
                else if (cell->cellType() == QXlsx::Cell::NumberType)
                    c.raw = utf(QString::number(v.toDouble(), 'g', 15));
                else if (cell->cellType() == QXlsx::Cell::ErrorType)
                    c.raw = "=" + utf(v.toString());
                else
                    c.raw = "'" + utf(v.toString());
                s.cells[{loc.row - 1, loc.col - 1}] = std::move(c);
            }
            for (int col = 1; col <= mc; ++col) {
                auto width = doc.columnWidth(col);
                if (width > 0)
                    s.widths[col - 1] = int(width * 7 + 5);
            }
            for (int row = 1; row <= mr; ++row) {
                auto height = doc.rowHeight(row);
                if (height > 0)
                    s.heights[row - 1] = int(height * 96 / 72);
            }
            for (auto &r : w->mergedCells())
                s.merges.push_back(
                    {{r.firstRow() - 1, r.firstColumn() - 1}, {r.lastRow() - 1, r.lastColumn() - 1}});
            out.sheets.push_back(std::move(s));
        }
    } else {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return file.errorString();
        if (file.size() > 100 * 1024 * 1024)
            return "파일이 100 MB 제한을 초과합니다.";
        auto bytes = file.readAll();
        if (ext == "csv" || ext == "tsv") {
            Sheet s;
            s.name = "Sheet1";
            std::vector<std::vector<std::string>> rows;
            try {
                rows = parseDelimited(bytes.toStdString(), ext == "tsv" ? '\t' : ',');
            } catch (...) {
                return "CSV의 따옴표가 올바르지 않습니다.";
            }
            if (rows.size() > MaxRows)
                return "행 수 제한을 초과합니다.";
            size_t count = 0;
            for (size_t r = 0; r < rows.size(); ++r) {
                if (rows[r].size() > MaxCols)
                    return "열 수 제한을 초과합니다.";
                s.cols = std::max(s.cols, int(rows[r].size()));
                for (size_t c = 0; c < rows[r].size(); ++c)
                    if (!rows[r][c].empty()) {
                        if (++count > 1000000)
                            return "셀 수 제한을 초과합니다.";
                        auto raw = rows[r][c];
                        if (raw[0] == '=' || raw[0] == '+' || raw[0] == '-' || raw[0] == '@') {
                            double n;
                            char *e = nullptr;
                            n = std::strtod(raw.c_str(), &e);
                            (void)n;
                            if (e != raw.c_str() + raw.size())
                                raw = "'" + raw;
                        }
                        s.cells[{int(r), int(c)}].raw = raw;
                    }
            }
            s.rows = std::max(1000, int(rows.size()) + 20);
            out.sheets.push_back(std::move(s));
        } else if (ext == "cellbook") {
            QJsonParseError error;
            auto json = QJsonDocument::fromJson(bytes, &error);
            if (error.error != QJsonParseError::NoError || !json.isObject())
                return "통합문서 형식이 올바르지 않습니다.";
            auto root = json.object();
            if (root.value("format").toString() != "CellBook" || root.value("version").toInt() != 1)
                return "지원하지 않는 통합문서 버전입니다.";
            auto sheets = root.value("sheets").toArray();
            if (sheets.isEmpty() || sheets.size() > 256)
                return "시트 수가 올바르지 않습니다.";
            size_t count = 0;
            QStringList names;
            for (auto sv : sheets) {
                auto o = sv.toObject();
                auto name = o.value("name").toString();
                if (!validateName(name).isEmpty() || names.contains(name, Qt::CaseInsensitive))
                    return "중복되거나 잘못된 시트 이름입니다.";
                names << name;
                Sheet s;
                s.name = utf(name);
                s.rows = std::clamp(o.value("rows").toInt(1000), 1, MaxRows);
                s.cols = std::clamp(o.value("cols").toInt(100), 1, MaxCols);
                for (auto cv : o.value("cells").toArray()) {
                    if (++count > 1000000)
                        return "셀 수 제한을 초과합니다.";
                    auto c = cv.toObject();
                    int r = c.value("r").toInt(-1), col = c.value("c").toInt(-1);
                    if (r < 0 || r >= MaxRows || col < 0 || col >= MaxCols)
                        return "셀 주소가 올바르지 않습니다.";
                    Cell cell;
                    cell.raw = utf(c.value("raw").toString());
                    cell.style = styleFromJson(c.value("style").toObject());
                    s.cells[{r, col}] = cell;
                    s.rows = std::max(s.rows, r + 1);
                    s.cols = std::max(s.cols, col + 1);
                }
                for (auto v : o.value("widths").toArray()) {
                    auto a = v.toArray();
                    if (a.size() == 2 && a[0].toInt(-1) >= 0 && a[0].toInt() < s.cols)
                        s.widths[a[0].toInt()] = std::clamp(a[1].toInt(), 20, 2000);
                }
                for (auto v : o.value("heights").toArray()) {
                    auto a = v.toArray();
                    if (a.size() == 2 && a[0].toInt(-1) >= 0 && a[0].toInt() < s.rows)
                        s.heights[a[0].toInt()] = std::clamp(a[1].toInt(), 15, 1000);
                }
                for (auto v : o.value("merges").toArray()) {
                    auto a = v.toArray();
                    if (a.size() != 4)
                        continue;
                    Position p{a[0].toInt(-1), a[1].toInt(-1)}, last{a[2].toInt(-1), a[3].toInt(-1)};
                    if (p.row >= 0 && p.col >= 0 && last.row >= p.row && last.col >= p.col &&
                        last.row < s.rows && last.col < s.cols)
                        s.merges.push_back({p, last});
                }
                for (auto v : o.value("charts").toArray()) {
                    auto c = v.toObject();
                    Chart chart;
                    chart.title = utf(c.value("title").toString());
                    chart.kind = std::clamp(c.value("kind").toInt(), 0, 1);
                    chart.first = {c.value("r0").toInt(-1), c.value("c0").toInt(-1)};
                    chart.last = {c.value("r1").toInt(-1), c.value("c1").toInt(-1)};
                    if (chart.first.row >= 0 && chart.first.col >= 0 && chart.last.row >= chart.first.row &&
                        chart.last.col >= chart.first.col && chart.last.row < s.rows &&
                        chart.last.col < s.cols)
                        s.charts.push_back(chart);
                }
                out.sheets.push_back(std::move(s));
            }
        } else
            return "지원하는 파일: .cellbook, .xlsx, .csv, .tsv";
    }
    if (out.sheets.empty())
        return "읽을 수 있는 워크시트가 없습니다.";
    book = std::move(out);
    return {};
}
QString saveWorkbook(const QString &path, const Workbook &book, int activeSheet) {
    auto ext = QFileInfo(path).suffix().toLower();
    if (ext != "xlsx" && ext != "cellbook" && ext != "csv" && ext != "tsv")
        return "지원하지 않는 파일 확장자입니다.";
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return file.errorString();
    if (ext == "xlsx") {
        QXlsx::Document doc;
        auto original = doc.sheetNames();
        doc.renameSheet(original.first(), q(book.sheets[0].name));
        Calculator calc(book);
        for (size_t i = 0; i < book.sheets.size(); ++i) {
            auto &s = book.sheets[i];
            if (i && !doc.addSheet(q(s.name)))
                return "시트를 생성할 수 없습니다.";
            doc.selectSheet(q(s.name));
            auto *w = doc.currentWorksheet();
            for (auto &[p, c] : s.cells) {
                auto f = xformat(c.style);
                bool ok = true;
                auto v = calc.get(int(i), p);
                auto raw = q(c.raw);
                if (raw.startsWith('=')) {
                    double cache = v.kind == Value::Number || v.kind == Value::Boolean ? v.number : 0;
                    ok = w->writeFormula(p.row + 1, p.col + 1, QXlsx::CellFormula(raw.mid(1)), f, cache);
                } else if (v.kind == Value::Number)
                    ok = w->writeNumeric(p.row + 1, p.col + 1, v.number, f);
                else if (v.kind == Value::Boolean)
                    ok = w->writeBool(p.row + 1, p.col + 1, v.number != 0, f);
                else if (v.kind == Value::Blank)
                    ok = w->writeBlank(p.row + 1, p.col + 1, f);
                else
                    ok = w->writeString(p.row + 1, p.col + 1, q(v.display()), f);
                if (!ok)
                    return "셀을 저장할 수 없습니다: " + q(address(p));
            }
            for (auto [col, width] : s.widths)
                doc.setColumnWidth(col + 1, std::max(1.0, (width - 5) / 7.0));
            for (auto [row, height] : s.heights)
                doc.setRowHeight(row + 1, height * 72.0 / 96);
            for (auto &m : s.merges)
                doc.mergeCells(
                    QXlsx::CellRange(m.first.row + 1, m.first.col + 1, m.second.row + 1, m.second.col + 1),
                    xformat(s.cells.count(m.first) ? s.cells.at(m.first).style : Style{}));
            int offset = 0;
            for (auto &chart : s.charts) {
                auto *x = doc.insertChart(2 + offset, chart.last.col + 3, QSize(600, 350));
                if (x) {
                    x->setChartType(chart.kind ? QXlsx::Chart::CT_LineChart : QXlsx::Chart::CT_BarChart);
                    x->setChartTitle(q(chart.title));
                    x->addSeries(QXlsx::CellRange(chart.first.row + 1, chart.first.col + 1,
                                                  chart.last.row + 1, chart.last.col + 1),
                                 nullptr, true, true);
                    offset += 20;
                }
            }
        }
        doc.selectSheet(std::clamp(activeSheet, 0, int(book.sheets.size()) - 1));
        if (!doc.saveAs(&file))
            return "Excel 파일 저장에 실패했습니다.";
    } else if (ext == "cellbook") {
        QJsonArray sheets;
        for (auto &s : book.sheets) {
            QJsonArray cells, widths, heights, merges, charts;
            for (auto &[p, c] : s.cells)
                cells.append(QJsonObject{
                    {"r", p.row}, {"c", p.col}, {"raw", q(c.raw)}, {"style", styleJson(c.style)}});
            for (auto [c, w] : s.widths)
                widths.append(QJsonArray{c, w});
            for (auto [r, h] : s.heights)
                heights.append(QJsonArray{r, h});
            for (auto &m : s.merges)
                merges.append(QJsonArray{m.first.row, m.first.col, m.second.row, m.second.col});
            for (auto &c : s.charts)
                charts.append(QJsonObject{{"title", q(c.title)},
                                          {"kind", c.kind},
                                          {"r0", c.first.row},
                                          {"c0", c.first.col},
                                          {"r1", c.last.row},
                                          {"c1", c.last.col}});
            sheets.append(QJsonObject{{"name", q(s.name)},
                                      {"rows", s.rows},
                                      {"cols", s.cols},
                                      {"cells", cells},
                                      {"widths", widths},
                                      {"heights", heights},
                                      {"merges", merges},
                                      {"charts", charts}});
        }
        auto bytes = QJsonDocument(QJsonObject{{"format", "CellBook"}, {"version", 1}, {"sheets", sheets}})
                         .toJson(QJsonDocument::Compact);
        if (file.write(bytes) != bytes.size())
            return file.errorString();
    } else {
        auto &s = book.sheets[std::clamp(activeSheet, 0, int(book.sheets.size()) - 1)];
        int mr = 0, mc = 0;
        for (auto &[p, c] : s.cells)
            if (!c.raw.empty()) {
                mr = std::max(mr, p.row + 1);
                mc = std::max(mc, p.col + 1);
            }
        if ((long long)mr * mc > 1000000)
            return "CSV로 내보낼 범위가 너무 큽니다.";
        Calculator calc(book);
        std::vector<std::vector<std::string>> rows;
        for (int r = 0; r < mr; ++r) {
            std::vector<std::string> row;
            for (int c = 0; c < mc; ++c)
                row.push_back(calc.get(activeSheet, {r, c}).display());
            rows.push_back(std::move(row));
        }
        auto bytes = QByteArray("\xEF\xBB\xBF") +
                     QByteArray::fromStdString(writeDelimited(rows, ext == "tsv" ? '\t' : ','));
        if (file.write(bytes) != bytes.size())
            return file.errorString();
    }
    if (!file.commit())
        return file.errorString();
    return {};
}
} // namespace grid
