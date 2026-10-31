#pragma once
#include <QString>
#include <initializer_list>
#include <string>

// Interface languages. The texts of the program are written in English in the code; tx() / tr8() look them up in the
// table in Translate.cpp and return the translation for the language chosen at start (English when there is none).
// Lite lets the user choose the language (Settings > General); Full stays English.
enum class Language { English = 0, Portuguese, German, Spanish, Slovak };

void setLanguage(Language language);  // call once at start, before any window exists
Language currentLanguage();
Language languageFromSetting(int setting);  // QSettings ui/language: 0 = like Windows, 1..5 = English..Slovak

const char* tr8(const char* english);  // UTF-8 translation (or `english` itself)
inline QString tx(const char* english) { return QString::fromUtf8(tr8(english)); }

// Translation with %1, %2, ... replaced by `args` (for the loader, which works with std::string).
std::string tr8f(const char* english, std::initializer_list<std::string> args);

// "%1 object" / "%1 objects" with the number filled in; Slovak also has a form for 2-4 (key "<many>|few").
std::string plural8(long long n, const char* one, const char* many);
inline QString plural(long long n, const char* one, const char* many) { return QString::fromStdString(plural8(n, one, many)); }
