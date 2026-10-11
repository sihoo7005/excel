#include "core.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <regex>
#include <sstream>
#include <stdexcept>
namespace grid {
Value Value::num(double n) {
    if (!std::isfinite(n))
        return error("#NUM!");
    Value v;
    v.kind = Number;
    v.number = n;
    return v;
}
Value Value::str(std::string s) {
    Value v;
    v.kind = Text;
    v.text = std::move(s);
    return v;
}
Value Value::boolean(bool b) {
    Value v;
    v.kind = Boolean;
    v.number = b;
    return v;
}
Value Value::error(std::string s) {
    Value v;
    v.kind = Error;
    v.text = std::move(s);
    return v;
}
std::string Value::display() const {
    if (kind == Blank)
        return {};
    if (kind == Text || kind == Error)
        return text;
    if (kind == Boolean)
        return number ? "TRUE" : "FALSE";
    std::ostringstream o;
    o << std::setprecision(15) << number;
    return o.str();
}
std::string columnName(int c) {
    std::string s;
    for (++c; c > 0; c = (c - 1) / 26)
        s.insert(s.begin(), char('A' + (c - 1) % 26));
    return s;
}
std::string address(Position p) {
    return columnName(p.col) + std::to_string(p.row + 1);
}
bool parseAddress(const std::string &s, Position &p) {
    size_t i = 0;
    int c = 0;
    if (i < s.size() && s[i] == '$')
        ++i;
    auto start = i;
    while (i < s.size() && std::isalpha((unsigned char)s[i])) {
        c = c * 26 + std::toupper((unsigned char)s[i++]) - 'A' + 1;
        if (c > MaxCols)
            return false;
    }
    if (i == start)
        return false;
    if (i < s.size() && s[i] == '$')
        ++i;
    start = i;
    long long r = 0;
    while (i < s.size() && std::isdigit((unsigned char)s[i])) {
        r = r * 10 + s[i++] - '0';
        if (r > MaxRows)
            return false;
    }
    if (start == i || i != s.size() || r < 1)
        return false;
    p = {int(r - 1), c - 1};
    return true;
}
namespace {
std::string upper(std::string s) {
    for (char &c : s)
        c = char(std::toupper((unsigned char)c));
    return s;
}
bool numeric(const std::string &s, double &v) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return false;
    auto t = s.substr(start, s.find_last_not_of(" \t\r\n") - start + 1);
    char *e = nullptr;
    v = std::strtod(t.c_str(), &e);
    return e == t.c_str() + t.size() && std::isfinite(v);
}
Value number(Value v) {
    if (v.kind == Value::Number || v.kind == Value::Boolean)
        return v;
    if (v.kind == Value::Blank)
        return Value::num(0);
    double n;
    if (v.kind == Value::Text && numeric(v.text, n))
        return Value::num(n);
    return v.kind == Value::Error ? v : Value::error("#VALUE!");
}
bool truth(Value v) {
    return v.kind == Value::Text ? !v.text.empty() : v.number != 0;
}
using Arg = std::vector<Value>;
struct Parser {
    Calculator &calc;
    int sheet;
    std::string s;
    size_t at = 0;
    int depth = 0;
    void space() {
        while (at < s.size() && std::isspace((unsigned char)s[at]))
            ++at;
    }
    bool eat(const std::string &t) {
        space();
        if (s.compare(at, t.size(), t) == 0) {
            at += t.size();
            return true;
        }
        return false;
    }
    Value scalar(const Arg &a) { return a.empty() ? Value{} : a.front(); }
    Value binary(Value a, Value b, const std::string &op) {
        if (a.kind == Value::Error)
            return a;
        if (b.kind == Value::Error)
            return b;
        if (op == "&")
            return Value::str(a.display() + b.display());
        if (op == "=" || op == "<>" || op == "<" || op == ">" || op == "<=" || op == ">=") {
            int c;
            if (a.kind == Value::Text || b.kind == Value::Text) {
                auto x = upper(a.display()), y = upper(b.display());
                c = x < y ? -1 : x > y ? 1 : 0;
            } else
                c = a.number < b.number ? -1 : a.number > b.number ? 1 : 0;
            return Value::boolean(op == "="    ? c == 0
                                  : op == "<>" ? c != 0
                                  : op == "<"  ? c < 0
                                  : op == ">"  ? c > 0
                                  : op == "<=" ? c <= 0
                                               : c >= 0);
        }
        a = number(a);
        b = number(b);
        if (a.kind == Value::Error)
            return a;
        if (b.kind == Value::Error)
            return b;
        if (op == "+")
            return Value::num(a.number + b.number);
        if (op == "-")
            return Value::num(a.number - b.number);
        if (op == "*")
            return Value::num(a.number * b.number);
        if (op == "/")
            return b.number ? Value::num(a.number / b.number) : Value::error("#DIV/0!");
        return Value::num(std::pow(a.number, b.number));
    }
    Arg expression() {
        if (++depth > 128)
            throw std::runtime_error("depth");
        auto a = concat();
        for (;;) {
            std::string op;
            for (auto x : {"<=", ">=", "<>", "=", "<", ">"})
                if (eat(x)) {
                    op = x;
                    break;
                }
            if (op.empty())
                break;
            a = {binary(scalar(a), scalar(concat()), op)};
        }
        --depth;
        return a;
    }
    Arg concat() {
        auto a = add();
        while (eat("&"))
            a = {binary(scalar(a), scalar(add()), "&")};
        return a;
    }
    Arg add() {
        auto a = multiply();
        for (;;) {
            if (eat("+"))
                a = {binary(scalar(a), scalar(multiply()), "+")};
            else if (eat("-"))
                a = {binary(scalar(a), scalar(multiply()), "-")};
            else
                break;
        }
        return a;
    }
    Arg multiply() {
        auto a = power();
        for (;;) {
            if (eat("*"))
                a = {binary(scalar(a), scalar(power()), "*")};
            else if (eat("/"))
                a = {binary(scalar(a), scalar(power()), "/")};
            else
                break;
        }
        return a;
    }
    Arg power() {
        auto a = unary();
        while (eat("^"))
            a = {binary(scalar(a), scalar(unary()), "^")};
        return a;
    }
    Arg unary() {
        if (eat("+"))
            return {number(scalar(unary()))};
        if (eat("-"))
            return {binary(Value::num(0), scalar(unary()), "-")};
        auto a = primary();
        while (eat("%"))
            a = {binary(scalar(a), Value::num(100), "/")};
        return a;
    }
    std::string token() {
        space();
        size_t start = at;
        while (at < s.size() && (std::isalnum((unsigned char)s[at]) || s[at] == '_' || s[at] == '$' ||
                                 s[at] == '.' || (unsigned char)s[at] >= 128))
            ++at;
        return s.substr(start, at - start);
    }
    Arg reference(std::string id, int sh) {
        Position p;
        if (!parseAddress(id, p))
            return {Value::error("#NAME?")};
        if (eat(":")) {
            Position q;
            if (!parseAddress(token(), q))
                return {Value::error("#REF!")};
            int r0 = std::min(p.row, q.row), r1 = std::max(p.row, q.row), c0 = std::min(p.col, q.col),
                c1 = std::max(p.col, q.col);
            // ponytail: cap expanded ranges; use sparse range iterators above one million cells.
            if ((long long)(r1 - r0 + 1) * (c1 - c0 + 1) > 1000000)
                return {Value::error("#NUM!")};
            Arg a;
            for (int r = r0; r <= r1; ++r)
                for (int c = c0; c <= c1; ++c)
                    a.push_back(calc.get(sh, {r, c}));
            return a;
        }
        return {calc.get(sh, p)};
    }
    Arg primary() {
        space();
        if (eat("(")) {
            auto a = expression();
            if (!eat(")"))
                throw std::runtime_error("paren");
            return a;
        }
        if (at >= s.size())
            throw std::runtime_error("missing");
        if (s[at] == '"') {
            ++at;
            std::string v;
            bool closed = false;
            while (at < s.size()) {
                char c = s[at++];
                if (c == '"') {
                    if (at < s.size() && s[at] == '"') {
                        v += '"';
                        ++at;
                    } else {
                        closed = true;
                        break;
                    }
                } else
                    v += c;
            }
            if (!closed)
                throw std::runtime_error("quote");
            return {Value::str(v)};
        }
        if (s[at] == '#') {
            auto start = at;
            while (at < s.size() && s[at] != ',' && s[at] != ')' && s[at] != ';' && s[at] != ' ')
                ++at;
            return {Value::error(s.substr(start, at - start))};
        }
        if (std::isdigit((unsigned char)s[at]) || s[at] == '.') {
            char *e = nullptr;
            double n = std::strtod(s.c_str() + at, &e);
            if (e == s.c_str() + at)
                throw std::runtime_error("number");
            at = size_t(e - s.c_str());
            return {Value::num(n)};
        }
        std::string id;
        if (s[at] == '\'') {
            ++at;
            bool closed = false;
            while (at < s.size()) {
                char c = s[at++];
                if (c == '\'') {
                    if (at < s.size() && s[at] == '\'') {
                        id += '\'';
                        ++at;
                    } else {
                        closed = true;
                        break;
                    }
                } else
                    id += c;
            }
            if (!closed)
                throw std::runtime_error("sheet");
        } else
            id = token();
        if (id.empty())
            throw std::runtime_error("token");
        if (eat("!")) {
            int sh = -1;
            for (size_t i = 0; i < calc.book.sheets.size(); ++i)
                if (upper(calc.book.sheets[i].name) == upper(id))
                    sh = int(i);
            auto ref = token();
            if (sh < 0)
                return {Value::error("#REF!")};
            return reference(ref, sh);
        }
        auto name = upper(id);
        if (eat("(")) {
            std::vector<Arg> args;
            if (!eat(")")) {
                do {
                    args.push_back(expression());
                } while (eat(",") || eat(";"));
                if (!eat(")"))
                    throw std::runtime_error("arguments");
            }
            return {function(name, args)};
        }
        if (name == "TRUE" || name == "FALSE")
            return {Value::boolean(name == "TRUE")};
        return reference(id, sheet);
    }
    Value function(const std::string &n, const std::vector<Arg> &args) {
        auto arg = [&](size_t i) { return i < args.size() ? scalar(args[i]) : Value{}; };
        auto num = [&](size_t i) { return number(arg(i)); };
        if (n == "IF") {
            if (args.size() < 2 || args.size() > 3)
                return Value::error("#VALUE!");
            auto c = arg(0);
            if (c.kind == Value::Error)
                return c;
            return truth(c) ? arg(1) : args.size() == 3 ? arg(2) : Value::boolean(false);
        }
        if (n == "IFERROR") {
            if (args.size() != 2)
                return Value::error("#VALUE!");
            return arg(0).kind == Value::Error ? arg(1) : arg(0);
        }
        if (n == "ISBLANK")
            return Value::boolean(arg(0).kind == Value::Blank);
        if (n == "ISNUMBER")
            return Value::boolean(arg(0).kind == Value::Number);
        if (n == "ISERROR")
            return Value::boolean(arg(0).kind == Value::Error);
        std::vector<Value> all;
        for (auto &a : args)
            all.insert(all.end(), a.begin(), a.end());
        if (n == "COUNT" || n == "COUNTA") {
            int c = 0;
            for (auto &v : all)
                if (n == "COUNT" ? v.kind == Value::Number : v.kind != Value::Blank)
                    ++c;
            return Value::num(c);
        }
        for (auto &v : all)
            if (v.kind == Value::Error)
                return v;
        if (n == "SUM" || n == "AVERAGE" || n == "MIN" || n == "MAX" || n == "PRODUCT") {
            double result = n == "PRODUCT" ? 1 : 0;
            int count = 0;
            for (auto &a : args)
                for (auto v : a) {
                    if (v.kind == Value::Blank)
                        continue;
                    if (v.kind == Value::Text || v.kind == Value::Boolean) {
                        if (a.size() > 1)
                            continue;
                        v = number(v);
                        if (v.kind == Value::Error)
                            return v;
                    }
                    if (n == "MIN")
                        result = count ? std::min(result, v.number) : v.number;
                    else if (n == "MAX")
                        result = count ? std::max(result, v.number) : v.number;
                    else if (n == "PRODUCT")
                        result *= v.number;
                    else
                        result += v.number;
                    ++count;
                }
            if (n == "AVERAGE")
                return count ? Value::num(result / count) : Value::error("#DIV/0!");
            if (n == "PRODUCT" && !count)
                return Value::num(0);
            return Value::num(result);
        }
        if (n == "AND" || n == "OR" || n == "NOT") {
            bool result = n == "AND";
            if (n == "NOT")
                return Value::boolean(!truth(arg(0)));
            for (auto &v : all)
                if (n == "AND")
                    result = result && truth(v);
                else
                    result = result || truth(v);
            return Value::boolean(result);
        }
        if (n == "CONCAT" || n == "CONCATENATE") {
            std::string out;
            for (auto &v : all)
                out += v.display();
            return Value::str(out);
        }
        if (n == "COUNTIF" || n == "SUMIF") {
            if (args.size() < 2 || args.size() > 3)
                return Value::error("#VALUE!");
            auto criterion = arg(1).display();
            std::string op = "=";
            for (auto x : {"<=", ">=", "<>", "<", ">", "="})
                if (criterion.rfind(x, 0) == 0) {
                    op = x;
                    criterion.erase(0, std::string(x).size());
                    break;
                }
            double val;
            Value target = numeric(criterion, val) ? Value::num(val) : Value::str(criterion);
            auto sum = args.size() == 3 ? args[2] : args[0];
            if (n == "SUMIF" && sum.size() != args[0].size())
                return Value::error("#VALUE!");
            double total = 0;
            for (size_t i = 0; i < args[0].size(); ++i)
                if (truth(binary(args[0][i], target, op))) {
                    if (n == "COUNTIF")
                        ++total;
                    else if (sum[i].kind == Value::Number)
                        total += sum[i].number;
                }
            return Value::num(total);
        }
        if (n == "LEN" || n == "LEFT" || n == "RIGHT" || n == "MID" || n == "UPPER" || n == "LOWER" ||
            n == "TRIM") {
            auto str = arg(0).display();
            std::vector<std::string> chars;
            for (size_t i = 0; i < str.size();) {
                unsigned char c = str[i];
                size_t len = c < 128 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : 4;
                chars.push_back(str.substr(i, len));
                i += len;
            }
            if (n == "LEN")
                return Value::num(double(chars.size()));
            if (n == "UPPER" || n == "LOWER") {
                for (char &c : str)
                    if ((unsigned char)c < 128)
                        c = char(n == "UPPER" ? std::toupper(c) : std::tolower(c));
                return Value::str(str);
            }
            if (n == "TRIM") {
                std::istringstream in(str);
                std::string word, out;
                while (in >> word) {
                    if (!out.empty())
                        out += ' ';
                    out += word;
                }
                return Value::str(out);
            }
            auto x = args.size() > 1 ? num(1) : Value::num(1), y = args.size() > 2 ? num(2) : Value::num(0);
            if (x.kind == Value::Error)
                return x;
            if (y.kind == Value::Error)
                return y;
            if (x.number < 0 || x.number > 1e9 || y.number < 0 || y.number > 1e9 ||
                (n == "MID" && x.number < 1))
                return Value::error("#VALUE!");
            size_t start = n == "RIGHT" ? chars.size() - std::min(chars.size(), size_t(x.number))
                           : n == "MID" ? size_t(x.number - 1)
                                        : 0,
                   len = n == "MID" ? size_t(y.number) : size_t(x.number);
            std::string out;
            for (size_t i = start; i < chars.size() && i - start < len; ++i)
                out += chars[i];
            return Value::str(out);
        }
        auto x = num(0), y = num(1);
        if (x.kind == Value::Error)
            return x;
        if (y.kind == Value::Error)
            return y;
        double a = x.number, b = y.number;
        if (n == "ABS")
            return Value::num(std::abs(a));
        if (n == "INT")
            return Value::num(std::floor(a));
        if (n == "SQRT")
            return Value::num(std::sqrt(a));
        if (n == "POWER")
            return Value::num(std::pow(a, b));
        if (n == "MOD")
            return b ? Value::num(a - b * std::floor(a / b)) : Value::error("#DIV/0!");
        if (n == "ROUND" || n == "ROUNDUP" || n == "ROUNDDOWN") {
            if (std::abs(b) > 308)
                return Value::error("#NUM!");
            double scale = std::pow(10, std::trunc(b)), v = a * scale,
                   r = n == "ROUND"     ? std::round(v)
                       : n == "ROUNDUP" ? std::copysign(std::ceil(std::abs(v)), v)
                                        : std::trunc(v);
            return Value::num(r / scale);
        }
        return Value::error("#NAME?");
    }
};
std::string rewrite(const std::string &s, const std::function<std::string(const std::smatch &)> &fn) {
    static const std::regex ref(
        R"(((?:'(?:[^']|'')+'|[A-Za-z_][A-Za-z0-9_ ]*)!)?(\$?)([A-Za-z]{1,3})(\$?)([1-9][0-9]*))");
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '"') {
            out += s[i++];
            while (i < s.size()) {
                char c = s[i++];
                out += c;
                if (c == '"') {
                    if (i < s.size() && s[i] == '"')
                        out += s[i++];
                    else
                        break;
                }
            }
            continue;
        }
        size_t end = s.find('"', i);
        if (end == std::string::npos)
            end = s.size();
        auto part = s.substr(i, end - i);
        size_t last = 0;
        for (std::sregex_iterator it(part.begin(), part.end(), ref), done; it != done; ++it) {
            auto m = *it;
            size_t pos = size_t(m.position()), after = pos + size_t(m.length());
            if ((pos && (std::isalnum((unsigned char)part[pos - 1]) || part[pos - 1] == '_')) ||
                (after < part.size() &&
                 (std::isalnum((unsigned char)part[after]) || part[after] == '_' || part[after] == '(')))
                continue;
            out += part.substr(last, pos - last);
            out += fn(m);
            last = after;
        }
        out += part.substr(last);
        i = end;
    }
    return out;
}
} // namespace
Value Calculator::get(int sh, Position p) {
    if (sh < 0 || sh >= int(book.sheets.size()) || p.row < 0 || p.col < 0 || p.row >= MaxRows ||
        p.col >= MaxCols)
        return Value::error("#REF!");
    auto key = std::make_tuple(sh, p.row, p.col);
    auto c = cache.find(key);
    if (c != cache.end())
        return c->second;
    if (active.count(key))
        return Value::error("#CYCLE!");
    if (active.size() > 256)
        return Value::error("#NUM!");
    auto it = book.sheets[sh].cells.find(p);
    if (it == book.sheets[sh].cells.end() || it->second.raw.empty())
        return {};
    auto raw = it->second.raw;
    Value v;
    active.insert(key);
    if (raw[0] == '=') {
        try {
            Parser parser{*this, sh, raw.substr(1)};
            v = parser.scalar(parser.expression());
            parser.space();
            if (parser.at != parser.s.size())
                v = Value::error("#VALUE!");
        } catch (...) {
            v = Value::error("#VALUE!");
        }
    } else if (raw[0] == '\'')
        v = Value::str(raw.substr(1));
    else {
        double n;
        if (numeric(raw, n))
            v = Value::num(n);
        else if (raw.back() == '%' && numeric(raw.substr(0, raw.size() - 1), n))
            v = Value::num(n / 100);
        else if (upper(raw) == "TRUE" || upper(raw) == "FALSE")
            v = Value::boolean(upper(raw) == "TRUE");
        else
            v = Value::str(raw);
    }
    active.erase(key);
    cache[key] = v;
    return v;
}
std::string translateFormula(const std::string &s, int dr, int dc) {
    if (s.empty() || s[0] != '=')
        return s;
    return rewrite(s, [&](const std::smatch &m) {
        Position p;
        if (!parseAddress(m[2].str() + m[3].str() + m[4].str() + m[5].str(), p))
            return m.str();
        if (m[2].str().empty())
            p.col += dc;
        if (m[4].str().empty())
            p.row += dr;
        if (p.row < 0 || p.col < 0 || p.row >= MaxRows || p.col >= MaxCols)
            return std::string("#REF!");
        return m[1].str() + m[2].str() + columnName(p.col) + m[4].str() + std::to_string(p.row + 1);
    });
}
void editStructure(Workbook &book, int sh, bool rows, int at, int count, bool remove) {
    auto &sheet = book.sheets.at(sh);
    std::map<Position, Cell> cells;
    for (auto &[p, c] : sheet.cells) {
        Position q = p;
        int &index = rows ? q.row : q.col;
        if (remove && index >= at && index < at + count)
            continue;
        if (index >= at)
            index += remove ? -count : count;
        if (q.row >= 0 && q.col >= 0 && q.row < MaxRows && q.col < MaxCols)
            cells[q] = c;
    }
    sheet.cells = std::move(cells);
    auto &dims = rows ? sheet.heights : sheet.widths;
    std::map<int, int> adjusted;
    for (auto entry : dims) {
        int index = entry.first, size = entry.second;
        if (remove && index >= at && index < at + count)
            continue;
        if (index >= at)
            index += remove ? -count : count;
        adjusted[index] = size;
    }
    dims = std::move(adjusted);
    sheet.merges.clear();
    int &size = rows ? sheet.rows : sheet.cols;
    size = std::clamp(size + (remove ? -count : count), 1, rows ? MaxRows : MaxCols);
    for (size_t owner = 0; owner < book.sheets.size(); ++owner)
        for (auto &[p, c] : book.sheets[owner].cells)
            if (!c.raw.empty() && c.raw[0] == '=')
                c.raw = rewrite(c.raw, [&](const std::smatch &m) {
                    auto prefix = m[1].str();
                    bool targets = int(owner) == sh;
                    if (!prefix.empty()) {
                        prefix.pop_back();
                        if (prefix.front() == '\'') {
                            prefix = prefix.substr(1, prefix.size() - 2);
                            size_t pos = 0;
                            while ((pos = prefix.find("''", pos)) != std::string::npos)
                                prefix.replace(pos, 2, "'");
                        }
                        targets = upper(prefix) == upper(sheet.name);
                    }
                    if (!targets)
                        return m.str();
                    Position q;
                    if (!parseAddress(m[2].str() + m[3].str() + m[4].str() + m[5].str(), q))
                        return m.str();
                    int &index = rows ? q.row : q.col;
                    if (remove && index >= at && index < at + count)
                        return std::string("#REF!");
                    if (index >= at)
                        index += remove ? -count : count;
                    if (q.row >= MaxRows || q.col >= MaxCols)
                        return std::string("#REF!");
                    return m[1].str() + m[2].str() + columnName(q.col) + m[4].str() +
                           std::to_string(q.row + 1);
                });
}
std::vector<std::vector<std::string>> parseDelimited(const std::string &text, char delimiter) {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool quoted = false;
    size_t i = text.rfind("\xEF\xBB\xBF", 0) == 0 ? 3 : 0;
    for (; i < text.size(); ++i) {
        char c = text[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    field += '"';
                    ++i;
                } else
                    quoted = false;
            } else
                field += c;
        } else if (c == '"' && field.empty())
            quoted = true;
        else if (c == delimiter) {
            row.push_back(field);
            field.clear();
        } else if (c == '\r' || c == '\n') {
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
                ++i;
            row.push_back(field);
            rows.push_back(row);
            row.clear();
            field.clear();
        } else
            field += c;
    }
    if (quoted)
        throw std::runtime_error("Unclosed CSV quote");
    if (!field.empty() || !row.empty() || (!text.empty() && text.back() == delimiter)) {
        row.push_back(field);
        rows.push_back(row);
    }
    return rows;
}
std::string writeDelimited(const std::vector<std::vector<std::string>> &rows, char delimiter) {
    std::string out;
    for (auto &row : rows) {
        bool first = true;
        for (auto &f : row) {
            if (!first)
                out += delimiter;
            first = false;
            if (f.find_first_of(std::string(1, delimiter) + "\"\r\n") != std::string::npos) {
                out += '"';
                for (char c : f) {
                    out += c;
                    if (c == '"')
                        out += '"';
                }
                out += '"';
            } else
                out += f;
        }
        out += "\r\n";
    }
    return out;
}
} // namespace grid
