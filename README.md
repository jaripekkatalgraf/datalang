# datalang — Beautiful SQL with minimal control flow

## Quick Start

```bash
git clone <your-repo-url>
cd datalang

# First time only - downloads DuckDB automatically
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Run
./build/bin/datalang example/shopping_test.dl