#include "parser.h"
#include "interpreter.h"
#include "utils.h"
#include <iostream>
#include <fstream>
#include <sstream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Usage: ./datalang file.dl\n";
        return 1;
    }

    std::ifstream f(argv[1]);
    std::stringstream buf; 
    buf << f.rdbuf();

    duckdb_database db; 
    duckdb_connection conn;
    check(duckdb_open(nullptr, &db), "open");
    check(duckdb_connect(db, &conn), "connect");

    Parser parser(buf.str());
    auto ast = parser.parseBlock();

    Interpreter interp(conn);
    interp.run(ast);

    duckdb_disconnect(&conn);
    duckdb_close(&db);
}