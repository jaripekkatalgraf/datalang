#pragma once
#include "ast.h"
#include "duckdb.h"
#include <unordered_map>

class Interpreter {
    duckdb_connection conn;

public:
    Interpreter(duckdb_connection c);
    void run(const std::vector<ASTPtr>& prog);

private:
    using Env = std::unordered_map<std::string,std::string>;

    void execBlock(const std::vector<ASTPtr>& block, Env env);

    void exec(const LetStmt& s, Env& env);
    void exec(const ForStmt& s, Env& env);
    void exec(const IfStmt& s, Env& env);
    void exec(const SQLStmt& s, Env& env);
    void exec(const PrintStmt& s, Env& env);
    void exec(const ImportStmt& s, Env& env);

    bool evalCond(std::string cond, Env& env);
    void printResult(duckdb_result* res);
};