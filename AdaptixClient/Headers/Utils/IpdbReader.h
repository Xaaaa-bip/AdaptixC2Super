#pragma once

#include <QString>

/// Offline IP lookup (IPIP ipdb / qqwry.ipdb format). Thread-safe after first load.
class IpdbReader {
public:
    /// Resolve external IP to a human-readable location string (e.g. country / region / city / ISP).
    static QString lookup(const QString &ipOrHost);

private:
    static bool ensureLoaded();
};
