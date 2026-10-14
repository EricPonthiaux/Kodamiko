#include "TextFile.h"

#include <QCoreApplication>
#include <QFile>
#include <QSaveFile>
#include <QStringDecoder>

namespace TextFile {
namespace {

const QByteArray utf8Bom("\xEF\xBB\xBF");

LineEnding detectLineEnding(const QByteArray &bytes)
{
    const qsizetype lf = bytes.indexOf('\n');
    if (lf > 0 && bytes.at(lf - 1) == '\r')
        return LineEnding::CrLf;
    if (lf >= 0)
        return LineEnding::Lf;
    if (bytes.contains('\r'))
        return LineEnding::Cr;
#ifdef Q_OS_WIN
    return LineEnding::CrLf;
#else
    return LineEnding::Lf;
#endif
}

// Heuristique des éditeurs courants : un octet nul au début = fichier binaire.
bool looksBinary(const QByteArray &bytes)
{
    return bytes.left(8192).contains('\0');
}

} // namespace

DecodedText decode(const QByteArray &bytes)
{
    DecodedText result;
    QByteArray content = bytes;
    if (content.startsWith(utf8Bom)) {
        result.format.hasUtf8Bom = true;
        content.remove(0, utf8Bom.size());
    }
    result.format.lineEnding = detectLineEnding(content);

    // Stateless : une séquence incomplète en fin de fichier est une erreur,
    // au lieu d'être gardée en attente d'octets qui ne viendront pas.
    QStringDecoder utf8(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    result.text = utf8.decode(content);
    if (utf8.hasError()) {
        result.format.isLatin1 = true;
        result.text = QString::fromLatin1(content);
    }
    return result;
}

QByteArray encode(const QString &text, const TextFileFormat &format)
{
    QByteArray bytes = format.isLatin1 ? text.toLatin1() : text.toUtf8();
    if (format.hasUtf8Bom)
        bytes.prepend(utf8Bom);
    return bytes;
}

std::optional<DecodedText> load(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return std::nullopt;
    }
    const QByteArray bytes = file.readAll();
    if (looksBinary(bytes)) {
        *error = QCoreApplication::translate("TextFile", "Fichier binaire : ouverture impossible dans l'éditeur.");
        return std::nullopt;
    }
    return decode(bytes);
}

bool save(const QString &path, const QString &text, const TextFileFormat &format, QString *error)
{
    const QByteArray bytes = encode(text, format);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace TextFile
