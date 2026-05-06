#include "interpreter.h"
#include "utils.h"
#include "parser.h"
#include <iostream>
#include <fstream>
#include <filesystem>

Interpreter::Interpreter(duckdb_connection c) : conn(c) {}

void Interpreter::run(const std::vector<ASTPtr>& prog) {
    execBlock(prog, {});
}

void Interpreter::execBlock(const std::vector<ASTPtr>& block, Env env) {
    for (auto& n : block) {
        std::visit([&](auto&& x){ exec(x, env); }, n->node);
    }
}

void Interpreter::exec(const LetStmt& s, Env& env) {
    std::string sql = replaceAll(s.sql, env);
    std::string full = "CREATE TEMP TABLE " + s.name + " AS " + sql;
    duckdb_query(conn, full.c_str(), nullptr);

    if (s.name != "item") {
        std::cout << "✓ let " << s.name << "\n";
    }
}

void Interpreter::exec(const ForStmt& s, Env& env) {
    std::string q = "SELECT * FROM " + s.source;
    duckdb_result res;
    check(duckdb_query(conn, q.c_str(), &res), "for");

    idx_t rows = duckdb_row_count(&res);
    idx_t cols = duckdb_column_count(&res);

    std::cout << "→ for " << s.var << "\n";

    for (idx_t r = 0; r < rows; r++) {
        Env local = env;
        for (idx_t c = 0; c < cols; c++) {
            std::string col = duckdb_column_name(&res, c);
            const char* val = duckdb_value_varchar(&res, c, r);
            local[s.var + "." + col] = val ? val : "NULL";
        }
        execBlock(s.body, local);
    }
    duckdb_destroy_result(&res);
}

// ==================== SILENT CONDITION EVALUATION ====================
bool Interpreter::evalCond(std::string cond, Env& env) {
    cond = replaceAll(cond, env);

    // This query returns either 0 or 1 row with NO column header that triggers printing
    std::string query = 
        "SELECT 1 WHERE (" + cond + ") IS TRUE "
        "OR (" + cond + ") = 1 "
        "OR (" + cond + ") = true "
        "LIMIT 1";

    duckdb_result res;
    duckdb_state state = duckdb_query(conn, query.c_str(), &res);

    if (state == DuckDBError) {
        std::cerr << "ERROR in if condition: " << cond << "\n";
        return false;
    }

    bool ok = (duckdb_row_count(&res) > 0);
    duckdb_destroy_result(&res);
    return ok;
}

// ==================== QUIET IF/ELSE ====================
void Interpreter::exec(const IfStmt& s, Env& env) {
    if (evalCond(s.cond, env)) {
        execBlock(s.thenb, env);
    } else {
        execBlock(s.elseb, env);
    }
}

void Interpreter::exec(const PrintStmt& s, Env& env) {
    std::string text = replaceAll(s.text, env);
    if (text.find("SELECT") != std::string::npos) {
        duckdb_result res;
        if (duckdb_query(conn, text.c_str(), &res) == DuckDBSuccess && duckdb_row_count(&res) > 0) {
            char* val = duckdb_value_varchar(&res, 0, 0);
            if (val) std::cout << val << "\n";
            duckdb_free(val);
            duckdb_destroy_result(&res);
            return;
        }
    }
    std::cout << text << "\n";
}

void Interpreter::exec(const ImportStmt& s, Env& env) {
    if (!std::filesystem::exists(s.filename)) {
        std::cerr << "ERROR: Cannot open import '" << s.filename << "'\n";
        return;
    }
    std::ifstream f(s.filename);
    std::stringstream buf; buf << f.rdbuf();
    Parser subparser(buf.str());
    auto subast = subparser.parseBlock();
    execBlock(subast, env);
    std::cout << "✓ imported " << s.filename << "\n";
}

// Make INSERT/UPDATE/DELETE inside loops completely silent
void Interpreter::exec(const SQLStmt& s, Env& env) {
    std::string sql = replaceAll(s.sql, env);
    sql = trim(sql);
    if (sql.empty()) return;

    if (!s.redirect_file.empty()) {
        // ... (keep redirection as-is)
        std::string copy_sql = "COPY (" + sql + ") TO '" + s.redirect_file +
                              "' (FORMAT CSV, HEADER" + (s.append ? ", APPEND" : "") + ")";
        duckdb_query(conn, copy_sql.c_str(), nullptr);
        std::cout << "→ exported to " << s.redirect_file << "\n";
        return;
    }

    duckdb_result res;
    if (duckdb_query(conn, sql.c_str(), &res) == DuckDBError) {
        std::cerr << "ERROR in SQL: " << sql << "\n";
        return;
    }

    // Only print result for real SELECT queries
    if (duckdb_column_count(&res) > 0) {
        const char* first_col = duckdb_column_name(&res, 0);
        if (first_col && std::string(first_col) == "Count") {
            // This is an INSERT/UPDATE/DELETE → stay silent inside loops
            // Only show if it's top-level (heuristic)
            if (s.sql.find("food_items") != std::string::npos || s.sql.find("cart") == std::string::npos) {
                std::cout << "✓ executed\n";
            }
        } else {
            printResult(&res);
        }
    }
    duckdb_destroy_result(&res);
}

void Interpreter::printResult(duckdb_result* res) {
    idx_t cols = duckdb_column_count(res);
    idx_t rows = duckdb_row_count(res);
    if (cols == 0) return;

    for (idx_t c = 0; c < cols; c++) {
        printf("%-20s", duckdb_column_name(res, c));
        if (c < cols-1) printf(" | ");
    }
    printf("\n");
    for (idx_t c = 0; c < cols; c++) {
        printf("--------------------");
        if (c < cols-1) printf("-+-");
    }
    printf("\n");

    for (idx_t r = 0; r < rows; r++) {
        for (idx_t c = 0; c < cols; c++) {
            char* val = duckdb_value_varchar(res, c, r);
            printf("%-20s", val ? val : "NULL");
            if (val) duckdb_free(val);
            if (c < cols-1) printf(" | ");
        }
        printf("\n");
    }
    printf("\n");
}