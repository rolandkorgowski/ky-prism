#pragma once
#include <QString>
namespace PrismLanguage {
void initialize();
bool english();
bool setEnglish(bool enabled, bool persist = true);
QString text(const QString &source);
QString metadata(const QString &source);
QString preferenceFile();
}
