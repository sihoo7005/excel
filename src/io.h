#pragma once
#include "core.h"
#include <QString>
namespace grid {
QString loadWorkbook(const QString &path, Workbook &book);
QString saveWorkbook(const QString &path, const Workbook &book, int activeSheet = 0);
} // namespace grid
