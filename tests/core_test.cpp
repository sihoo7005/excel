#include "core.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace grid;
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
int main() {
    Workbook b;
    auto put = [&](const char *a, const char *v) {
        Position p;
        require(parseAddress(a, p), "address");
        b.sheets[0].cells[p].raw = v;
    };
    auto check = [&](const char *a, const char *v) {
        Position p;
        parseAddress(a, p);
        Calculator c(b);
        auto actual = c.get(0, p).display();
        if (actual != v)
            throw std::runtime_error(std::string(a) + ": expected " + v + ", got " + actual);
    };
    put("A1", "10");
    put("A2", "20");
    put("A3", "=SUM(A1:A2)");
    check("A3", "30");
    put("B1", "=A3*2+5");
    check("B1", "65");
    put("A1", "15");
    check("B1", "75");
    put("B2", "=IF(A1>0,ROUND(10/3,2),1/0)");
    check("B2", "3.33");
    put("B3", "=IFERROR(1/0,\"fallback\")");
    check("B3", "fallback");
    put("C1", "=C2");
    put("C2", "=C1");
    check("C1", "#CYCLE!");
    put("D1", "=SUMIF(A1:A2,\">15\")");
    check("D1", "20");
    put("D2", "=COUNTIF(A1:A2,\">=15\")");
    check("D2", "2");
    put("D3", "=LEFT(\"한글문자\",2)");
    check("D3", "한글");
    put("E1", "=1/0");
    check("E1", "#DIV/0!");
    put("E2", "=UNKNOWN(1)");
    check("E2", "#NAME?");
    put("E3", "=SUM(A1:A2)+2^3^2");
    check("E3", "99");
    put("F1", "'00123");
    check("F1", "00123");
    put("F2", "25%");
    check("F2", "0.25");
    b.sheets.push_back({"Second sheet"});
    b.sheets[1].cells[{0, 0}].raw = "='Sheet1'!A3";
    Calculator cross(b);
    require(cross.get(1, {0, 0}).number == 35, "cross sheet");
    require(translateFormula("=A1+$B2+C$3+$D$4+\"A1\"+LOG10(5)", 2, 1) == "=B3+$B4+D$3+$D$4+\"A1\"+LOG10(5)",
            "translate refs");
    require(translateFormula("=A1", -1, 0) == "=#REF!", "invalid relative ref");
    editStructure(b, 0, true, 0, 1, false);
    Calculator shifted(b);
    require(shifted.get(1, {0, 0}).number == 35, "insert refs");
    auto csv = parseDelimited("a,\"b,c\",\"say \"\"hi\"\"\"\r\n1,\"line\nbreak\",\r\n", ',');
    require(csv.size() == 2 && csv[0][1] == "b,c" && csv[0][2] == "say \"hi\"" && csv[1][2].empty(),
            "CSV quoted fields");
    require(parseDelimited(writeDelimited(csv, ','), ',') == csv, "CSV roundtrip");
    bool bad = false;
    try {
        parseDelimited("\"bad", ',');
    } catch (...) {
        bad = true;
    }
    require(bad, "CSV malformed");
    Position p;
    require(parseAddress("XFD1048576", p) && !parseAddress("XFE1", p) && !parseAddress("A0", p),
            "grid limits");
    std::cout << "Core checks passed\n";
}
