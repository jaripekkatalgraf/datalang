#include "parser.h"
#include "utils.h"
#include <sstream>
#include <regex>
#include <iostream>

Parser::Parser(const std::string& src) {
    std::stringstream ss(src);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }
}

// ====================== parseBlock (more robust) ======================
std::vector<ASTPtr> Parser::parseBlock(int baseIndent) {
    std::vector<ASTPtr> block;

    while (pos < (int)lines.size()) {
        int lvl = indentLevel(lines[pos]);
        if (lvl < baseIndent) break;

        std::string t = trim(lines[pos]);

        if (t.empty() || t[0] == '#' || t.rfind("--", 0) == 0) {
            pos++;
            continue;
        }

        if (lvl > baseIndent) {
            std::cerr << "Indentation error at line " << (pos+1) << "\n";
            std::exit(1);
        }

        // === KEY CHANGE: always check keywords first ===
        if (t.rfind("let ", 0) == 0) {
            block.push_back(parseLet(baseIndent));
        } else if (t.rfind("for ", 0) == 0) {
            block.push_back(parseFor(baseIndent));
        } else if (t.rfind("if ", 0) == 0) {
            block.push_back(parseIf(baseIndent));
        } else if (t.rfind("print ", 0) == 0) {
            block.push_back(parsePrint());
        } else if (t.rfind("import ", 0) == 0) {
            block.push_back(parseImport());
        } else {
            block.push_back(parseRawSQL(baseIndent));
        }

        // Do NOT increment pos here — the parseXXX functions already do it
    }
    return block;
}

std::string Parser::collectSQL(int minIndent, std::string& redirect_file, bool& append) {
    std::string sql;
    while (pos < (int)lines.size()) {
        int lvl = indentLevel(lines[pos]);
        if (lvl < minIndent) break;

        std::string t = trim(lines[pos]);
        if (t.empty() || t[0] == '#' || t.rfind("--", 0) == 0) {
            pos++; 
            continue;
        }

        if (t.rfind("let ", 0) == 0 || t.rfind("for ", 0) == 0 || 
            t.rfind("if ", 0) == 0 || t.rfind("print ", 0) == 0 || 
            t.rfind("import ", 0) == 0 || t.rfind("else", 0) == 0) {
            break;
        }

        // redirection...
        std::regex redir(R"(^(.*)\s*(>>|>)\s*([^\s>]+)\s*$)");
        std::smatch match;
        if (std::regex_match(t, match, redir)) {
            sql += (sql.empty() ? "" : "\n") + match[1].str();
            redirect_file = match[3].str();
            append = (match[2].str() == ">>");
            pos++;
            break;
        }

        if (!sql.empty()) sql += "\n";
        sql += lines[pos];
        pos++;
    }
    return trim(sql);
}

ASTPtr Parser::parseRawSQL(int baseIndent) {
    std::string redirect_file;
    bool append = false;
    std::string sql = collectSQL(baseIndent, redirect_file, append);
    return std::make_shared<ASTNode>(SQLStmt{sql, redirect_file, append});
}

ASTPtr Parser::parseLet(int baseIndent) {
    std::string line = trim(lines[pos]);
    auto eq = line.find('=');
    std::string name = trim(line.substr(4, eq - 4));
    std::string sql = trim(line.substr(eq + 1));

    pos++;
    std::string redirect; bool app = false;
    std::string more = collectSQL(baseIndent + 4, redirect, app);
    if (!more.empty()) {
        if (!sql.empty()) sql += "\n";
        sql += more;
    }
    return std::make_shared<ASTNode>(LetStmt{name, sql});
}

ASTPtr Parser::parseFor(int baseIndent) {
    std::string line = trim(lines[pos]);
    auto inpos = line.find(" in ");
    std::string var = trim(line.substr(4, inpos - 4));
    std::string src = trim(line.substr(inpos + 4, line.find(':') - (inpos + 4)));

    pos++;
    auto body = parseBlock(baseIndent + 4);
    return std::make_shared<ASTNode>(ForStmt{var, src, body});
}

ASTPtr Parser::parseIf(int baseIndent) {
    std::string line = trim(lines[pos]);
    size_t l = line.find('(');
    size_t r = line.rfind(')');
    std::string cond = (l != std::string::npos && r != std::string::npos) 
                       ? line.substr(l + 1, r - l - 1) 
                       : line.substr(3);   // fallback

    pos++;  // consume the if line

    auto thenb = parseBlock(baseIndent + 4);

    std::vector<ASTPtr> elseb;
    if (pos < (int)lines.size()) {
        std::string nxt = trim(lines[pos]);
        if (nxt.rfind("else if", 0) == 0) {
            elseb.push_back(parseIf(baseIndent));
        } else if (nxt.rfind("else:", 0) == 0) {
            pos++;
            elseb = parseBlock(baseIndent + 4);
        }
    }
    return std::make_shared<ASTNode>(IfStmt{cond, thenb, elseb});
}

ASTPtr Parser::parsePrint() {
    std::string text = trim(lines[pos].substr(6));
    pos++;
    return std::make_shared<ASTNode>(PrintStmt{text});
}

ASTPtr Parser::parseImport() {
    std::string line = trim(lines[pos]);
    size_t start = line.find('"');
    size_t end = line.rfind('"');
    std::string filename = (start != std::string::npos && end > start)
                         ? line.substr(start + 1, end - start - 1) : "";
    pos++;
    return std::make_shared<ASTNode>(ImportStmt{filename});
}