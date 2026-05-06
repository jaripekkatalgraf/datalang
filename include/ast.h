#pragma once
#include <vector>
#include <memory>
#include <variant>
#include <string>

struct ASTNode;
using ASTPtr = std::shared_ptr<ASTNode>;

struct LetStmt    { std::string name, sql; };
struct ForStmt    { std::string var, source; std::vector<ASTPtr> body; };
struct IfStmt     { std::string cond; std::vector<ASTPtr> thenb, elseb; };
struct SQLStmt    { std::string sql; std::string redirect_file; bool append = false; };
struct PrintStmt  { std::string text; };
struct ImportStmt { std::string filename; };

struct ASTNode {
    std::variant<LetStmt, ForStmt, IfStmt, SQLStmt, PrintStmt, ImportStmt> node;
    
    template<typename T>
    ASTNode(T&& n) : node(std::forward<T>(n)) {}
};