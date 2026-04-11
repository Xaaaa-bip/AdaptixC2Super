#ifndef ADAPTIXCLIENT_AUTHPROFILE_H
#define ADAPTIXCLIENT_AUTHPROFILE_H

#include <main.h>

class QWebSocket;

class AuthProfile
{
    QString project;
    QString username;
    QString password;
    QString host;
    QString port;
    QString endpoint;
    QString accessToken;
    QString refreshToken;
    QString projectDir;
    QStringList subscriptions;
    QStringList registeredCategories;
    bool consoleMultiuser = true;
    QString tlsCaCertPath;
    QString tlsClientCertPath;
    QString tlsClientKeyPath;

public:
    bool valid;

    AuthProfile();
    AuthProfile(const QString &project, const QString &username, const QString &password, const QString &host, const QString &port, const QString &endpoint, const QString &projectDir = QString());
    ~AuthProfile();

    QSslConfiguration sslConfiguration() const;
    static void applyTlsToWebSocket(QWebSocket *ws, const QSslConfiguration &cfg);

    QString GetProject();
    QString GetProjectDir() const;
    QString GetUsername();
    QString GetPassword();
    QString GetHost();
    QString GetPort();
    QString GetEndpoint();
    QString GetAccessToken();
    QString GetRefreshToken();
    QString GetURL() const;
    void    SetAccessToken(const QString &token);
    void    SetRefreshToken(const QString &token);
    QStringList GetSubscriptions() const;
    void    SetSubscriptions(const QStringList &subs);
    QStringList GetRegisteredCategories() const;
    void    SetRegisteredCategories(const QStringList &cats);
    bool    GetConsoleMultiuser() const;
    void    SetConsoleMultiuser(bool multiuser);

    QString GetTlsCaCertPath() const;
    QString GetTlsClientCertPath() const;
    QString GetTlsClientKeyPath() const;
    void    SetTlsCaCertPath(const QString &path);
    void    SetTlsClientCertPath(const QString &path);
    void    SetTlsClientKeyPath(const QString &path);
};

#endif