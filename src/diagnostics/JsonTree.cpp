#include "JsonTree.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>

#include <cmath>

namespace {

constexpr int maxDepth = 512;

QString tr(const char *text)
{
    return QCoreApplication::translate("JsonTree", text);
}

class Parser {
public:
    explicit Parser(QByteArrayView text)
        : text_(text)
    {
    }

    JsonParseResult parse()
    {
        JsonParseResult result;
        skipSpace();
        std::optional<JsonNode> root = value(0);
        if (root) {
            skipSpace();
            if (pos_ < text_.size())
                fail(tr("Unexpected text after the end of the document"));
        }
        if (!error_.isEmpty()) {
            result.error = error_;
            result.errorOffset = errorOffset_;
        } else {
            result.root = std::move(root);
        }
        return result;
    }

private:
    bool atEnd() const { return pos_ >= text_.size(); }
    char peek() const { return atEnd() ? '\0' : text_[pos_]; }

    std::nullopt_t fail(const QString &message)
    {
        return fail(message, int(pos_));
    }

    std::nullopt_t fail(const QString &message, int offset)
    {
        if (error_.isEmpty()) {
            error_ = message;
            errorOffset_ = offset;
        }
        return std::nullopt;
    }

    void skipSpace()
    {
        while (!atEnd() && (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r'))
            ++pos_;
    }

    std::optional<JsonNode> value(int depth)
    {
        if (depth > maxDepth)
            return fail(tr("Nested too deeply"));
        JsonNode node;
        node.start = int(pos_);
        switch (peek()) {
        case '{':
            if (!object(node, depth))
                return std::nullopt;
            break;
        case '[':
            if (!array(node, depth))
                return std::nullopt;
            break;
        case '"': {
            std::optional<QString> text = string();
            if (!text)
                return std::nullopt;
            node.type = QJsonValue::String;
            node.scalar = *text;
            break;
        }
        case 't':
        case 'f':
        case 'n':
            if (!literal(node))
                return std::nullopt;
            break;
        case '\0':
            if (atEnd())
                return fail(tr("Unexpected end of the document"));
            [[fallthrough]];
        default:
            if (peek() == '-' || (peek() >= '0' && peek() <= '9')) {
                if (!number(node))
                    return std::nullopt;
                break;
            }
            return fail(tr("Expected a value"));
        }
        node.end = int(pos_);
        return node;
    }

    bool object(JsonNode &node, int depth)
    {
        node.type = QJsonValue::Object;
        ++pos_; // {
        skipSpace();
        if (peek() == '}') {
            ++pos_;
            return true;
        }
        while (true) {
            skipSpace();
            if (peek() != '"')
                return fail(peek() == '}' ? tr("Trailing comma") : tr("Expected a key in quotes")), false;
            JsonMember member;
            member.keyStart = int(pos_);
            std::optional<QString> key = string();
            if (!key)
                return false;
            member.key = *key;
            member.keyEnd = int(pos_);
            skipSpace();
            if (peek() != ':')
                return fail(tr("Expected “:” after the key")), false;
            ++pos_;
            skipSpace();
            std::optional<JsonNode> child = value(depth + 1);
            if (!child)
                return false;
            member.value = std::move(*child);
            node.members.append(std::move(member));
            skipSpace();
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            if (peek() == '}') {
                ++pos_;
                return true;
            }
            return fail(atEnd() ? tr("Unclosed object: “}” expected") : tr("Expected “,” or “}”")), false;
        }
    }

    bool array(JsonNode &node, int depth)
    {
        node.type = QJsonValue::Array;
        ++pos_; // [
        skipSpace();
        if (peek() == ']') {
            ++pos_;
            return true;
        }
        while (true) {
            skipSpace();
            if (peek() == ']')
                return fail(tr("Trailing comma")), false;
            std::optional<JsonNode> child = value(depth + 1);
            if (!child)
                return false;
            node.items.append(std::move(*child));
            skipSpace();
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            if (peek() == ']') {
                ++pos_;
                return true;
            }
            return fail(atEnd() ? tr("Unclosed array: “]” expected") : tr("Expected “,” or “]”")), false;
        }
    }

    std::optional<int> hex4()
    {
        if (pos_ + 4 > text_.size())
            return fail(tr("Invalid \\u escape"));
        int code = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_ + i];
            const int digit = c >= '0' && c <= '9'   ? c - '0'
                              : c >= 'a' && c <= 'f' ? c - 'a' + 10
                              : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                     : -1;
            if (digit < 0)
                return fail(tr("Invalid \\u escape"));
            code = code * 16 + digit;
        }
        pos_ += 4;
        return code;
    }

    std::optional<QString> string()
    {
        const int start = int(pos_);
        ++pos_; // "
        QByteArray utf8;
        QString result;
        while (true) {
            if (atEnd())
                return fail(tr("Unclosed string"), start);
            const char c = text_[pos_];
            if (c == '"') {
                ++pos_;
                return result + QString::fromUtf8(utf8);
            }
            if (static_cast<unsigned char>(c) < 0x20)
                return fail(c == '\n' ? tr("Unclosed string") : tr("Control character in a string"));
            if (c != '\\') {
                utf8.append(c);
                ++pos_;
                continue;
            }
            result += QString::fromUtf8(utf8);
            utf8.clear();
            ++pos_;
            const char escape = peek();
            ++pos_;
            switch (escape) {
            case '"': result += QLatin1Char('"'); break;
            case '\\': result += QLatin1Char('\\'); break;
            case '/': result += QLatin1Char('/'); break;
            case 'b': result += QLatin1Char('\b'); break;
            case 'f': result += QLatin1Char('\f'); break;
            case 'n': result += QLatin1Char('\n'); break;
            case 'r': result += QLatin1Char('\r'); break;
            case 't': result += QLatin1Char('\t'); break;
            case 'u': {
                std::optional<int> code = hex4();
                if (!code)
                    return std::nullopt;
                result += QChar(char16_t(*code)); // surrogates pair up in QString
                break;
            }
            default:
                pos_ -= 2;
                return fail(tr("Invalid escape in a string"));
            }
        }
    }

    bool literal(JsonNode &node)
    {
        for (const auto &[word, value] : {std::pair{QByteArrayView("true"), QJsonValue(true)},
                                          std::pair{QByteArrayView("false"), QJsonValue(false)},
                                          std::pair{QByteArrayView("null"), QJsonValue()}}) {
            if (text_.sliced(pos_).startsWith(word)) {
                pos_ += word.size();
                node.type = value.type();
                node.scalar = value;
                return true;
            }
        }
        return fail(tr("Expected a value")), false;
    }

    bool number(JsonNode &node)
    {
        const qsizetype start = pos_;
        const auto digits = [this] {
            const qsizetype from = pos_;
            while (peek() >= '0' && peek() <= '9')
                ++pos_;
            return pos_ > from;
        };
        if (peek() == '-')
            ++pos_;
        if (peek() == '0')
            ++pos_;
        else if (!digits())
            return fail(tr("Invalid number")), false;
        if (peek() == '.') {
            ++pos_;
            if (!digits())
                return fail(tr("Invalid number")), false;
        }
        if (peek() == 'e' || peek() == 'E') {
            ++pos_;
            if (peek() == '+' || peek() == '-')
                ++pos_;
            if (!digits())
                return fail(tr("Invalid number")), false;
        }
        bool ok = false;
        const double number = QByteArray(text_.sliced(start, pos_ - start)).toDouble(&ok);
        if (!ok || !std::isfinite(number))
            return fail(tr("Invalid number"), int(start)), false;
        node.type = QJsonValue::Double;
        node.scalar = number;
        return true;
    }

    QByteArrayView text_;
    qsizetype pos_ = 0;
    QString error_;
    int errorOffset_ = 0;
};

} // namespace

QJsonValue JsonNode::toJson() const
{
    switch (type) {
    case QJsonValue::Object: {
        QJsonObject object;
        for (const JsonMember &member : members)
            object.insert(member.key, member.value.toJson());
        return object;
    }
    case QJsonValue::Array: {
        QJsonArray array;
        for (const JsonNode &item : items)
            array.append(item.toJson());
        return array;
    }
    default:
        return scalar;
    }
}

JsonParseResult parseJson(QByteArrayView utf8)
{
    return Parser(utf8).parse();
}
