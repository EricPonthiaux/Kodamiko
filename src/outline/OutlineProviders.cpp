#include "OutlineProviders.h"

#include "TextOutlines.h"
#include "TreeSitterOutline.h"
#include "syntax/TreeSitterGrammars.h"

namespace OutlineProviders {
namespace {

// Macros Qt de déclaration de classe, sans argument ni point-virgule.
const QByteArrayList qtClassMacros = {"Q_OBJECT", "Q_GADGET", "Q_NAMESPACE"};

} // namespace

std::unique_ptr<IOutlineProvider> create(const QString &languageId)
{
    if (const TreeSitterGrammar *grammar = TreeSitterGrammars::forLanguage(languageId)) {
        if (!grammar->outline)
            return nullptr;
        const bool isCpp = languageId == QLatin1String("cpp");
        return std::make_unique<TreeSitterOutline>(grammar->language, *grammar->outline,
                                                   isCpp ? qtClassMacros : QByteArrayList());
    }
    if (languageId == QLatin1String("markdown"))
        return std::make_unique<MarkdownOutline>();
    if (languageId == QLatin1String("cmake"))
        return std::make_unique<CMakeOutline>();
    return nullptr;
}

} // namespace OutlineProviders
