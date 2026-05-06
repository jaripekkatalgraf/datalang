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
```

## Examples:
```
-- variable example
let files = SELECT * FROM (VALUES 
    (1, '2021/05/currency', 10), 
    (2, '2024/11/language', 20)) 
    files(id, name, howmany);

-- loop example
for f in files:
    SELECT * FROM read_csv('https://cdn.wsform.com/wp-content/uploads/{{f.name}}.csv') LIMIT f.howmany

let food_items = SELECT id, name, price, in_stock FROM items WHERE category = 'food';

-- if else
for i in food_items:
    let item = SELECT * FROM items WHERE id = i.id;
    
    if (SELECT in_stock FROM item):
        INSERT INTO cart SELECT id, name, price FROM item;
    else:
        INSERT INTO skipped VALUES (i.id, 'out_of_stock');
```