#include "utils.h"
#include <iostream>
#include <algorithm>
#include <unordered_map>
#include <string>

void check(duckdb_state s, const char* msg) {
    if (s == DuckDBError) {
        std::cerr << "ERROR: " << msg << "\n";
        std::exit(1);
    }
}

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

int indentLevel(const std::string& line) {
    int c = 0;
    for (char ch : line) {
        if (ch == ' ') c++;
        else if (ch == '\t') c += 4;
        else break;
    }
    return c;
}

std::string replaceAll(std::string s, const std::unordered_map<std::string, std::string>& env) {
    for (const auto& [key, value] : env) {
        // 1. Explicit {{f.name}} style - always safe and preferred
        {
            std::string placeholder = "{{" + key + "}}";
            size_t pos = 0;
            while ((pos = s.find(placeholder, pos)) != std::string::npos) {
                s.replace(pos, placeholder.size(), value);
                pos += value.size();
            }
        }

        // 2. Shorthand `f.name` with better boundaries
        size_t pos = 0;
        while ((pos = s.find(key, pos)) != std::string::npos) {
            bool should_replace = true;

            // Check left boundary
            if (pos > 0) {
                char left = s[pos - 1];
                if (std::isalnum(left) || left == '_' || left == '.') {
                    should_replace = false;
                }
            }

            // Check right boundary
            if (pos + key.size() < s.size()) {
                char right = s[pos + key.size()];
                if (std::isalnum(right) || right == '_' || right == '.') {
                    should_replace = false;
                }
            }

            if (should_replace) {
                s.replace(pos, key.size(), value);
                pos += value.size();
            } else {
                pos += key.size();
            }
        }
    }
    return s;
}