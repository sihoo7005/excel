#pragma once
#include "core.h"
#include <QAbstractTableModel>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QSlider>
#include <QSpinBox>
#include <QTabBar>
#include <QTableView>
#include <deque>
#include <functional>
class SheetModel : public QAbstractTableModel {
  public:
    grid::Workbook &book;
    int sheet = 0, zoom = 100;
    mutable grid::Calculator calc;
    bool showFormulas = false;
    std::function<void(grid::Position, QString)> edit;
    SheetModel(grid::Workbook &book, QObject *parent = nullptr);
    int rowCount(const QModelIndex & = {}) const override;
    int columnCount(const QModelIndex & = {}) const override;
    QVariant data(const QModelIndex &, int role = Qt::DisplayRole) const override;
    QVariant headerData(int, Qt::Orientation, int) const override;
    Qt::ItemFlags flags(const QModelIndex &) const override;
    bool setData(const QModelIndex &, const QVariant &, int role = Qt::EditRole) override;
    void refresh(int active);
};
class CellBookWindow : public QMainWindow {
  public:
    grid::Workbook book;
    SheetModel *model;
    QTableView *table;
    QTableView *frozen;
    QLineEdit *formula, *nameBox;
    QTabBar *tabs;
    QLabel *summary;
    QString filePath;
    bool dirty = false;
    explicit CellBookWindow();
    bool loadFile(const QString &, bool prompt = true);
    void selfTest();

  protected:
    void closeEvent(QCloseEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;

  private:
    int active = 0, zoom = 100;
    bool rebuilding = false, freezeRow = false;
    struct Snapshot {
        grid::Workbook book;
        int sheet;
        size_t bytes;
    };
    std::deque<Snapshot> undoStack, redoStack;
    QAction *undoAction, *redoAction;
    QComboBox *numberFormats;
    void mutate(const std::function<void()> &);
    void rebuild();
    void updateSelection();
    void updateTitle();
    void history(bool redo);
    void commitEditing();
    bool confirmSave();
    bool save(bool saveAs = false);
    void newBook();
    void copy(bool cut = false);
    void paste();
    void clear(bool formatting = false);
    void style(const std::function<void(grid::Style &)> &);
    void fill(bool right = false);
    void sort(bool descending);
    void filter();
    void find(bool replace);
    void structure(bool rows, bool remove);
    void merge(bool unmerge);
    void addSheet();
    void renameSheet();
    void deleteSheet();
    void chart();
    void print(bool pdf = false);
    void zoomTo(int);
    void setupFrozen();
    void autosum();
    bool selectionBounds(grid::Position &, grid::Position &, int max = 100000);
    QAction *action(QMenu *, const QString &, const QKeySequence &, std::function<void()>);
};
