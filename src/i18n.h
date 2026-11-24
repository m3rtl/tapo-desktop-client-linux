// Tapo Desktop Client – translations (German default, English)
#pragma once
#include <string>
#include <vector>

struct Language {
    const char* code;  // "de", "en"
    const char* name;  // shown in the settings
};

const std::vector<Language>& languages();
void set_language(const std::string& code);  // unknown codes fall back to German
std::string language();

// Translate an English UI string. Thread-safe; returns the input if no translation exists.
const char* tr(const char* en);
inline std::string trs(const char* en) { return tr(en); }
