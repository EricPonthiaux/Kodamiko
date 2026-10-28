#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
#
# Builds a repository whose merge of feature/pricing into main stops on
# every kind of conflict, to try Kodamiko's conflict resolution by hand:
# several conflicts in one file, a base section (diff3), CRLF line
# endings, a file without a final newline, add/add, modify/delete,
# rename/rename, a binary file, and a file merged without conflict.
#
#   tools/merge-demo/make-merge-demo.sh <folder> [merge|diff3|zdiff3]
#
# The last argument is git's merge.conflictStyle (diff3 by default).
# <folder> is deleted first: give it a folder of its own.
set -euo pipefail

dir=${1:?folder}
style=${2:-diff3}
rm -rf "$dir"
mkdir -p "$dir"
cd "$dir"

git init -q -b main
git config user.name "Demo"
git config user.email "demo@example.com"
git config merge.conflictStyle "$style"
git config core.autocrlf false

commit() { git add -A && git commit -qm "$1"; }

# ---------------------------------------------------------------- base
mkdir -p src config docs assets

cat > src/Inventory.h <<'EOF'
#pragma once

#include <string>
#include <vector>

struct Item {
    std::string name;
    int quantity = 0;
};

class Inventory {
public:
    void add(const Item &item);
    bool remove(const std::string &name);
    int total() const;
    std::vector<Item> lowStock(int threshold) const;

private:
    std::vector<Item> items_;
};
EOF

cat > src/Inventory.cpp <<'EOF'
#include "Inventory.h"

#include <algorithm>

void Inventory::add(const Item &item)
{
    items_.push_back(item);
}

bool Inventory::remove(const std::string &name)
{
    auto it = std::find_if(items_.begin(), items_.end(),
                           [&](const Item &i) { return i.name == name; });
    if (it == items_.end())
        return false;
    items_.erase(it);
    return true;
}

int Inventory::total() const
{
    int sum = 0;
    for (const Item &item : items_)
        sum += item.quantity;
    return sum;
}

std::vector<Item> Inventory::lowStock(int threshold) const
{
    std::vector<Item> result;
    for (const Item &item : items_)
        if (item.quantity < threshold)
            result.push_back(item);
    return result;
}
EOF

cat > src/Legacy.cpp <<'EOF'
// Old import format, still used by the warehouse scripts.
#include "Inventory.h"

void importLegacy(Inventory &inventory)
{
    inventory.add({"widget", 10});
}
EOF

cat > src/Report.cpp <<'EOF'
#include "Inventory.h"

#include <iostream>

void printReport(const Inventory &inventory)
{
    std::cout << "Total: " << inventory.total() << '\n';
}
EOF

cat > src/Utils.cpp <<'EOF'
#include <string>

std::string trim(const std::string &text)
{
    const auto first = text.find_first_not_of(' ');
    const auto last = text.find_last_not_of(' ');
    return first == std::string::npos ? "" : text.substr(first, last - first + 1);
}

std::string upper(std::string text)
{
    for (char &c : text)
        c = char(toupper(c));
    return text;
}
EOF

cat > config/settings.json <<'EOF'
{
    "currency": "EUR",
    "lowStockThreshold": 5,
    "warehouses": ["Lyon", "Nantes"],
    "report": {
        "format": "text",
        "daily": true
    }
}
EOF

cat > README.md <<'EOF'
Inventory
=========

A small inventory manager.

Building
--------

    cmake -B build && cmake --build build

Usage
=======

Run `inventory --help`.
EOF

printf 'Changelog\r\n\r\n1.0\r\n- First release\r\n' > docs/windows.txt
printf '# Changelog\n\n## 1.0\n- First release' > CHANGELOG.md
printf '\x89PNG\r\n\x1a\n\x00\x00base' > assets/logo.bin

commit "Inventory 1.0"
git tag v1.0

# ---------------------------------------------------- feature/pricing
git switch -qc feature/pricing

python3 - <<'EOF'
import re
p = 'src/Inventory.h'
s = open(p).read()
s = s.replace('''    int quantity = 0;
};''', '''    int quantity = 0;
    double unitPrice = 0.0;
    std::string supplier;
};''')
s = s.replace('''    int total() const;''', '''    int total() const;
    double value() const;''')
open(p, 'w').write(s)

p = 'src/Inventory.cpp'
s = open(p).read()
s = s.replace('''void Inventory::add(const Item &item)
{
    items_.push_back(item);
}''', '''void Inventory::add(const Item &item)
{
    if (item.unitPrice < 0)
        throw std::invalid_argument("negative price for " + item.name);
    items_.push_back(item);
}''')
s = s.replace('''    if (it == items_.end())
        return false;
    items_.erase(it);
    return true;''', '''    if (it == items_.end())
        return false;
    lastRemoved_ = *it;
    items_.erase(it);
    return true;''')
s = s.replace('''    int sum = 0;
    for (const Item &item : items_)
        sum += item.quantity;
    return sum;
}''', '''    int sum = 0;
    for (const Item &item : items_)
        sum += item.quantity;
    return sum;
}

double Inventory::value() const
{
    double sum = 0;
    for (const Item &item : items_)
        sum += item.quantity * item.unitPrice;
    return sum;
}''')
s = s.replace('''        if (item.quantity < threshold)
            result.push_back(item);
    return result;''', '''        if (item.quantity < threshold && !item.supplier.empty())
            result.push_back(item);
    std::sort(result.begin(), result.end(),
              [](const Item &a, const Item &b) { return a.unitPrice > b.unitPrice; });
    return result;''')
s = s.replace('#include <algorithm>\n', '#include <algorithm>\n#include <stdexcept>\n')
open(p, 'w').write(s)
EOF
commit "Add unit prices and the stock value"

cat > src/Pricing.cpp <<'EOF'
#include "Inventory.h"

// Prices with VAT, by the feature branch.
double withVat(double price)
{
    return price * 1.20;
}
EOF
git rm -q src/Legacy.cpp
commit "Add VAT pricing, drop the legacy import"

python3 - <<'EOF'
p = 'config/settings.json'
s = open(p).read()
s = s.replace('"lowStockThreshold": 5,', '"lowStockThreshold": 3,\n    "vatRate": 0.20,')
s = s.replace('"format": "text",', '"format": "csv",')
open(p, 'w').write(s)
p = 'README.md'
s = open(p).read()
s = s.replace('A small inventory manager.', 'A small inventory manager, with prices and VAT.')
s = s.replace('Run `inventory --help`.', 'Run `inventory --help`, or `inventory value` for the stock value.')
open(p, 'w').write(s)
EOF
printf 'Changelog\r\n\r\n1.1 (pricing)\r\n- Unit prices\r\n- VAT\r\n\r\n1.0\r\n- First release\r\n' > docs/windows.txt
printf '# Changelog\n\n## 1.1\n- Prices and VAT\n\n## 1.0\n- First release' > CHANGELOG.md
printf '\x89PNG\r\n\x1a\n\x00\x00pricing-logo' > assets/logo.bin
git mv src/Report.cpp src/PriceReport.cpp
commit "Prices in the settings, the docs and the report"

# Utils: a change far from main's, merged without conflict.
python3 - <<'EOF'
p = 'src/Utils.cpp'
s = open(p).read()
s = s.replace("        c = char(toupper(c));", "        c = char(std::toupper(static_cast<unsigned char>(c)));")
open(p, 'w').write(s)
EOF
commit "Safe toupper"

# ----------------------------------------------------------------- main
git switch -q main

python3 - <<'EOF'
p = 'src/Inventory.h'
s = open(p).read()
s = s.replace('''    int quantity = 0;
};''', '''    int quantity = 0;
    std::string location;
};''')
s = s.replace('''    int total() const;''', '''    int total() const;
    int countAt(const std::string &location) const;''')
open(p, 'w').write(s)

p = 'src/Inventory.cpp'
s = open(p).read()
s = s.replace('''void Inventory::add(const Item &item)
{
    items_.push_back(item);
}''', '''void Inventory::add(const Item &item)
{
    // Same name, same place: the quantities add up.
    for (Item &existing : items_) {
        if (existing.name == item.name && existing.location == item.location) {
            existing.quantity += item.quantity;
            return;
        }
    }
    items_.push_back(item);
}''')
s = s.replace('''    if (it == items_.end())
        return false;
    items_.erase(it);
    return true;''', '''    if (it == items_.end())
        return false;
    log("removed " + name);
    items_.erase(it);
    return true;''')
s = s.replace('''    int sum = 0;
    for (const Item &item : items_)
        sum += item.quantity;
    return sum;
}''', '''    return std::accumulate(items_.begin(), items_.end(), 0,
                           [](int sum, const Item &item) { return sum + item.quantity; });
}

int Inventory::countAt(const std::string &location) const
{
    return int(std::count_if(items_.begin(), items_.end(),
                             [&](const Item &item) { return item.location == location; }));
}''')
s = s.replace('''        if (item.quantity < threshold)
            result.push_back(item);
    return result;''', '''        if (item.quantity <= threshold)
            result.push_back(item);
    std::sort(result.begin(), result.end(),
              [](const Item &a, const Item &b) { return a.quantity < b.quantity; });
    return result;''')
s = s.replace('#include <algorithm>\n', '#include <algorithm>\n#include <numeric>\n')
open(p, 'w').write(s)
EOF
commit "Locations, merged quantities, removal log"

cat >> src/Legacy.cpp <<'EOF'

void importLegacyLocations(Inventory &inventory)
{
    inventory.add({"widget", 10, "Lyon"});
}
EOF
cat > src/Pricing.cpp <<'EOF'
#include "Inventory.h"

#include <cmath>

// Prices rounded to the cent, by main.
double rounded(double price)
{
    return std::round(price * 100) / 100;
}
EOF
commit "Legacy import of locations, price rounding"

python3 - <<'EOF'
p = 'config/settings.json'
s = open(p).read()
s = s.replace('"lowStockThreshold": 5,', '"lowStockThreshold": 10,')
s = s.replace('"warehouses": ["Lyon", "Nantes"],', '"warehouses": ["Lyon", "Nantes", "Lille"],')
s = s.replace('"format": "text",', '"format": "html",')
open(p, 'w').write(s)
p = 'README.md'
s = open(p).read()
s = s.replace('A small inventory manager.', 'A small inventory manager for several warehouses.')
s = s.replace('Run `inventory --help`.', 'Run `inventory --help`, or `inventory at Lyon` for one warehouse.')
open(p, 'w').write(s)
EOF
printf 'Changelog\r\n\r\n1.1 (locations)\r\n- Warehouses\r\n\r\n1.0\r\n- First release\r\n' > docs/windows.txt
printf '# Changelog\n\n## 1.1\n- Warehouses and locations\n\n## 1.0\n- First release' > CHANGELOG.md
printf '\x89PNG\r\n\x1a\n\x00\x00main-logo' > assets/logo.bin
git mv src/Report.cpp src/StockReport.cpp
commit "Warehouses in the settings, the docs and the report"

# -------------------------------------------------------------- merge
git merge --no-edit feature/pricing >/dev/null 2>&1 || true
git status --short
