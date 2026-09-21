#ifndef ADAPTIXCLIENT_RFBCLIENTWORKER_H
#define ADAPTIXCLIENT_RFBCLIENTWORKER_H

#include <QObject>
#include <QImage>
#include <QByteArray>
#include <QTcpSocket>
#include <QVector>
#include <array>
#include <zlib.h>

class RfbClientWorker : public QObject
{
Q_OBJECT

public:
    RfbClientWorker(QString host, quint16 port, bool shared, QObject* parent = nullptr);
    ~RfbClientWorker() override;

public Q_SLOTS:
    void start();
    void stop();
    void sendPointerEvent(quint8 buttonMask, quint16 x, quint16 y);
    void sendKeyEvent(bool down, quint32 keySym);

Q_SIGNALS:
    void connected();
    void disconnected();
    void statusChanged(const QString& status);
    void errorOccurred(const QString& error);
    void frameReady(const QImage& image);
    void desktopResized(int width, int height);
    void finished();

private Q_SLOTS:
    void onConnected();
    void onReadyRead();
    void onDisconnected();
    void onSocketError(QAbstractSocket::SocketError error);

private:
    enum class HandshakeState {
        ProtocolVersion,
        SecurityTypes,
        SecurityResult,
        ServerInit,
        Connected
    };

    enum class ServerMessageState {
        MessageType,
        FramebufferHeader,
        RectHeader,
        RectPayload,
        ServerCutTextHeader,
        ServerCutTextPayload
    };

    QString host;
    quint16 port = 0;
    bool shared = true;
    QTcpSocket* socket = nullptr;
    QByteArray buffer;
    HandshakeState handshakeState = HandshakeState::ProtocolVersion;
    ServerMessageState messageState = ServerMessageState::MessageType;
    bool protocol33 = false;
    int protocolMajor = 3;
    int protocolMinor = 8;
    bool stopped = false;

    QImage framebuffer;
    QString desktopName;

    quint16 pendingRects = 0;
    quint16 rectX = 0;
    quint16 rectY = 0;
    quint16 rectW = 0;
    quint16 rectH = 0;
    qint32 rectEncoding = 0;
    qint64 rectPayloadBytes = 0;
    quint32 cutTextBytes = 0;
    std::array<z_stream, 4> tightStreams{};
    std::array<bool, 4> tightStreamsReady{};
    z_stream zrleStream{};
    bool zrleStreamReady = false;

    bool processBuffer();
    bool processHandshake();
    bool processServerMessages();

    void fail(const QString& message);
    void sendClientInit();
    void sendSetPixelFormat();
    void sendSetEncodings();
    void sendFramebufferUpdateRequest(bool incremental);
    void applyRawRect(const QByteArray& payload);
    void applyCopyRect(const QByteArray& payload);
    bool tryConsumeVariableRect();
    bool tryConsumeTightRect(qsizetype* consumed, bool* needMore);
    bool applyTightRect(const QByteArray& decoded, int filterId, const QVector<QRgb>& palette);
    bool applyTightImageRect(const QByteArray& payload, const char* format);
    bool applyZrleRect(const QByteArray& payload);
    bool inflateExpected(z_stream& stream, bool& ready, const QByteArray& input, int expectedBytes, QByteArray* output);
    bool inflateZrle(const QByteArray& input, QByteArray* output);
    void resetCompressionStreams();
    void resizeFramebuffer(int width, int height);
    void fillRectRgb(QRgb color);
    void setRectPixel(int x, int y, QRgb color);

    static quint16 readU16(const char* p);
    static quint32 readU32(const char* p);
    static qint32 readS32(const char* p);
    static QRgb readCpixel(const char* p);
    static bool readCompactLength(const QByteArray& data, qsizetype offset, int* value, qsizetype* bytesRead);
    static void appendU16(QByteArray& out, quint16 value);
    static void appendU32(QByteArray& out, quint32 value);
    static void appendS32(QByteArray& out, qint32 value);
};

#endif
