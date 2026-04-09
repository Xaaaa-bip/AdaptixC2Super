#include <Utils/IpdbReader.h>

#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>

namespace {

QMutex g_mtx;
bool   g_triedLoad = false;
bool   g_loaded    = false;
QHash<QString, QString> g_cache;

QByteArray g_data;
int        g_nodeCount = 0;
int        g_v4Offset  = 0;
int        g_langOff   = 0;
int        g_fieldCount = 0;

quint32 readNodeU32(int node, int index)
{
    const qsizetype off = qsizetype(node) * 8 + index * 4;
    if (off + 4 > g_data.size())
        return 0;
    const auto *p = reinterpret_cast<const uchar *>(g_data.constData() + off);
    return quint32(p[0]) << 24 | quint32(p[1]) << 16 | quint32(p[2]) << 8 | quint32(p[3]);
}

int computeV4Entry()
{
    int node = 0;
    for (int i = 0; i < 96 && node < g_nodeCount; ++i) {
        const int bit = (i >= 80) ? 1 : 0;
        node = int(readNodeU32(node, bit));
    }
    return node;
}

QByteArray resolvePayload(int node)
{
    const qint64 resolved = qint64(node) - qint64(g_nodeCount) + qint64(g_nodeCount) * 8;
    if (resolved < 0 || resolved + 2 > g_data.size())
        return {};
    const auto *p = reinterpret_cast<const uchar *>(g_data.constData() + resolved);
    const int sz = int(quint16(p[0]) << 8 | quint16(p[1]));
    if (sz < 0 || resolved + 2 + sz > g_data.size())
        return {};
    return g_data.mid(int(resolved) + 2, sz);
}

int searchIPv4(const unsigned char nb[4])
{
    if (!g_loaded)
        return -1;

    int node = g_v4Offset;
    for (int i = 0; i < 32; ++i) {
        if (node > g_nodeCount)
            break;
        const int bit = (int(nb[i >> 3]) >> (7 - (i & 7))) & 1;
        node = int(readNodeU32(node, bit));
    }
    if (node > g_nodeCount)
        return node;
    return -1;
}

QString formatRecord(const QByteArray &raw)
{
    if (raw.isEmpty())
        return {};
    const QString s = QString::fromUtf8(raw);
    const QStringList all = s.split(QLatin1Char('\t'));
    const int start = g_langOff;
    if (start + g_fieldCount > all.size())
        return {};
    QStringList parts;
    parts.reserve(g_fieldCount);
    for (int i = 0; i < g_fieldCount; ++i) {
        const QString t = all.at(start + i).trimmed();
        if (!t.isEmpty())
            parts.append(t);
    }
    return parts.join(QLatin1String(" "));
}

bool loadFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray body = f.readAll();
    f.close();
    if (body.size() < 8)
        return false;

    const quint32 metaLen = quint32(uchar(body[0])) << 24 | quint32(uchar(body[1])) << 16
        | quint32(uchar(body[2])) << 8 | quint32(uchar(body[3]));
    if (metaLen == 0 || 4 + int(metaLen) > body.size())
        return false;

    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(body.mid(4, int(metaLen)), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    const QJsonObject meta = doc.object();
    g_nodeCount = meta.value(QLatin1String("node_count")).toInt();
    const int totalSize = meta.value(QLatin1String("total_size")).toInt();
    if (g_nodeCount <= 0 || totalSize <= 0 || 4 + int(metaLen) + totalSize != body.size())
        return false;

    const QJsonObject langs = meta.value(QLatin1String("languages")).toObject();
    if (langs.isEmpty())
        return false;
    const QString langKey = langs.contains(QLatin1String("CN")) ? QLatin1String("CN") : langs.keys().value(0);
    g_langOff = langs.value(langKey).toInt(0);

    const QJsonArray fields = meta.value(QLatin1String("fields")).toArray();
    g_fieldCount = fields.size();
    if (g_fieldCount <= 0)
        return false;

    g_data = body.mid(4 + int(metaLen), totalSize);
    if (g_data.size() != totalSize)
        return false;

    if (meta.contains(QLatin1String("v4node")))
        g_v4Offset = meta.value(QLatin1String("v4node")).toInt(computeV4Entry());
    else
        g_v4Offset = computeV4Entry();

    return true;
}

} // namespace

QString IpdbReader::lookup(const QString &ipOrHost)
{
    QString s = ipOrHost.trimmed();
    if (s.isEmpty())
        return {};

    const int colon = s.indexOf(QLatin1Char(':'));
    if (colon > 0 && s.count(QLatin1Char(':')) == 1) {
        QHostAddress hostOnly;
        if (hostOnly.setAddress(s.left(colon)))
            s = s.left(colon);
    }

    QHostAddress addr;
    if (!addr.setAddress(s))
        return {};

    bool v4Ok = false;
    const quint32 v4word = addr.toIPv4Address(&v4Ok);
    if (!v4Ok)
        return {};

    QHostAddress v4only;
    v4only.setAddress(v4word);
    const QString cacheKey = v4only.toString();

    QMutexLocker lock(&g_mtx);

    if (!g_triedLoad) {
        g_triedLoad = true;
        const QString path = QDir::currentPath() + QLatin1String("/qqwry.ipdb");
        if (loadFile(path))
            g_loaded = true;
    }

    if (!g_loaded)
        return {};

    if (g_cache.contains(cacheKey))
        return g_cache.value(cacheKey);

    const QStringList pts = cacheKey.split(QLatin1Char('.'));
    if (pts.size() != 4)
        return {};

    unsigned char nb[4];
    for (int i = 0; i < 4; ++i) {
        bool ok = false;
        const int v = pts[i].toInt(&ok);
        if (!ok || v < 0 || v > 255)
            return {};
        nb[i] = uchar(v);
    }

    const int node = searchIPv4(nb);
    if (node < 0)
        return {};

    const QString result = formatRecord(resolvePayload(node));
    if (g_cache.size() < 8192)
        g_cache.insert(cacheKey, result);
    return result;
}
