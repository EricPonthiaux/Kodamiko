// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>

#include <optional>

// How a column names the one it references, read by the tables (to find
// their foreign keys) and by the relations (to find the referenced column).
namespace diagram::datamodel::detail {

struct Reference {
    QString table;
    QString column;
};

// A comment naming the referenced column: `references customer.id`,
// `ref customer.id`, `-> customer.id` or just `customer.id`.
std::optional<Reference> referencedColumn(const QString &comment);

} // namespace diagram::datamodel::detail
