#include "utils.h"
#include <iostream>
#include <algorithm>

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

std::string replaceAll(std::string s, const std::unordered_map<std::string,std::string>& env) {
    for (auto& [k,v] : env) {
        size_t pos = 0;
        while ((pos = s.find(k, pos)) != std::string::npos) {
            s.replace(pos, k.size(), v);
            pos += v.size();
        }
    }
    return s;
}