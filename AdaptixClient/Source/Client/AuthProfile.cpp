#include <Client/AuthProfile.h>

#include <QFile>
#include <QSslCertificate>
#include <QSslKey>
#include <QWebSocket>

AuthProfile::AuthProfile()
{
    this->valid = false;
}

AuthProfile::AuthProfile(const QString &project, const QString &username, const QString &password, const QString &host, const QString &port, const QString &endpoint, const QString &projectDir)
{
    this->project = project.trimmed();
    this->projectDir = QDir::fromNativeSeparators(projectDir.trimmed());
    this->username = username.trimmed();
    this->password = password;
    this->host = host.trimmed();
    this->port = port.trimmed();
    this->endpoint = endpoint.trimmed();
    this->valid = true;
}

AuthProfile::~AuthProfile() {}

QString AuthProfile::GetTlsCaCertPath() const { return tlsCaCertPath; }

QString AuthProfile::GetTlsClientCertPath() const { return tlsClientCertPath; }

QString AuthProfile::GetTlsClientKeyPath() const { return tlsClientKeyPath; }

void AuthProfile::SetTlsCaCertPath(const QString &path) { tlsCaCertPath = path.trimmed(); }

void AuthProfile::SetTlsClientCertPath(const QString &path) { tlsClientCertPath = path.trimmed(); }

void AuthProfile::SetTlsClientKeyPath(const QString &path) { tlsClientKeyPath = path.trimmed(); }

QSslConfiguration AuthProfile::sslConfiguration() const
{
    QSslConfiguration cfg = QSslConfiguration::defaultConfiguration();
    cfg.setProtocol(QSsl::TlsV1_2OrLater);

    const QString caPath = tlsCaCertPath.trimmed();
    if (caPath.isEmpty()) {
        cfg.setPeerVerifyMode(QSslSocket::VerifyNone);
        return cfg;
    }

    QFile f(caPath);
    if (!f.open(QIODevice::ReadOnly)) {
        cfg.setPeerVerifyMode(QSslSocket::VerifyNone);
        return cfg;
    }
    const QByteArray pem = f.readAll();
    f.close();

    QList<QSslCertificate> cas = QSslCertificate::fromData(pem, QSsl::Pem);
    if (cas.isEmpty())
        cas = QSslCertificate::fromData(pem, QSsl::Der);
    if (cas.isEmpty()) {
        cfg.setPeerVerifyMode(QSslSocket::VerifyNone);
        return cfg;
    }

    cfg.setCaCertificates(cas);
    cfg.setPeerVerifyMode(QSslSocket::VerifyPeer);

    const QString cliCert = tlsClientCertPath.trimmed();
    const QString cliKey = tlsClientKeyPath.trimmed();
    if (!cliCert.isEmpty() && !cliKey.isEmpty()) {
        QFile cf(cliCert);
        QFile kf(cliKey);
        if (cf.open(QIODevice::ReadOnly) && kf.open(QIODevice::ReadOnly)) {
            QList<QSslCertificate> lc = QSslCertificate::fromDevice(&cf, QSsl::Pem);
            if (!lc.isEmpty())
                cfg.setLocalCertificate(lc.first());
            QSslKey key(&kf, QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey);
            if (key.isNull())
                key = QSslKey(&kf, QSsl::Ec, QSsl::Pem, QSsl::PrivateKey);
            if (!key.isNull())
                cfg.setPrivateKey(key);
        }
    }

    return cfg;
}

void AuthProfile::applyTlsToWebSocket(QWebSocket *ws, const QSslConfiguration &cfg)
{
    if (!ws)
        return;
    ws->setSslConfiguration(cfg);
    if (cfg.peerVerifyMode() == QSslSocket::VerifyNone) {
        QObject::connect(ws, &QWebSocket::sslErrors, ws, [ws](const QList<QSslError> &errors) {
            Q_UNUSED(errors)
            ws->ignoreSslErrors();
        });
    }
}

QString AuthProfile::GetProject() { return this->project; };

QString AuthProfile::GetProjectDir() const
{
    QString dirPath = this->projectDir.trimmed();
    if (dirPath.isEmpty()) {
        QDir home(QDir::homePath());
        QString basePath = home.filePath("AdaptixProjects");
        QDir baseDir(basePath);
        dirPath = baseDir.filePath(this->project.trimmed());
    }

    QDir().mkpath(dirPath);
    return dirPath;
}

QString AuthProfile::GetUsername() { return this->username; };

QString AuthProfile::GetPassword() { return this->password; };

QString AuthProfile::GetHost() { return this->host; };

QString AuthProfile::GetPort() { return this->port; };

QString AuthProfile::GetEndpoint() { return this->endpoint; };

QString AuthProfile::GetAccessToken() { return this->accessToken; };

QString AuthProfile::GetRefreshToken() { return this->refreshToken; };

QString AuthProfile::GetURL() const
{
    return "https://" + host + ":" + port + endpoint;
};

void AuthProfile::SetAccessToken(const QString &token) { this->accessToken = token; };

void AuthProfile::SetRefreshToken(const QString &token) { this->refreshToken = token; }

QStringList AuthProfile::GetSubscriptions() const { return this->subscriptions; }

void AuthProfile::SetSubscriptions(const QStringList &subs) { this->subscriptions = subs; }

QStringList AuthProfile::GetRegisteredCategories() const { return this->registeredCategories; }

void AuthProfile::SetRegisteredCategories(const QStringList &cats) { this->registeredCategories = cats; }

bool AuthProfile::GetConsoleMultiuser() const { return this->consoleMultiuser; }

void AuthProfile::SetConsoleMultiuser(bool multiuser) { this->consoleMultiuser = multiuser; }
