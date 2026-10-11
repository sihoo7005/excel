#pragma once
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace grid {
constexpr int MaxRows = 1048576, MaxCols = 16384;
struct Position {
    int row = 0, col = 0;
    bool operator<(const Position &p) const { return std::tie(row, col) < std::tie(p.row, p.col); }
    bool operator==(const Position &p) const { return row == p.row && col == p.col; }
};
struct Style {
    std::string font = "Malgun Gothic", foreground = "#202c38", background, numberFormat;
    int size = 11, alignment = 0;
    bool bold = false, italic = false, underline = false, border = false, wrap = false;
};
struct Cell {
    std::string raw;
    Style style;
};
struct Chart {
    std::string title = "Chart";
    int kind = 0;
    Position first, last;
};
struct Sheet {
    std::string name;
    std::map<Position, Cell> cells;
    std::map<int, int> widths, heights;
    std::vector<std::pair<Position, Position>> merges;
    int rows = 1000, cols = 100;
    std::vector<Chart> charts;
};
struct Workbook {
    std::vector<Sheet> sheets{{"Sheet1", {}, {}, {}, {}, 1000, 100}};
};
struct Value {
    enum Kind { Blank, Number, Text, Boolean, Error } kind = Blank;
    double number = 0;
    std::string text;
    static Value num(double n);
    static Value str(std::string s);
    static Value boolean(bool b);
    static Value error(std::string e);
    std::string display() const;
};
std::string columnName(int col);
std::string address(Position p);
bool parseAddress(const std::string &text, Position &p);
std::string translateFormula(const std::string &raw, int rowDelta, int colDelta);
// Structural edits update all references, including absolute references.
void editStructure(Workbook &book, int sheet, bool rows, int at, int count, bool remove);
class Calculator {
  public:
    explicit Calculator(const Workbook &book) : book(book) {}
    Value get(int sheet, Position p);
    void clear() {
        cache.clear();
        active.clear();
    }
    const Workbook &book;

  private:
    std::map<std::tuple<int, int, int>, Value> cache;
    std::set<std::tuple<int, int, int>> active;
};
std::vector<std::vector<std::string>> parseDelimited(const std::string &text, char delimiter);
std::string writeDelimited(const std::vector<std::vector<std::string>> &rows, char delimiter);
} // namespace grid
