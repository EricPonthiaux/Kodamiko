// SPDX-License-Identifier: GPL-3.0-only

#include "HighlightQuery.h"

#include <algorithm>

namespace {

QString errorTypeName(TSQueryError error)
{
    switch (error) {
    case TSQueryErrorSyntax:
        return QStringLiteral("syntax");
    case TSQueryErrorNodeType:
        return QStringLiteral("unknown node type");
    case TSQueryErrorField:
        return QStringLiteral("unknown field");
    case TSQueryErrorCapture:
        return QStringLiteral("unknown capture");
    case TSQueryErrorStructure:
        return QStringLiteral("impossible structure");
    case TSQueryErrorLanguage:
        return QStringLiteral("incompatible language version");
    default:
        return QStringLiteral("error");
    }
}

std::optional<TSNode> capturedNode(const TSQueryMatch &match, uint32_t captureId)
{
    for (uint16_t i = 0; i < match.capture_count; ++i) {
        if (match.captures[i].index == captureId)
            return match.captures[i].node;
    }
    return std::nullopt;
}

QString nodeText(TSNode node, const char *documentText)
{
    const uint32_t start = ts_node_start_byte(node);
    const uint32_t end = ts_node_end_byte(node);
    return QString::fromUtf8(documentText + start, static_cast<qsizetype>(end - start));
}

} // namespace

HighlightQuery::HighlightQuery(const TSLanguage *language, const QByteArray &source)
{
    uint32_t errorOffset = 0;
    TSQueryError errorType = TSQueryErrorNone;
    query_ = ts_query_new(language, source.constData(), static_cast<uint32_t>(source.size()), &errorOffset, &errorType);
    if (!query_) {
        const QByteArray context = source.mid(errorOffset, 60);
        errorMessage_ = QStringLiteral("%1 at byte %2: %3")
                            .arg(errorTypeName(errorType))
                            .arg(errorOffset)
                            .arg(QString::fromUtf8(context));
        return;
    }
    compilePredicates();
}

HighlightQuery::~HighlightQuery()
{
    if (query_)
        ts_query_delete(query_);
}

uint32_t HighlightQuery::captureCount() const
{
    return ts_query_capture_count(query_);
}

QString HighlightQuery::captureName(uint32_t captureIndex) const
{
    uint32_t length = 0;
    const char *name = ts_query_capture_name_for_id(query_, captureIndex, &length);
    return QString::fromUtf8(name, length);
}

QString HighlightQuery::stringValue(uint32_t stringId) const
{
    uint32_t length = 0;
    const char *value = ts_query_string_value_for_id(query_, stringId, &length);
    return QString::fromUtf8(value, length);
}

// A pattern's predicates arrive as a sequence of steps:
//   String("match?") Capture(@x) String("^[A-Z]") Done  String("eq?") ... Done
void HighlightQuery::compilePredicates()
{
    const uint32_t patternCount = ts_query_pattern_count(query_);
    predicatesByPattern_.resize(patternCount);

    for (uint32_t pattern = 0; pattern < patternCount; ++pattern) {
        uint32_t stepCount = 0;
        const TSQueryPredicateStep *steps = ts_query_predicates_for_pattern(query_, pattern, &stepCount);

        uint32_t first = 0;
        while (first < stepCount) {
            uint32_t done = first;
            while (steps[done].type != TSQueryPredicateStepTypeDone)
                ++done;
            const std::span<const TSQueryPredicateStep> predicateSteps(steps + first, done - first);
            first = done + 1;

            if (std::optional<Predicate> predicate = compilePredicate(predicateSteps))
                predicatesByPattern_[pattern].push_back(std::move(*predicate));
        }
    }
}

// `steps`: the operator then its arguments, without the final Done.
// nullopt for a malformed predicate or one with no effect on highlighting.
std::optional<HighlightQuery::Predicate> HighlightQuery::compilePredicate(
    std::span<const TSQueryPredicateStep> steps) const
{
    if (steps.empty())
        return std::nullopt;
    const TSQueryPredicateStep &operatorStep = steps.front();
    const std::span<const TSQueryPredicateStep> arguments = steps.subspan(1);
    const bool isWellFormed = operatorStep.type == TSQueryPredicateStepTypeString && arguments.size() >= 2 &&
                              arguments[0].type == TSQueryPredicateStepTypeCapture;
    if (!isWellFormed)
        return std::nullopt;

    const QString op = stringValue(operatorStep.value_id);
    const auto stringArgument = [&](size_t i) { return stringValue(arguments[i].value_id); };

    Predicate predicate;
    predicate.captureId = arguments[0].value_id;
    predicate.negated = op.startsWith(QStringLiteral("not-"));
    const QString baseOp = predicate.negated ? op.mid(4) : op;

    if (baseOp == QStringLiteral("eq?")) {
        predicate.kind = Predicate::Kind::Equals;
        if (arguments[1].type == TSQueryPredicateStepTypeCapture)
            predicate.otherCaptureId = arguments[1].value_id;
        else
            predicate.literals = {stringArgument(1)};
    } else if (baseOp == QStringLiteral("match?")) {
        predicate.kind = Predicate::Kind::Matches;
        predicate.regex = QRegularExpression(stringArgument(1));
    } else if (baseOp == QStringLiteral("any-of?")) {
        predicate.kind = Predicate::Kind::AnyOf;
        for (size_t i = 1; i < arguments.size(); ++i)
            predicate.literals.append(stringArgument(i));
    } else {
        return std::nullopt; // #is-not?, #set!...: no effect on highlighting
    }
    return predicate;
}

bool HighlightQuery::matchSatisfiesPredicates(const TSQueryMatch &match, const char *documentText) const
{
    return std::ranges::all_of(predicatesByPattern_[match.pattern_index],
                               [&](const Predicate &predicate) { return isSatisfied(predicate, match, documentText); });
}

bool HighlightQuery::isSatisfied(const Predicate &predicate, const TSQueryMatch &match, const char *documentText) const
{
    const std::optional<TSNode> node = capturedNode(match, predicate.captureId);
    if (!node)
        return true;
    const QString text = nodeText(*node, documentText);

    bool result = false;
    switch (predicate.kind) {
    case Predicate::Kind::Equals:
        if (predicate.otherCaptureId) {
            const std::optional<TSNode> other = capturedNode(match, *predicate.otherCaptureId);
            result = other && nodeText(*other, documentText) == text;
        } else {
            result = predicate.literals.first() == text;
        }
        break;
    case Predicate::Kind::Matches:
        result = predicate.regex.match(text).hasMatch();
        break;
    case Predicate::Kind::AnyOf:
        result = predicate.literals.contains(text);
        break;
    }
    return predicate.negated ? !result : result;
}
