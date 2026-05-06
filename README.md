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

## Example:
```
CREATE TABLE items(id INTEGER, name VARCHAR, category VARCHAR, price INTEGER, in_stock BOOLEAN);
INSERT INTO items VALUES
(1, 'Milk', 'food', 2, true),
(2, 'Bread', 'food', 3, true),
(3, 'Headphones', 'electronics', 50, false),
(4, 'Bananas', 'food', 1, true),
(5, 'Keyboard', 'electronics', 80, true);

CREATE TABLE cart(id INTEGER, name VARCHAR, price INTEGER);
CREATE TABLE skipped(id INTEGER, reason VARCHAR);

let food_items = SELECT id, name, price, in_stock FROM items WHERE category = 'food';

for i in food_items:
    let item = SELECT * FROM items WHERE id = i.id;
    
    if (SELECT in_stock FROM item):
        INSERT INTO cart SELECT id, name, price FROM item;
    else:
        INSERT INTO skipped VALUES (i.id, 'out_of_stock');
```