#include "ui.h"
#include "io.h"
#include <QDataStream>
#include <QDesktopServices>
#include <QMimeData>
#include <QPainter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <QtPrintSupport/QPrintDialog>
#include <QtPrintSupport/QPrintPreviewDialog>
#include <QtPrintSupport/QPrinter>
#include <QtWidgets>
#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace grid;
namespace {
QString q(const std::string &s) {
    return QString::fromUtf8(s.data(), int(s.size()));
}
std::string utf(const QString &s) {
    return s.toUtf8().toStdString();
}
QFont fontFor(const Style &s, int zoom = 100) {
    QFont f(q(s.font));
    f.setPointSizeF(s.size * zoom / 100.0);
    f.setBold(s.bold);
    f.setItalic(s.italic);
    f.setUnderline(s.underline);
    return f;
}
QString formatValue(const Value &v, const Style &s) {
    if (v.kind != Value::Number)
        return q(v.display());
    QString fmt = q(s.numberFormat);
    if (fmt.isEmpty() || fmt == "General")
        return q(v.display());
    if (fmt.contains('y', Qt::CaseInsensitive) || fmt.contains("dd", Qt::CaseInsensitive)) {
        auto date = QDate(1899, 12, 30).addDays(qint64(v.number));
        return date.toString(fmt.contains('/') ? "yyyy/MM/dd" : "yyyy-MM-dd");
    }
    int digits = 0;
    auto dot = fmt.indexOf('.');
    if (dot >= 0)
        for (int i = dot + 1; i < fmt.size() && (fmt[i] == '0' || fmt[i] == '#'); ++i)
            ++digits;
    digits = std::min(digits, 12);
    double n = v.number;
    bool percent = fmt.contains('%');
    if (percent)
        n *= 100;
    QString out = fmt.contains(',')
                      ? QLocale(QLocale::English, QLocale::UnitedStates).toString(n, 'f', digits)
                      : QString::number(n, 'f', digits);
    if (percent)
        out += '%';
    if (fmt.contains('$'))
        out = '$' + out;
    if (fmt.contains(QString::fromUtf8("₩")))
        out = QString::fromUtf8("₩") + out;
    return out;
}
QRect usedRange(const Sheet &s) {
    int r = 0, c = 0;
    for (auto &[p, cell] : s.cells)
        if (!cell.raw.empty()) {
            r = std::max(r, p.row);
            c = std::max(c, p.col);
        }
    return QRect(0, 0, c + 1, r + 1);
}
size_t memory(const Workbook &b) {
    size_t n = 0;
    for (auto &s : b.sheets)
        for (auto &[p, c] : s.cells)
            n += sizeof(Cell) + c.raw.size() + c.style.font.size() + c.style.numberFormat.size() + 100;
    return n;
}
void encode(QDataStream &out, const Cell &c) {
    auto &s = c.style;
    out << q(c.raw) << q(s.font) << q(s.foreground) << q(s.background) << q(s.numberFormat) << qint32(s.size)
        << qint32(s.alignment) << s.bold << s.italic << s.underline << s.border << s.wrap;
}
Cell decode(QDataStream &in) {
    Cell c;
    auto &s = c.style;
    QString raw, font, fg, bg, fmt;
    qint32 size, align;
    in >> raw >> font >> fg >> bg >> fmt >> size >> align >> s.bold >> s.italic >> s.underline >> s.border >>
        s.wrap;
    c.raw = utf(raw);
    s.font = utf(font);
    s.foreground = utf(fg);
    s.background = utf(bg);
    s.numberFormat = utf(fmt);
    s.size = std::clamp(int(size), 6, 96);
    s.alignment = std::clamp(int(align), 0, 3);
    return c;
}
class CellDelegate : public QStyledItemDelegate {
  public:
    SheetModel *model;
    int zoom = 100;
    CellDelegate(SheetModel *m, QObject *p) : QStyledItemDelegate(p), model(m) {}
    void paint(QPainter *p, const QStyleOptionViewItem &o, const QModelIndex &i) const override {
        QStyleOptionViewItem option(o);
        initStyleOption(&option, i);
        option.font = fontFor(style(i), zoom);
        QStyledItemDelegate::paint(p, option, i);
        if (style(i).border) {
            p->save();
            p->setPen(QColor("#8397a8"));
            p->drawRect(o.rect.adjusted(0, 0, -1, -1));
            p->restore();
        }
    }
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &i) const override {
        auto *editor = new QLineEdit(parent);
        editor->setFont(fontFor(style(i), zoom));
        editor->setFrame(false);
        return editor;
    }
    Style style(const QModelIndex &i) const {
        auto &s = model->book.sheets[model->sheet];
        auto it = s.cells.find({i.row(), i.column()});
        return it == s.cells.end() ? Style{} : it->second.style;
    }
};
class FirstRowModel : public SheetModel {
  public:
    using SheetModel::SheetModel;
    int rowCount(const QModelIndex & = {}) const override { return 1; }
};
class ChartWidget : public QWidget {
  public:
    Workbook &book;
    int sheet;
    Chart chart;
    ChartWidget(Workbook &b, int sh, Chart c, QWidget *p) : QWidget(p), book(b), sheet(sh), chart(c) {
        setMinimumSize(700, 420);
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), Qt::white);
        p.setPen(QColor("#173b34"));
        QFont title = font();
        title.setPointSize(16);
        title.setBold(true);
        p.setFont(title);
        p.drawText(QRect(20, 15, width() - 40, 35), Qt::AlignCenter, q(chart.title));
        p.setFont(font());
        if (sheet >= int(book.sheets.size()))
            return;
        Calculator calc(book);
        QVector<QVector<double>> values;
        QStringList names, labels;
        int r0 = chart.first.row + 1, r1 = std::min(chart.last.row, r0 + 99), c0 = chart.first.col + 1,
            c1 = std::min(chart.last.col, c0 + 11);
        if (chart.first.col == chart.last.col)
            c0 = chart.first.col;
        for (int c = c0; c <= c1; ++c) {
            QVector<double> series;
            bool any = false;
            for (int r = r0; r <= r1; ++r) {
                auto v = calc.get(sheet, {r, c});
                series.append(v.kind == Value::Number ? v.number : 0);
                any |= v.kind == Value::Number;
            }
            if (any) {
                values.append(series);
                auto name = q(calc.get(sheet, {chart.first.row, c}).display());
                names << (name.isEmpty() ? q(columnName(c)) : name);
            }
        }
        if (values.isEmpty() || r1 < r0) {
            p.drawText(rect(), Qt::AlignCenter, "첫 행은 제목, 그 아래 행은 숫자 데이터로 선택하세요.");
            return;
        }
        for (int r = r0; r <= r1; ++r)
            labels << q(calc.get(sheet, {r, chart.first.col}).display());
        double lo = 0, hi = 0;
        for (auto &s : values)
            for (double v : s) {
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
        if (hi == lo)
            hi = lo + 1;
        double pad = (hi - lo) * .1;
        hi += pad;
        if (lo < 0)
            lo -= pad;
        QRectF plot(75, 75, width() - 105, height() - 150);
        auto y = [&](double v) { return plot.bottom() - (v - lo) / (hi - lo) * plot.height(); };
        for (int i = 0; i <= 5; ++i) {
            double v = lo + (hi - lo) * i / 5;
            p.setPen(QColor("#e4ebe8"));
            p.drawLine(QPointF(plot.left(), y(v)), QPointF(plot.right(), y(v)));
            p.setPen(QColor("#5e726b"));
            p.drawText(QRectF(0, y(v) - 10, 65, 20), Qt::AlignRight | Qt::AlignVCenter,
                       QString::number(v, 'g', 5));
        }
        QVector<QColor> colors{QColor("#168a66"), QColor("#4384cf"), QColor("#ecaa46"),
                               QColor("#ad6cc5"), QColor("#d96666"), QColor("#3ba4ac")};
        int count = values[0].size();
        double step = plot.width() / count;
        for (int j = 0; j < values.size(); ++j) {
            auto color = colors[j % colors.size()];
            p.setPen(QPen(color, 2));
            p.setBrush(color);
            QPolygonF line;
            for (int i = 0; i < count; ++i) {
                double x = plot.left() + (i + .5) * step;
                if (chart.kind)
                    line << QPointF(x, y(values[j][i]));
                else {
                    double w = step * .75 / values.size(),
                           left = plot.left() + i * step + step * .125 + j * w;
                    p.drawRect(QRectF(left, std::min(y(0), y(values[j][i])), std::max(1.0, w - 2),
                                      std::abs(y(values[j][i]) - y(0))));
                }
            }
            if (chart.kind) {
                p.setBrush(Qt::NoBrush);
                p.drawPolyline(line);
                for (auto &point : line)
                    p.drawEllipse(point, 3, 3);
            }
        }
        int stride = std::max(1, (count + 9) / 10);
        p.setPen(QColor("#52645f"));
        for (int i = 0; i < count; i += stride)
            p.drawText(QRectF(plot.left() + i * step - step, plot.bottom() + 8, step * 3, 25),
                       Qt::AlignCenter, labels[i].left(14));
        int x = 75;
        for (int j = 0; j < names.size(); ++j) {
            p.fillRect(QRect(x, height() - 35, 12, 12), colors[j % colors.size()]);
            p.setPen(QColor("#344940"));
            p.drawText(x + 18, height() - 24, names[j].left(14));
            x += 140;
        }
    }
};
} // namespace
SheetModel::SheetModel(Workbook &b, QObject *p) : QAbstractTableModel(p), book(b), calc(b) {}
int SheetModel::rowCount(const QModelIndex &) const {
    return book.sheets[sheet].rows;
}
int SheetModel::columnCount(const QModelIndex &) const {
    return book.sheets[sheet].cols;
}
QVariant SheetModel::headerData(int n, Qt::Orientation o, int role) const {
    if (role != Qt::DisplayRole)
        return {};
    return o == Qt::Horizontal ? q(columnName(n)) : QString::number(n + 1);
}
Qt::ItemFlags SheetModel::flags(const QModelIndex &i) const {
    return i.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable : Qt::NoItemFlags;
}
QVariant SheetModel::data(const QModelIndex &i, int role) const {
    if (!i.isValid())
        return {};
    auto &s = book.sheets[sheet];
    auto it = s.cells.find({i.row(), i.column()});
    Cell blank;
    auto &cell = it == s.cells.end() ? blank : it->second;
    if (role == Qt::EditRole)
        return q(cell.raw);
    if (role == Qt::DisplayRole) {
        if (showFormulas && !cell.raw.empty() && cell.raw[0] == '=')
            return q(cell.raw);
        return formatValue(calc.get(sheet, {i.row(), i.column()}), cell.style);
    }
    if (role == Qt::FontRole)
        return fontFor(cell.style, zoom);
    if (role == Qt::BackgroundRole && !cell.style.background.empty())
        return QColor(q(cell.style.background));
    if (role == Qt::ForegroundRole) {
        auto v = calc.get(sheet, {i.row(), i.column()});
        return v.kind == Value::Error ? QColor("#be3d45") : QColor(q(cell.style.foreground));
    }
    if (role == Qt::TextAlignmentRole) {
        int a = cell.style.alignment;
        auto v = calc.get(sheet, {i.row(), i.column()});
        return int(Qt::AlignVCenter | (a == 1                              ? Qt::AlignLeft
                                       : a == 2                            ? Qt::AlignHCenter
                                       : a == 3 || v.kind == Value::Number ? Qt::AlignRight
                                                                           : Qt::AlignLeft));
    }
    if (role == Qt::ToolTipRole && !cell.raw.empty())
        return q(address({i.row(), i.column()})) + "\n" + q(cell.raw);
    return {};
}
bool SheetModel::setData(const QModelIndex &i, const QVariant &v, int role) {
    if (role != Qt::EditRole || !i.isValid() || !edit)
        return false;
    edit({i.row(), i.column()}, v.toString());
    return true;
}
void SheetModel::refresh(int sh) {
    beginResetModel();
    sheet = sh;
    calc.clear();
    endResetModel();
}
QAction *CellBookWindow::action(QMenu *m, const QString &text, const QKeySequence &keys,
                                std::function<void()> fn) {
    auto *a = m->addAction(text);
    if (!keys.isEmpty())
        a->setShortcut(keys);
    connect(a, &QAction::triggered, this, [fn] { fn(); });
    return a;
}
CellBookWindow::CellBookWindow() {
    resize(1280, 800);
    setMinimumSize(800, 500);
    setFont(QFont("Malgun Gothic", 10));
    model = new SheetModel(book, this);
    table = new QTableView(this);
    table->setObjectName("grid");
    table->setModel(model);
    table->setItemDelegate(new CellDelegate(model, table));
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setSelectionBehavior(QAbstractItemView::SelectItems);
    table->setAlternatingRowColors(false);
    table->setWordWrap(true);
    table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed |
                           QAbstractItemView::AnyKeyPressed);
    table->horizontalHeader()->setDefaultSectionSize(110);
    table->verticalHeader()->setDefaultSectionSize(28);
    table->verticalHeader()->setMinimumSectionSize(18);
    table->horizontalHeader()->setMinimumSectionSize(25);
    table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->installEventFilter(this);
    table->viewport()->installEventFilter(this);
    auto *fmodel = new FirstRowModel(book, this);
    frozen = new QTableView(table);
    frozen->setModel(fmodel);
    frozen->setItemDelegate(new CellDelegate(fmodel, frozen));
    frozen->horizontalHeader()->hide();
    frozen->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    frozen->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    frozen->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    frozen->setSelectionMode(QAbstractItemView::SingleSelection);
    frozen->hide();
    model->edit = [this](Position p, QString raw) {
        mutate([&] {
            auto &cell = book.sheets[active].cells[p];
            cell.raw = utf(raw);
            if (raw.endsWith('%') && cell.style.numberFormat.empty())
                cell.style.numberFormat = "0.00%";
        });
        table->setCurrentIndex(model->index(p.row, p.col));
    };
    fmodel->edit = model->edit;
    connect(frozen, &QTableView::clicked, this,
            [this](const QModelIndex &i) { table->setCurrentIndex(model->index(0, i.column())); });
    connect(table->horizontalScrollBar(), &QScrollBar::valueChanged, frozen->horizontalScrollBar(),
            &QScrollBar::setValue);
    auto *file = menuBar()->addMenu("파일(&F)");
    action(file, "새 통합문서", QKeySequence::New, [this] { newBook(); });
    action(file, "열기…", QKeySequence::Open, [this] {
        auto p = QFileDialog::getOpenFileName(this, "통합문서 열기", {},
                                              "통합문서 (*.cellbook *.xlsx *.csv *.tsv)");
        if (!p.isEmpty())
            loadFile(p);
    });
    action(file, "저장", QKeySequence::Save, [this] { save(); });
    action(file, "다른 이름으로 저장…", QKeySequence::SaveAs, [this] { save(true); });
    file->addSeparator();
    action(file, "인쇄…", QKeySequence::Print, [this] { print(); });
    action(file, "PDF로 내보내기…", {}, [this] { print(true); });
    file->addSeparator();
    action(file, "종료", QKeySequence::Quit, [this] { close(); });
    auto *editMenu = menuBar()->addMenu("편집(&E)");
    undoAction = action(editMenu, "실행 취소", QKeySequence::Undo, [this] { history(false); });
    redoAction = action(editMenu, "다시 실행", QKeySequence::Redo, [this] { history(true); });
    editMenu->addSeparator();
    action(editMenu, "잘라내기", QKeySequence::Cut, [this] { copy(true); });
    action(editMenu, "복사", QKeySequence::Copy, [this] { copy(); });
    action(editMenu, "붙여넣기", QKeySequence::Paste, [this] { paste(); });
    action(editMenu, "내용 지우기", QKeySequence(Qt::Key_Delete), [this] { clear(); });
    action(editMenu, "모두 선택", QKeySequence::SelectAll, [this] { table->selectAll(); });
    editMenu->addSeparator();
    action(editMenu, "찾기…", QKeySequence::Find, [this] { find(false); });
    action(editMenu, "바꾸기…", QKeySequence::Replace, [this] { find(true); });
    action(editMenu, "아래로 채우기", QKeySequence("Ctrl+D"), [this] { fill(); });
    action(editMenu, "오른쪽으로 채우기", QKeySequence("Ctrl+R"), [this] { fill(true); });
    auto *insert = menuBar()->addMenu("삽입(&I)");
    action(insert, "행 삽입", {}, [this] { structure(true, false); });
    action(insert, "열 삽입", {}, [this] { structure(false, false); });
    action(insert, "행 삭제", {}, [this] { structure(true, true); });
    action(insert, "열 삭제", {}, [this] { structure(false, true); });
    insert->addSeparator();
    action(insert, "시트 추가", QKeySequence("Shift+F11"), [this] { addSheet(); });
    action(insert, "시트 이름 변경…", {}, [this] { renameSheet(); });
    action(insert, "시트 삭제", {}, [this] { deleteSheet(); });
    action(insert, "차트…", {}, [this] { chart(); });
    auto *format = menuBar()->addMenu("서식(&O)");
    action(format, "굵게", QKeySequence::Bold, [this] {
        auto i = table->currentIndex();
        bool b = !model->data(i, Qt::FontRole).value<QFont>().bold();
        style([b](Style &s) { s.bold = b; });
    });
    action(format, "기울임", QKeySequence::Italic, [this] {
        auto i = table->currentIndex();
        bool b = !model->data(i, Qt::FontRole).value<QFont>().italic();
        style([b](Style &s) { s.italic = b; });
    });
    action(format, "밑줄", QKeySequence::Underline, [this] {
        auto i = table->currentIndex();
        bool b = !model->data(i, Qt::FontRole).value<QFont>().underline();
        style([b](Style &s) { s.underline = b; });
    });
    action(format, "글자 색…", {}, [this] {
        auto c = QColorDialog::getColor(QColor("#202c38"), this);
        if (c.isValid())
            style([c](Style &s) { s.foreground = utf(c.name()); });
    });
    action(format, "채우기 색…", {}, [this] {
        auto c = QColorDialog::getColor(QColor("#e3f3eb"), this);
        if (c.isValid())
            style([c](Style &s) { s.background = utf(c.name()); });
    });
    action(format, "모든 테두리", {}, [this] { style([](Style &s) { s.border = true; }); });
    action(format, "테두리 제거", {}, [this] { style([](Style &s) { s.border = false; }); });
    action(format, "자동 줄바꿈", {}, [this] {
        style([](Style &s) { s.wrap = !s.wrap; });
        table->resizeRowsToContents();
    });
    action(format, "셀 병합", {}, [this] { merge(false); });
    action(format, "병합 해제", {}, [this] { merge(true); });
    action(format, "열 너비 자동 맞춤", {}, [this] {
        Position a, b;
        if (selectionBounds(a, b))
            for (int c = a.col; c <= b.col; ++c)
                table->resizeColumnToContents(c);
    });
    action(format, "서식 지우기", {}, [this] { clear(true); });
    auto *dataMenu = menuBar()->addMenu("데이터(&D)");
    action(dataMenu, "오름차순 정렬", {}, [this] { sort(false); });
    action(dataMenu, "내림차순 정렬", {}, [this] { sort(true); });
    action(dataMenu, "선택 열 필터…", {}, [this] { filter(); });
    action(dataMenu, "필터 해제", {}, [this] {
        for (int r = 0; r < model->rowCount(); ++r)
            table->setRowHidden(r, false);
    });
    action(dataMenu, "자동 합계", QKeySequence("Alt+="), [this] { autosum(); });
    auto *view = menuBar()->addMenu("보기(&V)");
    auto *freeze = action(view, "첫 행 고정", {}, [this] {
        freezeRow = !freezeRow;
        setupFrozen();
    });
    freeze->setCheckable(true);
    auto *formulas = action(view, "수식 표시", QKeySequence("Ctrl+`"), [this] {
        model->showFormulas = !model->showFormulas;
        rebuild();
    });
    formulas->setCheckable(true);
    action(view, "100% 배율", {}, [this] { zoomTo(100); });
    action(view, "저장된 차트 보기…", {}, [this] {
        auto &s = book.sheets[active];
        if (s.charts.empty()) {
            QMessageBox::information(this, "차트", "저장된 차트가 없습니다.");
            return;
        }
        QStringList titles;
        for (auto &c : s.charts)
            titles << q(c.title);
        bool ok;
        auto t = QInputDialog::getItem(this, "차트", "차트 선택", titles, 0, false, &ok);
        if (ok) {
            auto *d = new QDialog(this);
            d->setAttribute(Qt::WA_DeleteOnClose);
            d->setWindowTitle(t);
            auto *l = new QVBoxLayout(d);
            l->addWidget(new ChartWidget(book, active, s.charts[titles.indexOf(t)], d));
            d->show();
        }
    });
    auto *help = menuBar()->addMenu("도움말(&H)");
    action(help, "지원 기능", QKeySequence::HelpContents, [this] {
        QMessageBox::information(
            this, "CellBook",
            "셀 편집 · 여러 시트 · 수식 · 복사/붙여넣기 · 채우기\n서식 · 정렬/필터 · 차트 · 인쇄/PDF · 실행 "
            "취소\n\n파일: .cellbook / .xlsx / .csv / .tsv\n\n지원 함수:\nSUM, AVERAGE, MIN, MAX, COUNT, "
            "COUNTA, PRODUCT, IF, IFERROR, AND, OR, NOT, ISBLANK, ISNUMBER, ISERROR, ABS, ROUND, ROUNDUP, "
            "ROUNDDOWN, INT, MOD, POWER, SQRT, COUNTIF, SUMIF, LEN, LEFT, RIGHT, MID, CONCAT, CONCATENATE, "
            "UPPER, LOWER, TRIM\n\n.xlsx는 기본 데이터·수식·서식·병합을 지원합니다.\n매크로·피벗·기존 "
            "차트·고급 기능은 보존하지 않습니다.\n원본은 다른 이름으로 저장하세요.");
    });
    auto *bar = addToolBar("빠른 실행");
    bar->setMovable(false);
    bar->setIconSize(QSize(20, 20));
    auto addTool = [&](QAction *a, QStyle::StandardPixmap icon) {
        a->setIcon(QMainWindow::style()->standardIcon(icon));
        bar->addAction(a);
    };
    addTool(file->actions()[0], QStyle::SP_FileIcon);
    addTool(file->actions()[1], QStyle::SP_DialogOpenButton);
    addTool(file->actions()[2], QStyle::SP_DialogSaveButton);
    bar->addSeparator();
    addTool(undoAction, QStyle::SP_ArrowBack);
    addTool(redoAction, QStyle::SP_ArrowForward);
    bar->addSeparator();
    auto *fonts = new QFontComboBox(bar);
    fonts->setMaximumWidth(160);
    fonts->setCurrentFont(QFont("Malgun Gothic"));
    bar->addWidget(fonts);
    connect(fonts, &QFontComboBox::currentFontChanged, this,
            [this](const QFont &f) { style([f](Style &s) { s.font = utf(f.family()); }); });
    auto *sizes = new QSpinBox(bar);
    sizes->setRange(6, 96);
    sizes->setValue(11);
    sizes->setSuffix(" pt");
    bar->addWidget(sizes);
    connect(sizes, &QSpinBox::valueChanged, this,
            [this](int size) { style([size](Style &s) { s.size = size; }); });
    bar->addSeparator();
    for (int i = 0; i < 3; ++i)
        bar->addAction(format->actions()[i]);
    bar->addSeparator();
    bar->addAction(format->actions()[3]);
    bar->addAction(format->actions()[4]);
    auto *bar2 = addToolBar("셀 서식");
    bar2->setMovable(false);
    numberFormats = new QComboBox(bar2);
    numberFormats->addItems({"일반", "숫자", "천 단위", "백분율", "통화 (₩)", "날짜"});
    bar2->addWidget(numberFormats);
    connect(numberFormats, &QComboBox::activated, this, [this](int index) {
        static const char *formats[] = {"", "0.00", "#,##0.00", "0.00%", "₩#,##0", "yyyy-mm-dd"};
        style([index](Style &s) { s.numberFormat = formats[index]; });
    });
    for (auto name : {"왼쪽", "가운데", "오른쪽"}) {
        int align = bar2->actions().size();
        auto *a = bar2->addAction(name);
        connect(a, &QAction::triggered, this,
                [this, align] { style([align](Style &s) { s.alignment = std::clamp(align, 1, 3); }); });
    }
    bar2->addSeparator();
    bar2->addAction(format->actions()[5]);
    bar2->addAction(format->actions()[7]);
    bar2->addAction(format->actions()[8]);
    bar2->addSeparator();
    bar2->addAction(dataMenu->actions()[4]);
    bar2->addAction(dataMenu->actions()[0]);
    bar2->addAction(dataMenu->actions()[1]);
    bar2->addAction(insert->actions().last());
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *formulaBar = new QWidget(central);
    formulaBar->setObjectName("formulaBar");
    auto *fl = new QHBoxLayout(formulaBar);
    fl->setContentsMargins(10, 7, 10, 7);
    nameBox = new QLineEdit("A1", formulaBar);
    nameBox->setFixedWidth(100);
    nameBox->setToolTip("셀 주소 또는 범위: A1, A1:C10");
    fl->addWidget(nameBox);
    auto *fx = new QLabel("ƒx", formulaBar);
    fx->setFixedWidth(30);
    fx->setAlignment(Qt::AlignCenter);
    fl->addWidget(fx);
    formula = new QLineEdit(formulaBar);
    formula->setPlaceholderText("값 또는 수식 입력 (예: =SUM(A1:A10))");
    fl->addWidget(formula);
    layout->addWidget(formulaBar);
    layout->addWidget(table, 1);
    auto *tabStrip = new QWidget(central);
    auto *tl = new QHBoxLayout(tabStrip);
    tl->setContentsMargins(8, 2, 8, 2);
    tabs = new QTabBar(tabStrip);
    tabs->setExpanding(false);
    tabs->setDrawBase(false);
    tabs->setContextMenuPolicy(Qt::CustomContextMenu);
    tl->addWidget(tabs);
    auto *plus = new QPushButton("+", tabStrip);
    plus->setFixedSize(32, 28);
    tl->addWidget(plus);
    tl->addStretch();
    layout->addWidget(tabStrip);
    setCentralWidget(central);
    summary = new QLabel("준비", this);
    statusBar()->addWidget(summary, 1);
    auto *slider = new QSlider(Qt::Horizontal, this);
    slider->setRange(50, 200);
    slider->setValue(100);
    slider->setFixedWidth(140);
    statusBar()->addPermanentWidget(slider);
    auto *zoomLabel = new QLabel("100%", this);
    zoomLabel->setFixedWidth(45);
    statusBar()->addPermanentWidget(zoomLabel);
    connect(slider, &QSlider::valueChanged, this, [this, zoomLabel](int value) {
        zoomLabel->setText(QString::number(value) + "%");
        zoomTo(value);
    });
    connect(plus, &QPushButton::clicked, this, [this] { addSheet(); });
    connect(tabs, &QTabBar::currentChanged, this, [this](int i) {
        if (!rebuilding && i >= 0 && i < int(book.sheets.size())) {
            active = i;
            rebuild();
        }
    });
    connect(tabs, &QTabBar::tabBarDoubleClicked, this, [this](int i) {
        if (i >= 0) {
            active = i;
            renameSheet();
        }
    });
    connect(tabs, &QTabBar::customContextMenuRequested, this, [this](QPoint p) {
        int i = tabs->tabAt(p);
        if (i < 0)
            return;
        active = i;
        rebuild();
        QMenu m;
        auto *r = m.addAction("이름 변경");
        auto *d = m.addAction("시트 삭제");
        auto *a = m.exec(tabs->mapToGlobal(p));
        if (a == r)
            renameSheet();
        if (a == d)
            deleteSheet();
    });
    connect(table->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this] { updateSelection(); });
    connect(table->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this] { updateSelection(); });
    connect(formula, &QLineEdit::returnPressed, this, [this] {
        auto i = table->currentIndex();
        if (i.isValid())
            model->setData(i, formula->text());
        table->setFocus();
    });
    connect(nameBox, &QLineEdit::returnPressed, this, [this] {
        auto parts = nameBox->text().split(':');
        Position a, b;
        if (parts.size() > 2 || !parseAddress(utf(parts[0].trimmed()), a)) {
            QMessageBox::information(this, "주소", "올바른 셀 주소를 입력하세요.");
            return;
        }
        b = a;
        if (parts.size() == 2 && !parseAddress(utf(parts[1].trimmed()), b))
            return;
        auto &s = book.sheets[active];
        s.rows = std::max(s.rows, std::max(a.row, b.row) + 1);
        s.cols = std::max(s.cols, std::max(a.col, b.col) + 1);
        rebuild();
        QItemSelection selection(model->index(std::min(a.row, b.row), std::min(a.col, b.col)),
                                 model->index(std::max(a.row, b.row), std::max(a.col, b.col)));
        table->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
        table->selectionModel()->setCurrentIndex(model->index(a.row, a.col), QItemSelectionModel::NoUpdate);
        table->scrollTo(model->index(a.row, a.col));
        table->setFocus();
    });
    connect(table->horizontalHeader(), &QHeaderView::sectionResized, this, [this](int n, int, int value) {
        if (!rebuilding) {
            book.sheets[active].widths[n] = value * 100 / zoom;
            dirty = true;
            updateTitle();
        }
        frozen->setColumnWidth(n, value);
        setupFrozen();
    });
    connect(table->verticalHeader(), &QHeaderView::sectionResized, this, [this](int n, int, int value) {
        if (!rebuilding) {
            book.sheets[active].heights[n] = value * 100 / zoom;
            dirty = true;
            updateTitle();
        }
        setupFrozen();
    });
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table, &QWidget::customContextMenuRequested, this, [this, editMenu, insert](QPoint p) {
        QMenu m;
        for (int i = 3; i <= 6; ++i)
            m.addAction(editMenu->actions()[i]);
        m.addSeparator();
        for (int i = 0; i < 4; ++i)
            m.addAction(insert->actions()[i]);
        m.exec(table->mapToGlobal(p));
    });
    setStyleSheet(
        "QMainWindow{background:#f6f8f7;}QMenuBar{background:#17684e;color:white;padding:5px;}QMenuBar::item:"
        "selected{background:#268367;}QToolBar{background:#f4f7f5;border:0;border-bottom:1px solid "
        "#d8e2dc;spacing:6px;padding:6px;}QToolButton{padding:5px;border-radius:4px;}QToolButton:hover{"
        "background:#deece4;}QHeaderView::section{background:#eef3ef;color:#496258;border:0;border-right:1px "
        "solid #d3ded6;border-bottom:1px solid "
        "#d3ded6;padding:4px;}QTableView{background:white;gridline-color:#e0e6e2;selection-background-color:#"
        "d1eadc;selection-color:#183e2c;border:0;}QLineEdit{background:white;border:1px solid "
        "#c9d8cf;border-radius:4px;padding:5px;selection-background-color:#b6d9c6;}QLineEdit:focus{border:"
        "1px solid #16875d;}QTabBar::tab{background:#edf2ee;padding:7px "
        "18px;margin:2px;color:#536a5c;border-radius:3px;}QTabBar::tab:selected{background:white;color:#"
        "146746;border-bottom:3px solid "
        "#16875d;}QStatusBar{background:#edf3ef;color:#496258;}QPushButton{padding:5px "
        "10px;}QComboBox,QSpinBox{padding:3px;}QMenu{background:white;color:#253b2e;}");
    rebuild();
}
void CellBookWindow::updateTitle() {
    setWindowTitle((dirty ? "* " : "") +
                   (filePath.isEmpty() ? "새 통합문서" : QFileInfo(filePath).fileName()) + " — CellBook");
    undoAction->setEnabled(!undoStack.empty());
    redoAction->setEnabled(!redoStack.empty());
}
void CellBookWindow::mutate(const std::function<void()> &fn) {
    Snapshot before{book, active, memory(book)};
    try {
        fn();
    } catch (const std::exception &e) {
        book = std::move(before.book);
        active = before.sheet;
        rebuild();
        QMessageBox::warning(this, "작업 실패", QString::fromUtf8(e.what()));
        return;
    }
    undoStack.push_back(std::move(before));
    redoStack.clear();
    size_t total = 0;
    for (auto &snapshot : undoStack)
        total += snapshot.bytes;
    // ponytail: snapshots keep undo simple; use per-cell deltas if history needs to exceed 64 MB.
    while (undoStack.size() > 1 && (undoStack.size() > 50 || total > 64 * 1024 * 1024)) {
        total -= undoStack.front().bytes;
        undoStack.pop_front();
    }
    dirty = true;
    rebuild();
}
void CellBookWindow::rebuild() {
    rebuilding = true;
    auto current = table->currentIndex();
    auto selection = table->selectionModel()->selection();
    active = std::clamp(active, 0, int(book.sheets.size()) - 1);
    auto &s = book.sheets[active];
    model->refresh(active);
    static_cast<SheetModel *>(frozen->model())->refresh(active);
    auto *fmodel = static_cast<SheetModel *>(frozen->model());
    fmodel->showFormulas = model->showFormulas;
    while (tabs->count())
        tabs->removeTab(0);
    for (auto &sh : book.sheets)
        tabs->addTab(q(sh.name));
    tabs->setCurrentIndex(active);
    table->clearSpans();
    table->horizontalHeader()->setDefaultSectionSize(110 * zoom / 100);
    table->verticalHeader()->setDefaultSectionSize(28 * zoom / 100);
    for (int c = 0; c < model->columnCount(); ++c) {
        auto it = s.widths.find(c);
        int w = (it == s.widths.end() ? 110 : it->second) * zoom / 100;
        table->setColumnWidth(c, w);
        frozen->setColumnWidth(c, w);
    }
    for (int r = 0; r < model->rowCount(); ++r) {
        auto it = s.heights.find(r);
        if (it != s.heights.end())
            table->setRowHeight(r, it->second * zoom / 100);
    }
    for (auto &m : s.merges)
        table->setSpan(m.first.row, m.first.col, m.second.row - m.first.row + 1,
                       m.second.col - m.first.col + 1);
    if (current.isValid() && current.row() < s.rows && current.column() < s.cols)
        table->setCurrentIndex(model->index(current.row(), current.column()));
    else
        table->setCurrentIndex(model->index(0, 0));
    for (auto range : selection)
        if (range.bottom() < s.rows && range.right() < s.cols)
            table->selectionModel()->select(QItemSelection(model->index(range.top(), range.left()),
                                                           model->index(range.bottom(), range.right())),
                                            QItemSelectionModel::Select);
    rebuilding = false;
    setupFrozen();
    updateSelection();
    updateTitle();
}
void CellBookWindow::updateSelection() {
    if (rebuilding)
        return;
    auto i = table->currentIndex();
    if (!i.isValid())
        return;
    nameBox->setText(q(address({i.row(), i.column()})));
    formula->setText(model->data(i, Qt::EditRole).toString());
    double sum = 0;
    int count = 0, numbers = 0;
    long long area = 0;
    for (auto range : table->selectionModel()->selection())
        area += (long long)range.width() * range.height();
    if (area > 100000) {
        summary->setText(QString("선택: %1개 셀").arg(area));
        return;
    }
    for (auto range : table->selectionModel()->selection())
        for (int r = range.top(); r <= range.bottom(); ++r)
            for (int c = range.left(); c <= range.right(); ++c) {
                auto v = model->calc.get(active, {r, c});
                if (v.kind != Value::Blank)
                    ++count;
                if (v.kind == Value::Number) {
                    sum += v.number;
                    ++numbers;
                }
            }
    summary->setText(numbers ? QString("개수 %1    합계 %2    평균 %3")
                                   .arg(count)
                                   .arg(sum, 0, 'g', 12)
                                   .arg(sum / numbers, 0, 'g', 12)
                             : QString("선택한 값 %1개    %2").arg(count).arg(q(book.sheets[active].name)));
}
bool CellBookWindow::selectionBounds(Position &a, Position &b, int max) {
    auto ranges = table->selectionModel()->selection();
    if (ranges.isEmpty()) {
        auto i = table->currentIndex();
        if (!i.isValid())
            return false;
        a = b = {i.row(), i.column()};
        return true;
    }
    if (ranges.size() != 1) {
        QMessageBox::information(this, "선택", "연속된 직사각형 범위를 선택하세요.");
        return false;
    }
    auto r = ranges.first();
    a = {r.top(), r.left()};
    b = {r.bottom(), r.right()};
    if ((long long)(b.row - a.row + 1) * (b.col - a.col + 1) > max) {
        QMessageBox::information(this, "선택 범위",
                                 QString("한 번에 최대 %1개 셀을 처리할 수 있습니다.").arg(max));
        return false;
    }
    return true;
}
void CellBookWindow::history(bool redo) {
    auto &from = redo ? redoStack : undoStack;
    auto &to = redo ? undoStack : redoStack;
    if (from.empty())
        return;
    to.push_back({book, active, memory(book)});
    auto snapshot = std::move(from.back());
    from.pop_back();
    book = std::move(snapshot.book);
    active = snapshot.sheet;
    dirty = true;
    rebuild();
}
bool CellBookWindow::confirmSave() {
    if (!dirty)
        return true;
    auto answer = QMessageBox::question(this, "변경 내용 저장", "변경 내용을 저장할까요?",
                                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                        QMessageBox::Save);
    return answer == QMessageBox::Discard || (answer == QMessageBox::Save && save() && !dirty);
}
void CellBookWindow::newBook() {
    if (!confirmSave())
        return;
    book = Workbook{};
    active = 0;
    filePath.clear();
    dirty = false;
    undoStack.clear();
    redoStack.clear();
    rebuild();
}
bool CellBookWindow::loadFile(const QString &path, bool prompt) {
    if (prompt && !confirmSave())
        return false;
    Workbook loaded;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    auto error = loadWorkbook(path, loaded);
    QApplication::restoreOverrideCursor();
    if (!error.isEmpty()) {
        QMessageBox::warning(this, "열기 실패", error);
        return false;
    }
    book = std::move(loaded);
    filePath = path;
    active = 0;
    dirty = false;
    undoStack.clear();
    redoStack.clear();
    rebuild();
    if (prompt && QFileInfo(path).suffix().compare("xlsx", Qt::CaseInsensitive) == 0)
        QMessageBox::information(
            this, "Excel 파일 호환",
            "기본 데이터·수식·서식·병합을 불러왔습니다.\n매크로·피벗·기존 차트 등 고급 기능은 보존하지 "
            "않습니다.\n원본 보호를 위해 저장 시 새 파일 이름을 선택하세요.");
    return true;
}
bool CellBookWindow::save(bool saveAs) {
    auto ext = QFileInfo(filePath).suffix().toLower();
    bool imported = ext == "xlsx" || ext == "csv" || ext == "tsv";
    QString path = filePath;
    if (saveAs || path.isEmpty() || imported) {
        QString selected;
        path = QFileDialog::getSaveFileName(this, "통합문서 저장",
                                            path.isEmpty()
                                                ? "통합문서.cellbook"
                                                : QFileInfo(path).absolutePath() + "/" +
                                                      QFileInfo(path).completeBaseName() + "_편집.cellbook",
                                            "CellBook 통합문서 (*.cellbook);;Excel 통합문서 (*.xlsx);;CSV "
                                            "(쉼표로 구분) (*.csv);;TSV (탭으로 구분) (*.tsv)",
                                            &selected);
        if (path.isEmpty())
            return false;
        if (QFileInfo(path).suffix().isEmpty())
            path += selected.contains("*.xlsx")  ? ".xlsx"
                    : selected.contains("*.csv") ? ".csv"
                    : selected.contains("*.tsv") ? ".tsv"
                                                 : ".cellbook";
    }
    ext = QFileInfo(path).suffix().toLower();
    if (ext == "csv" || ext == "tsv") {
        if (QMessageBox::question(this, "현재 시트 내보내기",
                                  "CSV/TSV는 현재 시트의 계산 결과만 저장합니다.\n수식·서식·다른 시트는 "
                                  "저장하지 않습니다. 계속할까요?") != QMessageBox::Yes)
            return false;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    auto error = saveWorkbook(path, book, active);
    QApplication::restoreOverrideCursor();
    if (!error.isEmpty()) {
        QMessageBox::warning(this, "저장 실패", error);
        return false;
    }
    if (ext == "csv" || ext == "tsv") {
        statusBar()->showMessage("현재 시트 내보내기 완료", 4000);
        return true;
    }
    filePath = path;
    dirty = false;
    updateTitle();
    statusBar()->showMessage("저장 완료", 3000);
    return true;
}
void CellBookWindow::closeEvent(QCloseEvent *e) {
    if (confirmSave())
        e->accept();
    else
        e->ignore();
}
bool CellBookWindow::eventFilter(QObject *o, QEvent *e) {
    if (o == table && e->type() == QEvent::Resize)
        QTimer::singleShot(0, this, [this] { setupFrozen(); });
    if (o == table && e->type() == QEvent::KeyPress) {
        auto *k = static_cast<QKeyEvent *>(e);
        if (k->key() == Qt::Key_Escape)
            table->setFocus();
    }
    return QMainWindow::eventFilter(o, e);
}
void CellBookWindow::copy(bool cut) {
    Position a, b;
    if (!selectionBounds(a, b))
        return;
    std::vector<std::vector<std::string>> rows;
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_4);
    stream << quint32(0x43424b31) << qint32(a.row) << qint32(a.col) << qint32(b.row - a.row + 1)
           << qint32(b.col - a.col + 1);
    auto &s = book.sheets[active];
    for (int r = a.row; r <= b.row; ++r) {
        std::vector<std::string> row;
        for (int c = a.col; c <= b.col; ++c) {
            auto it = s.cells.find({r, c});
            Cell cell = it == s.cells.end() ? Cell{} : it->second;
            encode(stream, cell);
            row.push_back(model->calc.get(active, {r, c}).display());
        }
        rows.push_back(std::move(row));
    }
    auto *mime = new QMimeData;
    mime->setText(q(writeDelimited(rows, '\t')));
    mime->setData("application/x-cellbook", bytes);
    QApplication::clipboard()->setMimeData(mime);
    if (cut)
        clear();
}
void CellBookWindow::paste() {
    auto *mime = QApplication::clipboard()->mimeData();
    auto i = table->currentIndex();
    if (!i.isValid())
        return;
    Position start{i.row(), i.column()};
    std::vector<std::vector<Cell>> cells;
    if (mime->hasFormat("application/x-cellbook")) {
        auto bytes = mime->data("application/x-cellbook");
        if (bytes.size() > 32 * 1024 * 1024) {
            QMessageBox::warning(this, "붙여넣기", "클립보드 데이터가 너무 큽니다.");
            return;
        }
        QDataStream in(bytes);
        in.setVersion(QDataStream::Qt_6_4);
        quint32 magic;
        qint32 sr, sc, nr, nc;
        in >> magic >> sr >> sc >> nr >> nc;
        if (magic != 0x43424b31 || sr < 0 || sc < 0 || nr < 1 || nc < 1 || (long long)nr * nc > 100000)
            return;
        for (int r = 0; r < nr; ++r) {
            std::vector<Cell> row;
            for (int c = 0; c < nc; ++c) {
                auto cell = decode(in);
                cell.raw = translateFormula(cell.raw, start.row - sr, start.col - sc);
                row.push_back(std::move(cell));
            }
            cells.push_back(std::move(row));
        }
        if (in.status() != QDataStream::Ok) {
            QMessageBox::warning(this, "붙여넣기", "클립보드 데이터가 손상되었습니다.");
            return;
        }
    } else {
        auto text = mime->text();
        if (text.size() > 32 * 1024 * 1024)
            return;
        std::vector<std::vector<std::string>> rows;
        try {
            rows = parseDelimited(utf(text), '\t');
        } catch (...) {
            QMessageBox::warning(this, "붙여넣기", "표 데이터의 따옴표가 올바르지 않습니다.");
            return;
        }
        size_t count = 0;
        for (auto &row : rows) {
            std::vector<Cell> line;
            for (auto &raw : row) {
                if (++count > 100000) {
                    QMessageBox::warning(this, "붙여넣기", "최대 100,000개 셀을 붙여넣을 수 있습니다.");
                    return;
                }
                Cell cell;
                cell.raw = raw;
                line.push_back(cell);
            }
            cells.push_back(std::move(line));
        }
    }
    if (cells.empty())
        return;
    size_t cols = 0;
    for (auto &row : cells)
        cols = std::max(cols, row.size());
    if (start.row + (long long)cells.size() > MaxRows || start.col + (long long)cols > MaxCols) {
        QMessageBox::warning(this, "붙여넣기", "시트 범위를 초과합니다.");
        return;
    }
    mutate([&] {
        auto &s = book.sheets[active];
        for (size_t r = 0; r < cells.size(); ++r)
            for (size_t c = 0; c < cells[r].size(); ++c)
                s.cells[{start.row + int(r), start.col + int(c)}] = cells[r][c];
        s.rows = std::max(s.rows, start.row + int(cells.size()));
        s.cols = std::max(s.cols, start.col + int(cols));
    });
    table->selectionModel()->select(
        QItemSelection(model->index(start.row, start.col),
                       model->index(start.row + int(cells.size()) - 1, start.col + int(cols) - 1)),
        QItemSelectionModel::ClearAndSelect);
}
void CellBookWindow::clear(bool formatting) {
    Position a, b;
    if (!selectionBounds(a, b))
        return;
    mutate([&] {
        auto &s = book.sheets[active];
        for (int r = a.row; r <= b.row; ++r)
            for (int c = a.col; c <= b.col; ++c) {
                auto it = s.cells.find({r, c});
                if (it != s.cells.end()) {
                    if (formatting)
                        it->second.style = Style{};
                    else
                        it->second.raw.clear();
                }
            }
    });
}
void CellBookWindow::style(const std::function<void(Style &)> &fn) {
    Position a, b;
    if (!selectionBounds(a, b))
        return;
    mutate([&] {
        for (int r = a.row; r <= b.row; ++r)
            for (int c = a.col; c <= b.col; ++c)
                fn(book.sheets[active].cells[{r, c}].style);
    });
}
void CellBookWindow::fill(bool right) {
    Position a, b;
    if (!selectionBounds(a, b))
        return;
    if ((right && a.col == b.col) || (!right && a.row == b.row))
        return;
    mutate([&] {
        auto &s = book.sheets[active];
        for (int r = a.row; r <= b.row; ++r)
            for (int c = a.col; c <= b.col; ++c) {
                Position source = right ? Position{r, a.col} : Position{a.row, c};
                auto it = s.cells.find(source);
                Cell cell = it == s.cells.end() ? Cell{} : it->second;
                cell.raw = translateFormula(cell.raw, r - source.row, c - source.col);
                s.cells[{r, c}] = cell;
            }
    });
}
void CellBookWindow::sort(bool descending) {
    Position a, b;
    if (!selectionBounds(a, b))
        return;
    if (a == b) {
        auto used = usedRange(book.sheets[active]);
        a = {0, 0};
        b = {used.bottom(), used.right()};
    }
    bool hasMerges = false;
    for (auto &m : book.sheets[active].merges)
        if (m.first.row <= b.row && m.second.row >= a.row && m.first.col <= b.col && m.second.col >= a.col)
            hasMerges = true;
    if (hasMerges) {
        QMessageBox::information(this, "정렬", "먼저 선택 범위의 셀 병합을 해제하세요.");
        return;
    }
    bool ok;
    QStringList columns;
    for (int c = a.col; c <= b.col; ++c)
        columns << q(columnName(c));
    auto chosen = QInputDialog::getItem(
        this, "정렬", "기준 열", columns,
        std::clamp(table->currentIndex().column() - a.col, 0, int(columns.size()) - 1), false, &ok);
    if (!ok)
        return;
    int key = a.col + columns.indexOf(chosen);
    bool header = QMessageBox::question(this, "정렬", "첫 행을 제목으로 제외할까요?") == QMessageBox::Yes;
    int first = a.row + (header ? 1 : 0);
    if (first >= b.row)
        return;
    std::vector<int> order;
    for (int r = first; r <= b.row; ++r)
        order.push_back(r);
    Calculator calc(book);
    std::map<int, Value> values;
    for (int r : order)
        values[r] = calc.get(active, {r, key});
    std::stable_sort(order.begin(), order.end(), [&](int x, int y) {
        auto va = values[x], vb = values[y];
        if (va.kind == Value::Blank || vb.kind == Value::Blank)
            return va.kind != Value::Blank && vb.kind == Value::Blank;
        if (va.kind == Value::Number && vb.kind == Value::Number)
            return descending ? va.number > vb.number : va.number < vb.number;
        int cmp = QString::compare(q(va.display()), q(vb.display()), Qt::CaseInsensitive);
        return descending ? cmp > 0 : cmp < 0;
    });
    mutate([&] {
        auto &s = book.sheets[active];
        auto old = s.cells;
        for (size_t n = 0; n < order.size(); ++n)
            for (int c = a.col; c <= b.col; ++c) {
                Position target{first + int(n), c}, source{order[n], c};
                auto it = old.find(source);
                if (it == old.end())
                    s.cells.erase(target);
                else {
                    auto cell = it->second;
                    cell.raw = translateFormula(cell.raw, target.row - source.row, 0);
                    s.cells[target] = cell;
                }
            }
    });
}
void CellBookWindow::filter() {
    auto i = table->currentIndex();
    if (!i.isValid())
        return;
    bool ok;
    auto text = QInputDialog::getText(this, "필터", "현재 열에서 포함할 텍스트 (빈칸: 필터 해제)",
                                      QLineEdit::Normal, {}, &ok);
    if (!ok)
        return;
    auto used = usedRange(book.sheets[active]);
    for (int r = 1; r < model->rowCount(); ++r) {
        bool hide = r <= used.bottom() && !text.isEmpty() &&
                    !model->data(model->index(r, i.column())).toString().contains(text, Qt::CaseInsensitive);
        table->setRowHidden(r, hide);
    }
    statusBar()->showMessage("첫 행은 제목으로 유지됩니다.", 4000);
}
void CellBookWindow::find(bool replace) {
    bool ok;
    auto text =
        QInputDialog::getText(this, replace ? "바꾸기" : "찾기", "찾을 텍스트", QLineEdit::Normal, {}, &ok);
    if (!ok || text.isEmpty())
        return;
    if (replace) {
        auto replacement =
            QInputDialog::getText(this, "바꾸기", "바꿀 텍스트 (현재 시트 전체)", QLineEdit::Normal, {}, &ok);
        if (!ok)
            return;
        mutate([&] {
            for (auto &[p, c] : book.sheets[active].cells) {
                auto raw = q(c.raw);
                if (raw.contains(text, Qt::CaseInsensitive)) {
                    raw.replace(text, replacement, Qt::CaseInsensitive);
                    c.raw = utf(raw);
                }
            }
        });
        return;
    }
    auto current = table->currentIndex();
    std::vector<Position> matches;
    for (auto &[p, c] : book.sheets[active].cells)
        if (q(c.raw).contains(text, Qt::CaseInsensitive) ||
            q(model->calc.get(active, p).display()).contains(text, Qt::CaseInsensitive))
            matches.push_back(p);
    if (matches.empty()) {
        QMessageBox::information(this, "찾기", "일치하는 값이 없습니다.");
        return;
    }
    Position target = matches.front();
    for (auto p : matches)
        if (Position{current.row(), current.column()} < p) {
            target = p;
            break;
        }
    table->setCurrentIndex(model->index(target.row, target.col));
    table->scrollTo(model->index(target.row, target.col));
}
void CellBookWindow::structure(bool rows, bool remove) {
    Position a, b;
    if (!selectionBounds(a, b))
        return;
    int at = rows ? a.row : a.col, count = rows ? b.row - a.row + 1 : b.col - a.col + 1;
    int size = rows ? book.sheets[active].rows : book.sheets[active].cols;
    if (remove && count >= size) {
        QMessageBox::information(this, "삭제", "최소 한 행과 한 열은 남겨야 합니다.");
        return;
    }
    if (!remove && size + count > (rows ? MaxRows : MaxCols)) {
        QMessageBox::information(this, "삽입", "시트 범위를 초과합니다.");
        return;
    }
    mutate([&] { editStructure(book, active, rows, at, count, remove); });
}
void CellBookWindow::merge(bool unmerge) {
    Position a, b;
    if (!selectionBounds(a, b))
        return;
    auto &s = book.sheets[active];
    if (!unmerge && a == b)
        return;
    if (!unmerge) {
        int filled = 0;
        for (int r = a.row; r <= b.row; ++r)
            for (int c = a.col; c <= b.col; ++c) {
                auto it = s.cells.find({r, c});
                if (it != s.cells.end() && !it->second.raw.empty() && !(Position{r, c} == a))
                    ++filled;
            }
        if (filled &&
            QMessageBox::question(this, "병합", "왼쪽 위 셀을 제외한 값은 지워집니다. 계속할까요?") !=
                QMessageBox::Yes)
            return;
    }
    mutate([&] {
        auto &sh = book.sheets[active];
        sh.merges.erase(std::remove_if(sh.merges.begin(), sh.merges.end(),
                                       [&](auto &m) {
                                           return m.first.row <= b.row && m.second.row >= a.row &&
                                                  m.first.col <= b.col && m.second.col >= a.col;
                                       }),
                        sh.merges.end());
        if (!unmerge) {
            for (int r = a.row; r <= b.row; ++r)
                for (int c = a.col; c <= b.col; ++c)
                    if (!(Position{r, c} == a))
                        sh.cells[{r, c}].raw.clear();
            sh.merges.push_back({a, b});
        }
    });
}
void CellBookWindow::addSheet() {
    if (book.sheets.size() >= 256) {
        QMessageBox::information(this, "시트", "최대 256개 시트를 지원합니다.");
        return;
    }
    int n = int(book.sheets.size()) + 1;
    std::string name;
    for (;;) {
        name = "Sheet" + std::to_string(n++);
        bool exists = false;
        for (auto &s : book.sheets)
            exists |= s.name == name;
        if (!exists)
            break;
    }
    mutate([&] {
        book.sheets.push_back({name});
        active = int(book.sheets.size()) - 1;
    });
}
void CellBookWindow::renameSheet() {
    bool ok;
    auto old = q(book.sheets[active].name);
    auto name = QInputDialog::getText(this, "시트 이름", "새 이름 (최대 31자)", QLineEdit::Normal, old, &ok)
                    .trimmed();
    if (!ok || name == old)
        return;
    if (name.isEmpty() || name.size() > 31 || name.contains(QRegularExpression("[\\\\/*?:\\[\\]]")) ||
        name.startsWith('\'') || name.endsWith('\'')) {
        QMessageBox::warning(this, "시트 이름", "이름에 사용할 수 없는 문자가 있습니다.");
        return;
    }
    for (auto &s : book.sheets)
        if (q(s.name).compare(name, Qt::CaseInsensitive) == 0) {
            QMessageBox::warning(this, "시트 이름", "같은 이름의 시트가 있습니다.");
            return;
        }
    mutate([&] {
        auto quoted = [](QString s) {
            s.replace("'", "''");
            return "'" + s + "'!";
        };
        auto oldQuoted = quoted(old), newQuoted = quoted(name);
        QRegularExpression unquoted("(?<![A-Za-z0-9_'])" + QRegularExpression::escape(old) + "!",
                                    QRegularExpression::CaseInsensitiveOption);
        for (auto &s : book.sheets)
            for (auto &[p, c] : s.cells)
                if (!c.raw.empty() && c.raw[0] == '=') {
                    auto raw = q(c.raw);
                    QString out;
                    bool string = false;
                    for (int i = 0; i < raw.size();) {
                        if (raw[i] == '"') {
                            out += raw[i++];
                            if (string && i < raw.size() && raw[i] == '"') {
                                out += raw[i++];
                                continue;
                            }
                            string = !string;
                            continue;
                        }
                        int end = raw.indexOf('"', i);
                        if (end < 0)
                            end = raw.size();
                        auto piece = raw.mid(i, end - i);
                        if (!string) {
                            piece.replace(oldQuoted, newQuoted, Qt::CaseInsensitive);
                            piece.replace(unquoted, newQuoted);
                        }
                        out += piece;
                        i = end;
                    }
                    c.raw = utf(out);
                }
        book.sheets[active].name = utf(name);
    });
}
void CellBookWindow::deleteSheet() {
    if (book.sheets.size() == 1) {
        QMessageBox::information(this, "시트 삭제", "최소 한 시트는 남겨야 합니다.");
        return;
    }
    if (QMessageBox::question(this, "시트 삭제", "현재 시트를 삭제할까요? 실행 취소할 수 있습니다.") !=
        QMessageBox::Yes)
        return;
    mutate([&] {
        book.sheets.erase(book.sheets.begin() + active);
        active = std::min(active, int(book.sheets.size()) - 1);
    });
}
void CellBookWindow::chart() {
    Position a, b;
    if (!selectionBounds(a, b))
        return;
    if (a.row == b.row) {
        QMessageBox::information(this, "차트", "제목 행과 숫자 데이터 행을 함께 선택하세요.");
        return;
    }
    bool ok;
    auto title = QInputDialog::getText(this, "차트", "차트 제목", QLineEdit::Normal, "차트", &ok);
    if (!ok)
        return;
    auto type = QInputDialog::getItem(this, "차트", "종류", {"막대", "꺾은선"}, 0, false, &ok);
    if (!ok)
        return;
    Chart c{utf(title), type == "꺾은선" ? 1 : 0, a, b};
    mutate([&] { book.sheets[active].charts.push_back(c); });
    auto *d = new QDialog(this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setWindowTitle(title);
    auto *l = new QVBoxLayout(d);
    auto *widget = new ChartWidget(book, active, c, d);
    l->addWidget(widget);
    auto *exportButton = new QPushButton("PNG로 저장…", d);
    l->addWidget(exportButton);
    connect(exportButton, &QPushButton::clicked, d, [this, widget] {
        auto path = QFileDialog::getSaveFileName(this, "차트 저장", "chart.png", "PNG (*.png)");
        if (!path.isEmpty() && !widget->grab().save(path))
            QMessageBox::warning(this, "저장 실패", "차트 이미지를 저장할 수 없습니다.");
    });
    d->show();
}
void CellBookWindow::zoomTo(int value) {
    zoom = value;
    model->zoom = value;
    static_cast<SheetModel *>(frozen->model())->zoom = value;
    static_cast<CellDelegate *>(table->itemDelegate())->zoom = value;
    static_cast<CellDelegate *>(frozen->itemDelegate())->zoom = value;
    rebuild();
}
void CellBookWindow::setupFrozen() {
    if (!freezeRow) {
        frozen->hide();
        return;
    }
    frozen->verticalHeader()->setFixedWidth(table->verticalHeader()->width());
    frozen->setRowHeight(0, table->rowHeight(0));
    frozen->setGeometry(table->frameWidth(), table->horizontalHeader()->height() + table->frameWidth(),
                        table->viewport()->width() + table->verticalHeader()->width(),
                        table->rowHeight(0) + 2);
    frozen->horizontalScrollBar()->setValue(table->horizontalScrollBar()->value());
    frozen->show();
    frozen->raise();
}
void CellBookWindow::autosum() {
    auto i = table->currentIndex();
    if (!i.isValid())
        return;
    int r = i.row() - 1;
    while (r >= 0 && !model->data(model->index(r, i.column())).toString().isEmpty())
        --r;
    if (r < i.row() - 1) {
        model->setData(i, "=SUM(" + q(address({r + 1, i.column()})) + ":" +
                              q(address({i.row() - 1, i.column()})) + ")");
        return;
    }
    int c = i.column() - 1;
    while (c >= 0 && !model->data(model->index(i.row(), c)).toString().isEmpty())
        --c;
    if (c < i.column() - 1)
        model->setData(i, "=SUM(" + q(address({i.row(), c + 1})) + ":" +
                              q(address({i.row(), i.column() - 1})) + ")");
}
void CellBookWindow::print(bool pdf) {
    QPrinter printer(QPrinter::HighResolution);
    QString path;
    if (pdf) {
        path = QFileDialog::getSaveFileName(this, "PDF 저장", "통합문서.pdf", "PDF (*.pdf)");
        if (path.isEmpty())
            return;
        if (!path.endsWith(".pdf", Qt::CaseInsensitive))
            path += ".pdf";
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(path);
    } else {
        QPrintDialog dialog(&printer, this);
        if (dialog.exec() != QDialog::Accepted)
            return;
    }
    auto &s = book.sheets[active];
    auto used = usedRange(s);
    int r0 = 0, c0 = 0, r1 = used.bottom(), c1 = used.right();
    auto selection = table->selectionModel()->selection();
    if (selection.size() == 1 && selection[0].width() * selection[0].height() > 1) {
        r0 = selection[0].top();
        c0 = selection[0].left();
        r1 = selection[0].bottom();
        c1 = selection[0].right();
    }
    if ((long long)(r1 - r0 + 1) * (c1 - c0 + 1) > 100000) {
        QMessageBox::warning(this, "인쇄", "100,000개 셀 이하의 범위를 선택하세요.");
        return;
    }
    QPainter painter;
    if (!painter.begin(&printer)) {
        QMessageBox::warning(this, "인쇄", "출력 장치를 열 수 없습니다.");
        return;
    }
    auto page = printer.pageLayout().paintRectPixels(printer.resolution());
    double totalWidth = 0;
    for (int c = c0; c <= c1; ++c)
        totalWidth += s.widths.count(c) ? s.widths.at(c) : 110;
    double scale = std::min(printer.resolution() / 96.0, double(page.width()) / std::max(totalWidth, 1.0));
    double titleHeight = 40;
    double logicalHeight = page.height() / scale;
    Calculator calc(book);
    double y = titleHeight;
    auto heading = [&] {
        painter.setFont(QFont("Malgun Gothic", 12));
        painter.drawText(QRectF(0, 0, totalWidth, 30), Qt::AlignLeft | Qt::AlignVCenter, q(s.name));
    };
    painter.scale(scale, scale);
    heading();
    for (int r = r0; r <= r1; ++r) {
        if (table->isRowHidden(r))
            continue;
        double height = s.heights.count(r) ? s.heights.at(r) : 28;
        if (y + height > logicalHeight) {
            if (!printer.newPage()) {
                painter.end();
                QMessageBox::warning(this, "인쇄", "새 페이지를 만들 수 없습니다.");
                return;
            }
            y = titleHeight;
            heading();
        }
        double x = 0;
        for (int c = c0; c <= c1; ++c) {
            double width = s.widths.count(c) ? s.widths.at(c) : 110;
            auto it = s.cells.find({r, c});
            Cell blank;
            auto &cell = it == s.cells.end() ? blank : it->second;
            QRectF rect(x, y, width, height);
            if (!cell.style.background.empty())
                painter.fillRect(rect, QColor(q(cell.style.background)));
            painter.setPen(QColor("#ccd6cf"));
            painter.drawRect(rect);
            painter.setPen(QColor(q(cell.style.foreground)));
            QFont f = fontFor(cell.style);
            f.setPixelSize(int(cell.style.size * 96 / 72.0));
            painter.setFont(f);
            auto v = calc.get(active, {r, c});
            int align = cell.style.alignment;
            int flags = Qt::AlignVCenter | (align == 2                              ? Qt::AlignHCenter
                                            : align == 3 || v.kind == Value::Number ? Qt::AlignRight
                                                                                    : Qt::AlignLeft);
            if (cell.style.wrap)
                flags |= Qt::TextWordWrap;
            painter.save();
            painter.setClipRect(rect);
            painter.drawText(rect.adjusted(4, 2, -4, -2), flags, formatValue(v, cell.style));
            painter.restore();
            x += width;
        }
        y += height;
    }
    painter.end();
    statusBar()->showMessage(pdf ? "PDF 저장 완료" : "인쇄 완료", 4000);
}
void CellBookWindow::selfTest() {
    auto require = [](bool ok, const char *m) {
        if (!ok)
            throw std::runtime_error(m);
    };
    auto cell = [&](int r, int c, const QString &s) {
        require(model->setData(model->index(r, c), s), "edit");
    };
    cell(0, 0, "10");
    cell(1, 0, "20");
    cell(2, 0, "=SUM(A1:A2)");
    require(model->data(model->index(2, 0)).toString() == "30", "GUI calculation");
    table->setCurrentIndex(model->index(2, 0));
    copy();
    table->setCurrentIndex(model->index(2, 1));
    paste();
    require(book.sheets[0].cells.at({2, 1}).raw == "=SUM(B1:B2)", "clipboard formula shift");
    history(false);
    require(!book.sheets[0].cells.count({2, 1}), "undo");
    history(true);
    require(book.sheets[0].cells.count({2, 1}), "redo");
    table->setCurrentIndex(model->index(0, 0));
    style([](Style &s) { s.bold = true; });
    require(model->data(model->index(0, 0), Qt::FontRole).value<QFont>().bold(), "style");
    table->selectionModel()->select(QItemSelection(model->index(2, 0), model->index(4, 0)),
                                    QItemSelectionModel::ClearAndSelect);
    fill();
    require(book.sheets[0].cells.at({3, 0}).raw == "=SUM(A2:A3)", "fill");
    addSheet();
    require(book.sheets.size() == 2 && active == 1, "new sheet");
    cell(0, 0, "=Sheet1!A3");
    require(model->data(model->index(0, 0)).toString() == "30", "GUI cross sheet");
    freezeRow = true;
    setupFrozen();
    require(!frozen->isHidden(), "frozen row");
    zoomTo(125);
    require(table->columnWidth(0) >= 100, "zoom");
    QTemporaryDir dir;
    require(saveWorkbook(dir.filePath("test.cellbook"), book).isEmpty(), "GUI save");
    require(loadFile(dir.filePath("test.cellbook"), false), "GUI load");
    require(book.sheets.size() == 2, "GUI reload");
    dirty = false;
}
