#include "interpreter.h"
#include "utils.h"
#include "parser.h"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <unistd.h>

// ====================== ANSI COLORS ======================
// Only emit color codes when stderr is a real terminal.
// Piped output (logs, CI) stays clean.
namespace {
    bool colors_on() {
        static bool v = isatty(STDERR_FILENO);
        return v;
    }
    const char* RED    = "\033[1;31m";
    const char* YELLOW = "\033[1;33m";
    const char* DIM    = "\033[2m";
    const char* RESET  = "\033[0m";

    std::string red   (const std::string& s) { return colors_on() ? RED    + s + RESET : s; }
    std::string yellow(const std::string& s) { return colors_on() ? YELLOW + s + RESET : s; }
    std::string dim   (const std::string& s) { return colors_on() ? DIM    + s + RESET : s; }
}

Interpreter::Interpreter(duckdb_connection c, bool verbose) : conn(c), verbose(verbose) {}

void Interpreter::run(const std::vector<ASTPtr>& prog, const std::string& source_file) {
    if (!source_file.empty()) {
        file_stack.push_back(std::filesystem::absolute(source_file).string());
    }
    execBlock(prog, {});
    if (!source_file.empty()) {
        file_stack.pop_back();
    }
}

void Interpreter::execBlock(const std::vector<ASTPtr>& block, Env env) {
    for (auto& n : block) {
        std::visit([&](auto&& x){ exec(x, env); }, n->node);
    }
}

// ====================== HELPERS ======================

std::string Interpreter::resolve(const std::string& sql, const Env& env) {
    return replaceEnvVars(replaceAll(sql, env));
}

// Only matches if the entire expression is a bare function call: name()
// Rejects anything with SQL keywords, spaces in the name, etc.
bool Interpreter::isFunctionCall(const std::string& sql, std::string& fn_name) {
    std::string t = trim(sql);
    if (t.empty()) return false;
    if (t.back() == ';') t.pop_back();
    t = trim(t);

    size_t lparen = t.find('(');
    if (lparen == std::string::npos) return false;

    // Everything after the closing paren must be empty
    size_t rparen = t.rfind(')');
    if (rparen == std::string::npos || rparen < lparen) return false;
    std::string after = trim(t.substr(rparen + 1));
    if (!after.empty()) return false;

    fn_name = trim(t.substr(0, lparen));
    if (fn_name.empty()) return false;

    // Name must be a plain identifier — no spaces, no SQL keywords sneaking in
    for (char c : fn_name) {
        if (!std::isalnum((unsigned char)c) && c != '_') return false;
    }
    return true;
}

bool Interpreter::evalCond(std::string cond, Env& env) {
    cond = resolve(cond, env);

    // Wrap in subquery so the expression is evaluated exactly once
    std::string query = "SELECT 1 FROM (SELECT (" + cond + ") AS _cond) WHERE _cond IS TRUE LIMIT 1";

    duckdb_result res;
    duckdb_state state = duckdb_query(conn, query.c_str(), &res);

    if (state == DuckDBError) {
        std::cerr << "ERROR in condition: " << cond << "\n";
        duckdb_destroy_result(&res);
        return false;
    }

    bool ok = (duckdb_row_count(&res) > 0);
    duckdb_destroy_result(&res);
    return ok;
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

// ====================== FUNCTION DEFINITION ======================

void Interpreter::exec(const FnStmt& s, Env& env) {
    functions[s.name] = s;
    if (verbose) std::cout << dim("✓ defined fn " + s.name + "()") << "\n";
}

// ====================== FUNCTION EXECUTION ======================
//
// Functions never touch the database during execution.
// - let x = ...   → accumulated as CTE (lazy, no temp table)
// - SELECT ...     → captured as the return value (last one wins)
// - everything else (INSERT, UPDATE, CREATE...) → side effect, runs immediately
//
// Nested function calls (let x = inner()) have their CTEs merged in,
// producing a flat single WITH chain in the returned FnResult.

FnResult Interpreter::execFn(const FnStmt& fn, Env env) {
    FnResult result;

    for (auto& n : fn.body) {
        if (auto* let = std::get_if<LetStmt>(&n->node)) {
            std::string sql = trim(resolve(let->sql, env));

            std::string inner_fn;
            if (isFunctionCall(sql, inner_fn) && functions.count(inner_fn)) {
                // Inline the called function's CTE chain
                FnResult inner = execFn(functions[inner_fn], env);
                for (auto& cte : inner.ctes) {
                    result.ctes.push_back(cte);
                }
                if (!inner.select.empty()) {
                    result.ctes.push_back({let->name, inner.select});
                }
            } else {
                result.ctes.push_back({let->name, sql});
            }

        } else if (auto* stmt = std::get_if<SQLStmt>(&n->node)) {
            std::string sql = trim(resolve(stmt->sql, env));
            std::string upper = sql;
            std::transform(upper.begin(), upper.end(), upper.begin(),
                           [](unsigned char c){ return std::toupper(c); });

            if (upper.rfind("SELECT", 0) == 0) {
                result.select = sql;  // last SELECT wins
            } else {
                exec(*stmt, env);     // side effect — INSERT, CREATE, etc.
            }
        } else {
            std::visit([&](auto&& x){ exec(x, env); }, n->node);
        }
    }

    return result;
}

// ====================== LET ======================

void Interpreter::exec(const LetStmt& s, Env& env) {
    std::string sql = trim(resolve(s.sql, env));

    std::string fn_name;
    if (isFunctionCall(sql, fn_name) && functions.count(fn_name)) {
        FnResult r = execFn(functions[fn_name], env);
        std::string built = r.build();
        if (!built.empty()) {
            std::string full = "CREATE OR REPLACE TEMP TABLE " + s.name +
                               " AS (" + built + ")";
            if (duckdb_query(conn, full.c_str(), nullptr) == DuckDBError) {
                std::cerr << "ERROR: let " << s.name << " = " << fn_name << "()\n";
                return;
            }
            if (verbose) std::cout << dim("✓ let " + s.name + " = " + fn_name + "()") << "\n";
        }
        return;
    }

    std::string full = "CREATE OR REPLACE TEMP TABLE " + s.name + " AS " + sql;
    if (duckdb_query(conn, full.c_str(), nullptr) == DuckDBError) {
        std::cerr << "ERROR: let " << s.name << "\n";
        return;
    }
    if (verbose) std::cout << dim("✓ let " + s.name) << "\n";
}

// ====================== FOR ======================

void Interpreter::exec(const ForStmt& s, Env& env) {
    std::string q = "SELECT * FROM " + s.source;
    duckdb_result res;
    check(duckdb_query(conn, q.c_str(), &res), "for");

    idx_t rows = duckdb_row_count(&res);
    idx_t cols = duckdb_column_count(&res);

    if (verbose) std::cout << dim("→ for " + s.var) << "\n";

    for (idx_t r = 0; r < rows; r++) {
        Env local = env;
        for (idx_t c = 0; c < cols; c++) {
            std::string col = duckdb_column_name(&res, c);
            char* val = duckdb_value_varchar(&res, c, r);
            local[s.var + "." + col] = val ? val : "NULL";
            if (val) duckdb_free(val);  // was leaking before
        }
        execBlock(s.body, local);
    }
    duckdb_destroy_result(&res);
}

// ====================== IF / WHILE ======================

void Interpreter::exec(const IfStmt& s, Env& env) {
    if (evalCond(s.cond, env)) {
        execBlock(s.thenb, env);
    } else {
        execBlock(s.elseb, env);
    }
}

void Interpreter::exec(const WhileStmt& s, Env& env) {
    if (verbose) std::cout << dim("→ while") << "\n";
    while (evalCond(s.cond, env)) {
        execBlock(s.body, env);
    }
}

// ====================== EXPECT ======================

void Interpreter::exec(const ExpectStmt& s, Env& env) {
    if (!evalCond(s.condition, env)) {
        std::string msg = s.message.empty()
            ? "Expectation failed: " + s.condition
            : s.message;

        if (s.action == "fail") {
            std::cerr << red("❌ FAIL: " + msg) << "\n";
            std::exit(1);
        } else {
            std::cerr << yellow("⚠️  WARN: " + msg) << "\n";
        }
    }
}

// ====================== PRINT ======================

void Interpreter::exec(const PrintStmt& s, Env& env) {
    std::string text = trim(resolve(s.text, env));

    std::string upper = text;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c){ return std::toupper(c); });

    if (upper.rfind("SELECT", 0) == 0) {
        duckdb_result res;
        duckdb_state state = duckdb_query(conn, text.c_str(), &res);
        if (state == DuckDBSuccess &&
            duckdb_row_count(&res) > 0 && duckdb_column_count(&res) > 0) {
            char* val = duckdb_value_varchar(&res, 0, 0);
            if (val) { std::cout << val << "\n"; duckdb_free(val); }
        } else if (state == DuckDBError) {
            std::cerr << "ERROR in print SQL: " << text << "\n";
        }
        duckdb_destroy_result(&res);
        return;
    }

    std::cout << text << "\n";
}

// ====================== IMPORT ======================

void Interpreter::exec(const ImportStmt& s, Env& env) {
    // Resolve relative to the currently executing file, not CWD.
    // This means import "utils.dl" works from any working directory
    // as long as the files sit together.
    std::filesystem::path base = file_stack.empty()
        ? std::filesystem::current_path()
        : std::filesystem::path(file_stack.back()).parent_path();

    std::filesystem::path filepath = std::filesystem::weakly_canonical(base / s.filename);

    if (!std::filesystem::exists(filepath)) {
        std::cerr << "ERROR: Cannot open import '" << filepath.string() << "'\n";
        return;
    }

    std::ifstream f(filepath);
    std::stringstream buf;
    buf << f.rdbuf();

    file_stack.push_back(filepath.string());

    Parser subparser(buf.str());
    auto subast = subparser.parseBlock();
    execBlock(subast, env);

    file_stack.pop_back();
    if (verbose) std::cout << dim("✓ imported " + filepath.string()) << "\n";
}

// ====================== SQL ======================

void Interpreter::exec(const SQLStmt& s, Env& env) {
    std::string sql = trim(resolve(s.sql, env));
    if (sql.empty()) return;

    // Standalone function call
    std::string fn_name;
    if (isFunctionCall(sql, fn_name) && functions.count(fn_name)) {
        if (verbose) std::cout << dim("→ calling " + fn_name + "()") << "\n";
        FnResult r = execFn(functions[fn_name], env);
        std::string built = r.build();
        if (built.empty()) return;

        if (!s.redirect_file.empty()) {
            std::string copy_sql = "COPY (" + built + ") TO '" + s.redirect_file +
                                   "' (FORMAT CSV, HEADER" + (s.append ? ", APPEND" : "") + ")";
            duckdb_query(conn, copy_sql.c_str(), nullptr);
            if (verbose) std::cout << dim("→ exported to " + s.redirect_file) << "\n";
        } else {
            duckdb_result res;
            if (duckdb_query(conn, built.c_str(), &res) == DuckDBSuccess) {
                printResult(&res);
            }
            duckdb_destroy_result(&res);
        }
        return;
    }

    // Redirect to file
    if (!s.redirect_file.empty()) {
        std::string copy_sql = "COPY (" + sql + ") TO '" + s.redirect_file +
                               "' (FORMAT CSV, HEADER" + (s.append ? ", APPEND" : "") + ")";
        duckdb_query(conn, copy_sql.c_str(), nullptr);
        if (verbose) std::cout << dim("→ exported to " + s.redirect_file) << "\n";
        return;
    }

    // Normal SQL
    duckdb_result res;
    if (duckdb_query(conn, sql.c_str(), &res) == DuckDBError) {
        std::cerr << "ERROR in SQL: " << sql << "\n";
        duckdb_destroy_result(&res);
        return;
    }

    // Only print tabular results for SELECT/WITH.
    // DuckDB returns a "Count" result column for INSERT/UPDATE/DELETE
    // (rows affected) which we suppress — it is noise for a scripting tool.
    std::string up = sql;
    std::transform(up.begin(), up.end(), up.begin(), [](unsigned char c){ return std::toupper(c); });
    bool is_query = up.rfind("SELECT", 0) == 0 || up.rfind("WITH", 0) == 0;

    if (is_query && duckdb_column_count(&res) > 0) {
        printResult(&res);
    }
    duckdb_destroy_result(&res);
}