#include <Workers/RfbClientWorker.h>

#include <QBuffer>
#include <QHostAddress>
#include <QPainter>
#include <algorithm>

namespace {
constexpr qint32 EncodingRaw = 0;
constexpr qint32 EncodingCopyRect = 1;
constexpr qint32 EncodingTight = 7;
constexpr qint32 EncodingZrle = 16;
constexpr qint32 EncodingDesktopSize = -223;

constexpr quint8 SecurityNone = 1;
constexpr int TightFilterCopy = 0;
constexpr int TightFilterPalette = 1;
constexpr int TightFilterGradient = 2;
}

RfbClientWorker::RfbClientWorker(QString host, quint16 port, bool shared, QObject* parent) : QObject(parent)
{
    this->host = std::move(host);
    this->port = port;
    this->shared = shared;
}

RfbClientWorker::~RfbClientWorker()
{
    stop();
}

void RfbClientWorker::start()
{
    if (socket || stopped)
        return;

    socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, &RfbClientWorker::onConnected);
    connect(socket, &QTcpSocket::readyRead, this, &RfbClientWorker::onReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, &RfbClientWorker::onDisconnected);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(socket, &QTcpSocket::errorOccurred, this, &RfbClientWorker::onSocketError);
#else
    connect(socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error), this, &RfbClientWorker::onSocketError);
#endif

    Q_EMIT statusChanged(QString("Connecting to %1:%2").arg(host).arg(port));
    socket->connectToHost(host, port);
}

void RfbClientWorker::stop()
{
    if (stopped)
        return;

    stopped = true;
    resetCompressionStreams();
    if (socket) {
        socket->disconnect(this);
        socket->close();
        socket->deleteLater();
        socket = nullptr;
    }
    Q_EMIT finished();
}

void RfbClientWorker::sendPointerEvent(quint8 buttonMask, quint16 x, quint16 y)
{
    if (!socket || handshakeState != HandshakeState::Connected)
        return;

    QByteArray msg;
    msg.reserve(6);
    msg.append(char(5));
    msg.append(char(buttonMask));
    appendU16(msg, x);
    appendU16(msg, y);
    socket->write(msg);
}

void RfbClientWorker::sendKeyEvent(bool down, quint32 keySym)
{
    if (!socket || handshakeState != HandshakeState::Connected || keySym == 0)
        return;

    QByteArray msg;
    msg.reserve(8);
    msg.append(char(4));
    msg.append(char(down ? 1 : 0));
    msg.append(char(0));
    msg.append(char(0));
    appendU32(msg, keySym);
    socket->write(msg);
}

void RfbClientWorker::onConnected()
{
    Q_EMIT statusChanged("TCP connected, waiting for RFB handshake");
}

void RfbClientWorker::onReadyRead()
{
    if (!socket)
        return;

    buffer.append(socket->readAll());
    processBuffer();
}

void RfbClientWorker::onDisconnected()
{
    Q_EMIT disconnected();
    Q_EMIT statusChanged("Disconnected");
    stop();
}

void RfbClientWorker::onSocketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error)
    if (!socket || stopped)
        return;
    fail(socket->errorString());
}

bool RfbClientWorker::processBuffer()
{
    while (!stopped) {
        bool progressed = false;
        if (handshakeState != HandshakeState::Connected)
            progressed = processHandshake();
        else
            progressed = processServerMessages();

        if (!progressed)
            break;
    }
    return true;
}

bool RfbClientWorker::processHandshake()
{
    if (!socket)
        return false;

    switch (handshakeState) {
    case HandshakeState::ProtocolVersion: {
        if (buffer.size() < 12)
            return false;

        QByteArray version = buffer.left(12);
        buffer.remove(0, 12);
        if (!version.startsWith("RFB ")) {
            fail("Invalid RFB protocol banner");
            return false;
        }

        bool majorOk = false;
        bool minorOk = false;
        protocolMajor = version.mid(4, 3).toInt(&majorOk);
        protocolMinor = version.mid(8, 3).toInt(&minorOk);
        if (!majorOk || !minorOk || protocolMajor != 3) {
            fail(QString("Unsupported RFB protocol banner: %1").arg(QString::fromLatin1(version.trimmed())));
            return false;
        }

        QByteArray clientVersion("RFB 003.003\n", 12);
        protocol33 = protocolMinor < 7;
        if (protocolMinor >= 8) {
            clientVersion = QByteArray("RFB 003.008\n", 12);
        } else if (protocolMinor >= 7) {
            clientVersion = QByteArray("RFB 003.007\n", 12);
        }
        socket->write(clientVersion);
        handshakeState = HandshakeState::SecurityTypes;
        Q_EMIT statusChanged(QString("RFB server %1, using %2")
                                 .arg(QString::fromLatin1(version.trimmed()))
                                 .arg(QString::fromLatin1(clientVersion.trimmed())));
        return true;
    }
    case HandshakeState::SecurityTypes: {
        if (protocol33) {
            if (buffer.size() < 4)
                return false;

            quint32 securityType = readU32(buffer.constData());
            buffer.remove(0, 4);
            if (securityType != SecurityNone) {
                fail("VNC authentication is not supported yet; use a no-auth VNC server for this MVP");
                return false;
            }

            sendClientInit();
            handshakeState = HandshakeState::ServerInit;
            return true;
        }

        if (buffer.size() < 1)
            return false;

        quint8 count = quint8(buffer[0]);
        if (count == 0) {
            if (buffer.size() < 5)
                return false;
            quint32 reasonLen = readU32(buffer.constData() + 1);
            if (buffer.size() < 5 + int(reasonLen))
                return false;
            QString reason = QString::fromUtf8(buffer.mid(5, reasonLen));
            fail("RFB security negotiation failed: " + reason);
            return false;
        }

        if (buffer.size() < 1 + count)
            return false;

        bool supportsNone = false;
        for (int i = 0; i < count; ++i) {
            if (quint8(buffer[1 + i]) == SecurityNone) {
                supportsNone = true;
                break;
            }
        }
        buffer.remove(0, 1 + count);

        if (!supportsNone) {
            fail("VNC authentication is required, but this MVP only supports security type None");
            return false;
        }

        socket->write(QByteArray(1, char(SecurityNone)));
        handshakeState = HandshakeState::SecurityResult;
        return true;
    }
    case HandshakeState::SecurityResult: {
        if (buffer.size() < 4)
            return false;

        quint32 result = readU32(buffer.constData());
        buffer.remove(0, 4);
        if (result != 0) {
            fail(QString("RFB security result failed: %1").arg(result));
            return false;
        }

        sendClientInit();
        handshakeState = HandshakeState::ServerInit;
        return true;
    }
    case HandshakeState::ServerInit: {
        if (buffer.size() < 24)
            return false;

        quint16 width = readU16(buffer.constData());
        quint16 height = readU16(buffer.constData() + 2);
        quint32 nameLen = readU32(buffer.constData() + 20);
        if (buffer.size() < 24 + int(nameLen))
            return false;

        desktopName = QString::fromUtf8(buffer.mid(24, nameLen));
        buffer.remove(0, 24 + int(nameLen));

        resizeFramebuffer(width, height);
        sendSetPixelFormat();
        sendSetEncodings();
        sendFramebufferUpdateRequest(false);
        handshakeState = HandshakeState::Connected;

        Q_EMIT connected();
        Q_EMIT statusChanged(QString("Connected: %1 (%2x%3)").arg(desktopName).arg(width).arg(height));
        return true;
    }
    case HandshakeState::Connected:
        return false;
    }

    return false;
}

bool RfbClientWorker::processServerMessages()
{
    switch (messageState) {
    case ServerMessageState::MessageType: {
        if (buffer.size() < 1)
            return false;

        quint8 type = quint8(buffer[0]);
        buffer.remove(0, 1);
        if (type == 0) {
            messageState = ServerMessageState::FramebufferHeader;
        } else if (type == 2) {
            Q_EMIT statusChanged("Bell");
        } else if (type == 3) {
            messageState = ServerMessageState::ServerCutTextHeader;
        } else {
            fail(QString("Unsupported RFB server message type %1").arg(type));
            return false;
        }
        return true;
    }
    case ServerMessageState::FramebufferHeader: {
        if (buffer.size() < 3)
            return false;

        pendingRects = readU16(buffer.constData() + 1);
        buffer.remove(0, 3);
        messageState = pendingRects == 0 ? ServerMessageState::MessageType : ServerMessageState::RectHeader;
        if (pendingRects == 0)
            sendFramebufferUpdateRequest(true);
        return true;
    }
    case ServerMessageState::RectHeader: {
        if (pendingRects == 0) {
            Q_EMIT frameReady(framebuffer);
            sendFramebufferUpdateRequest(true);
            messageState = ServerMessageState::MessageType;
            return true;
        }

        if (buffer.size() < 12)
            return false;

        rectX = readU16(buffer.constData());
        rectY = readU16(buffer.constData() + 2);
        rectW = readU16(buffer.constData() + 4);
        rectH = readU16(buffer.constData() + 6);
        rectEncoding = readS32(buffer.constData() + 8);
        buffer.remove(0, 12);

        if (rectEncoding == EncodingRaw) {
            rectPayloadBytes = qint64(rectW) * qint64(rectH) * 4;
            messageState = ServerMessageState::RectPayload;
        } else if (rectEncoding == EncodingCopyRect) {
            rectPayloadBytes = 4;
            messageState = ServerMessageState::RectPayload;
        } else if (rectEncoding == EncodingTight || rectEncoding == EncodingZrle) {
            rectPayloadBytes = -1;
            messageState = ServerMessageState::RectPayload;
        } else if (rectEncoding == EncodingDesktopSize) {
            resizeFramebuffer(rectW, rectH);
            pendingRects--;
            messageState = ServerMessageState::RectHeader;
        } else {
            fail(QString("Unsupported RFB encoding %1; supported: Tight, ZRLE, CopyRect, Raw and DesktopSize").arg(rectEncoding));
            return false;
        }
        return true;
    }
    case ServerMessageState::RectPayload: {
        if (rectPayloadBytes < 0)
            return tryConsumeVariableRect();

        if (buffer.size() < rectPayloadBytes)
            return false;

        QByteArray payload = buffer.left(qsizetype(rectPayloadBytes));
        buffer.remove(0, qsizetype(rectPayloadBytes));

        if (rectEncoding == EncodingRaw)
            applyRawRect(payload);
        else if (rectEncoding == EncodingCopyRect)
            applyCopyRect(payload);

        pendingRects--;
        messageState = ServerMessageState::RectHeader;
        return true;
    }
    case ServerMessageState::ServerCutTextHeader: {
        if (buffer.size() < 7)
            return false;

        cutTextBytes = readU32(buffer.constData() + 3);
        buffer.remove(0, 7);
        messageState = ServerMessageState::ServerCutTextPayload;
        return true;
    }
    case ServerMessageState::ServerCutTextPayload: {
        if (buffer.size() < int(cutTextBytes))
            return false;

        buffer.remove(0, int(cutTextBytes));
        cutTextBytes = 0;
        messageState = ServerMessageState::MessageType;
        return true;
    }
    }

    return false;
}

void RfbClientWorker::fail(const QString& message)
{
    Q_EMIT errorOccurred(message);
    Q_EMIT statusChanged("Error: " + message);
    stop();
}

void RfbClientWorker::sendClientInit()
{
    socket->write(QByteArray(1, char(shared ? 1 : 0)));
}

void RfbClientWorker::sendSetPixelFormat()
{
    QByteArray msg;
    msg.reserve(20);
    msg.append(char(0));
    msg.append(char(0));
    msg.append(char(0));
    msg.append(char(0));
    msg.append(char(32));
    msg.append(char(24));
    msg.append(char(0));
    msg.append(char(1));
    appendU16(msg, 255);
    appendU16(msg, 255);
    appendU16(msg, 255);
    msg.append(char(16));
    msg.append(char(8));
    msg.append(char(0));
    msg.append(char(0));
    msg.append(char(0));
    msg.append(char(0));
    socket->write(msg);
}

void RfbClientWorker::sendSetEncodings()
{
    QByteArray msg;
    msg.reserve(24);
    msg.append(char(2));
    msg.append(char(0));
    appendU16(msg, 5);
    appendS32(msg, EncodingTight);
    appendS32(msg, EncodingZrle);
    appendS32(msg, EncodingCopyRect);
    appendS32(msg, EncodingDesktopSize);
    appendS32(msg, EncodingRaw);
    socket->write(msg);
}

void RfbClientWorker::sendFramebufferUpdateRequest(bool incremental)
{
    if (!socket || framebuffer.isNull())
        return;

    QByteArray msg;
    msg.reserve(10);
    msg.append(char(3));
    msg.append(char(incremental ? 1 : 0));
    appendU16(msg, 0);
    appendU16(msg, 0);
    appendU16(msg, quint16(framebuffer.width()));
    appendU16(msg, quint16(framebuffer.height()));
    socket->write(msg);
}

void RfbClientWorker::applyRawRect(const QByteArray& payload)
{
    if (framebuffer.isNull() || rectX >= framebuffer.width() || rectY >= framebuffer.height())
        return;

    const int copyW = qMin<int>(rectW, framebuffer.width() - rectX);
    const int copyH = qMin<int>(rectH, framebuffer.height() - rectY);
    const char* src = payload.constData();

    for (int y = 0; y < copyH; ++y) {
        QRgb* dst = reinterpret_cast<QRgb*>(framebuffer.scanLine(rectY + y)) + rectX;
        const quint32* row = reinterpret_cast<const quint32*>(src + (qsizetype(y) * rectW * 4));
        for (int x = 0; x < copyW; ++x)
            dst[x] = 0xff000000u | row[x];
    }
}

void RfbClientWorker::applyCopyRect(const QByteArray& payload)
{
    if (payload.size() < 4 || framebuffer.isNull())
        return;

    quint16 srcX = readU16(payload.constData());
    quint16 srcY = readU16(payload.constData() + 2);
    QRect srcRect(srcX, srcY, rectW, rectH);
    QRect dstRect(rectX, rectY, rectW, rectH);
    srcRect = srcRect.intersected(framebuffer.rect());
    dstRect = dstRect.intersected(framebuffer.rect());
    if (srcRect.isEmpty() || dstRect.isEmpty())
        return;

    QImage copy = framebuffer.copy(srcRect);
    QPainter painter(&framebuffer);
    painter.drawImage(dstRect.topLeft(), copy);
}

bool RfbClientWorker::tryConsumeVariableRect()
{
    if (rectEncoding == EncodingZrle) {
        if (buffer.size() < 4)
            return false;

        quint32 payloadLen = readU32(buffer.constData());
        if (payloadLen > 128u * 1024u * 1024u) {
            fail("ZRLE rectangle is too large");
            return false;
        }
        if (buffer.size() < 4 + int(payloadLen))
            return false;

        QByteArray payload = buffer.mid(4, int(payloadLen));
        buffer.remove(0, 4 + int(payloadLen));
        if (!applyZrleRect(payload))
            return false;
    } else if (rectEncoding == EncodingTight) {
        qsizetype consumed = 0;
        bool needMore = false;
        if (!tryConsumeTightRect(&consumed, &needMore))
            return !needMore ? false : false;
        if (needMore)
            return false;
        buffer.remove(0, consumed);
    } else {
        fail(QString("Internal error: variable RFB encoding %1").arg(rectEncoding));
        return false;
    }

    pendingRects--;
    messageState = ServerMessageState::RectHeader;
    return true;
}

bool RfbClientWorker::tryConsumeTightRect(qsizetype* consumed, bool* needMore)
{
    *consumed = 0;
    *needMore = false;
    if (buffer.size() < 1) {
        *needMore = true;
        return false;
    }

    qsizetype offset = 0;
    const quint8 control = quint8(buffer[offset++]);
    for (int i = 0; i < 4; ++i) {
        if ((control & (1u << i)) != 0 && tightStreamsReady[i]) {
            inflateEnd(&tightStreams[i]);
            tightStreams[i] = z_stream{};
            tightStreamsReady[i] = false;
        }
    }

    const quint8 subencoding = control >> 4;
    if (subencoding == 8) {
        if (buffer.size() < offset + 3) {
            *needMore = true;
            return false;
        }
        fillRectRgb(readCpixel(buffer.constData() + offset));
        *consumed = offset + 3;
        return true;
    }

    if (subencoding == 9 || subencoding == 10) {
        int payloadLen = 0;
        qsizetype lenBytes = 0;
        if (!readCompactLength(buffer, offset, &payloadLen, &lenBytes)) {
            *needMore = true;
            return false;
        }
        offset += lenBytes;
        if (payloadLen < 0 || buffer.size() < offset + payloadLen) {
            *needMore = true;
            return false;
        }
        QByteArray imagePayload = buffer.mid(offset, payloadLen);
        if (!applyTightImageRect(imagePayload, subencoding == 9 ? "JPG" : "PNG"))
            return false;
        *consumed = offset + payloadLen;
        return true;
    }

    if (subencoding > 7) {
        fail(QString("Unsupported Tight subencoding %1").arg(subencoding));
        return false;
    }

    int filterId = TightFilterCopy;
    QVector<QRgb> palette;
    if ((subencoding & 0x04) != 0) {
        if (buffer.size() < offset + 1) {
            *needMore = true;
            return false;
        }
        filterId = quint8(buffer[offset++]);
        if (filterId == TightFilterPalette) {
            if (buffer.size() < offset + 1) {
                *needMore = true;
                return false;
            }
            const int colors = quint8(buffer[offset++]) + 1;
            if (colors < 1 || colors > 256 || buffer.size() < offset + colors * 3) {
                *needMore = true;
                return false;
            }
            palette.reserve(colors);
            for (int i = 0; i < colors; ++i) {
                palette.push_back(readCpixel(buffer.constData() + offset));
                offset += 3;
            }
        } else if (filterId != TightFilterCopy && filterId != TightFilterGradient) {
            fail(QString("Unknown Tight filter %1").arg(filterId));
            return false;
        }
    }

    int expectedBytes = 0;
    if (filterId == TightFilterPalette)
        expectedBytes = palette.size() == 2 ? ((int(rectW) + 7) / 8) * int(rectH) : int(rectW) * int(rectH);
    else
        expectedBytes = int(rectW) * int(rectH) * 3;

    QByteArray decoded;
    if (expectedBytes < 12) {
        if (buffer.size() < offset + expectedBytes) {
            *needMore = true;
            return false;
        }
        decoded = buffer.mid(offset, expectedBytes);
        offset += expectedBytes;
    } else {
        int compressedLen = 0;
        qsizetype lenBytes = 0;
        if (!readCompactLength(buffer, offset, &compressedLen, &lenBytes)) {
            *needMore = true;
            return false;
        }
        offset += lenBytes;
        if (compressedLen < 0 || buffer.size() < offset + compressedLen) {
            *needMore = true;
            return false;
        }
        const int streamId = subencoding & 0x03;
        if (!inflateExpected(tightStreams[streamId], tightStreamsReady[streamId], buffer.mid(offset, compressedLen), expectedBytes, &decoded))
            return false;
        offset += compressedLen;
    }

    if (!applyTightRect(decoded, filterId, palette))
        return false;
    *consumed = offset;
    return true;
}

bool RfbClientWorker::applyTightRect(const QByteArray& decoded, int filterId, const QVector<QRgb>& palette)
{
    if (framebuffer.isNull())
        return true;

    if (filterId == TightFilterPalette) {
        if (palette.isEmpty()) {
            fail("Tight palette filter without a palette");
            return false;
        }

        qsizetype offset = 0;
        if (palette.size() == 2) {
            const int stride = (int(rectW) + 7) / 8;
            if (decoded.size() < stride * int(rectH)) {
                fail("Truncated Tight 2-color palette payload");
                return false;
            }
            for (int y = 0; y < int(rectH); ++y) {
                for (int x = 0; x < int(rectW); ++x) {
                    const quint8 byte = quint8(decoded[offset + x / 8]);
                    const int bit = 7 - (x & 7);
                    setRectPixel(x, y, palette[(byte >> bit) & 1]);
                }
                offset += stride;
            }
            return true;
        }

        if (decoded.size() < int(rectW) * int(rectH)) {
            fail("Truncated Tight palette payload");
            return false;
        }
        for (int y = 0; y < int(rectH); ++y) {
            for (int x = 0; x < int(rectW); ++x) {
                const int index = quint8(decoded[offset++]);
                if (index >= palette.size()) {
                    fail("Tight palette index out of range");
                    return false;
                }
                setRectPixel(x, y, palette[index]);
            }
        }
        return true;
    }

    if (decoded.size() < int(rectW) * int(rectH) * 3) {
        fail("Truncated Tight truecolor payload");
        return false;
    }

    qsizetype offset = 0;
    if (filterId == TightFilterGradient) {
        QVector<uchar> prevRow(int(rectW) * 3);
        QVector<uchar> curRow(int(rectW) * 3);
        for (int y = 0; y < int(rectH); ++y) {
            uchar left[3] = {0, 0, 0};
            for (int x = 0; x < int(rectW); ++x) {
                uchar pixel[3] = {0, 0, 0};
                for (int c = 0; c < 3; ++c) {
                    int up = prevRow[x * 3 + c];
                    int upLeft = x == 0 ? 0 : prevRow[(x - 1) * 3 + c];
                    int predictor = qBound(0, int(left[c]) + up - upLeft, 255);
                    pixel[c] = uchar((quint8(decoded[offset++]) + predictor) & 0xff);
                    left[c] = pixel[c];
                    curRow[x * 3 + c] = pixel[c];
                }
                setRectPixel(x, y, qRgb(pixel[2], pixel[1], pixel[0]));
            }
            prevRow.swap(curRow);
            std::fill(curRow.begin(), curRow.end(), 0);
        }
        return true;
    }

    for (int y = 0; y < int(rectH); ++y) {
        for (int x = 0; x < int(rectW); ++x) {
            setRectPixel(x, y, readCpixel(decoded.constData() + offset));
            offset += 3;
        }
    }
    return true;
}

bool RfbClientWorker::applyTightImageRect(const QByteArray& payload, const char* format)
{
    QImage image;
    if (!image.loadFromData(payload, format)) {
        fail(QString("Failed to decode Tight %1 image").arg(format));
        return false;
    }

    if (framebuffer.isNull())
        return true;
    QPainter painter(&framebuffer);
    painter.drawImage(QRect(rectX, rectY, rectW, rectH).intersected(framebuffer.rect()), image);
    return true;
}

bool RfbClientWorker::applyZrleRect(const QByteArray& payload)
{
    QByteArray decoded;
    if (!inflateZrle(payload, &decoded))
        return false;

    qsizetype offset = 0;
    for (int tileY = 0; tileY < int(rectH); tileY += 64) {
        const int tileH = qMin(64, int(rectH) - tileY);
        for (int tileX = 0; tileX < int(rectW); tileX += 64) {
            const int tileW = qMin(64, int(rectW) - tileX);
            if (offset >= decoded.size()) {
                fail("Truncated ZRLE tile stream");
                return false;
            }

            const quint8 subencoding = quint8(decoded[offset++]);
            const bool rle = (subencoding & 0x80) != 0;
            const int paletteSize = subencoding & 0x7f;
            QVector<QRgb> palette;
            if (paletteSize > 0) {
                if (paletteSize > 127 || decoded.size() < offset + paletteSize * 3) {
                    fail("Invalid ZRLE palette");
                    return false;
                }
                palette.reserve(paletteSize);
                for (int i = 0; i < paletteSize; ++i) {
                    palette.push_back(readCpixel(decoded.constData() + offset));
                    offset += 3;
                }
            }

            if (!rle && paletteSize == 0) {
                if (decoded.size() < offset + tileW * tileH * 3) {
                    fail("Truncated ZRLE raw tile");
                    return false;
                }
                for (int y = 0; y < tileH; ++y) {
                    for (int x = 0; x < tileW; ++x) {
                        setRectPixel(tileX + x, tileY + y, readCpixel(decoded.constData() + offset));
                        offset += 3;
                    }
                }
            } else if (!rle && paletteSize == 1) {
                for (int y = 0; y < tileH; ++y)
                    for (int x = 0; x < tileW; ++x)
                        setRectPixel(tileX + x, tileY + y, palette[0]);
            } else if (!rle) {
                int bitsPerPixel = paletteSize <= 2 ? 1 : (paletteSize <= 4 ? 2 : 4);
                int mask = (1 << bitsPerPixel) - 1;
                int stride = (tileW * bitsPerPixel + 7) / 8;
                if (paletteSize > 16 || decoded.size() < offset + stride * tileH) {
                    fail("Invalid ZRLE packed palette tile");
                    return false;
                }
                for (int y = 0; y < tileH; ++y) {
                    qsizetype row = offset + y * stride;
                    for (int x = 0; x < tileW; ++x) {
                        int bitOffset = x * bitsPerPixel;
                        quint8 byte = quint8(decoded[row + bitOffset / 8]);
                        int shift = 8 - bitsPerPixel - (bitOffset % 8);
                        int index = (byte >> shift) & mask;
                        if (index >= palette.size()) {
                            fail("ZRLE packed palette index out of range");
                            return false;
                        }
                        setRectPixel(tileX + x, tileY + y, palette[index]);
                    }
                }
                offset += stride * tileH;
            } else {
                int pixelIndex = 0;
                while (pixelIndex < tileW * tileH) {
                    QRgb color = Qt::black;
                    int runLength = 1;
                    bool hasExplicitRun = false;
                    if (paletteSize == 0) {
                        if (decoded.size() < offset + 3) {
                            fail("Truncated ZRLE RLE tile");
                            return false;
                        }
                        color = readCpixel(decoded.constData() + offset);
                        offset += 3;
                        hasExplicitRun = true;
                    } else {
                        if (offset >= decoded.size()) {
                            fail("Truncated ZRLE palette RLE tile");
                            return false;
                        }
                        quint8 indexByte = quint8(decoded[offset++]);
                        int index = indexByte & 0x7f;
                        if (index >= palette.size()) {
                            fail("ZRLE RLE palette index out of range");
                            return false;
                        }
                        color = palette[index];
                        hasExplicitRun = (indexByte & 0x80) != 0;
                    }

                    if (hasExplicitRun) {
                        runLength = 1;
                        quint8 b = 0;
                        do {
                            if (offset >= decoded.size()) {
                                fail("Truncated ZRLE run length");
                                return false;
                            }
                            b = quint8(decoded[offset++]);
                            runLength += b;
                        } while (b == 255);
                    }

                    for (int i = 0; i < runLength && pixelIndex < tileW * tileH; ++i, ++pixelIndex) {
                        int x = pixelIndex % tileW;
                        int y = pixelIndex / tileW;
                        setRectPixel(tileX + x, tileY + y, color);
                    }
                }
            }
        }
    }
    return true;
}

bool RfbClientWorker::inflateExpected(z_stream& stream, bool& ready, const QByteArray& input, int expectedBytes, QByteArray* output)
{
    output->clear();
    output->reserve(expectedBytes);
    if (!ready) {
        stream = z_stream{};
        if (inflateInit(&stream) != Z_OK) {
            fail("Failed to initialize Tight zlib stream");
            return false;
        }
        ready = true;
    }

    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.constData()));
    stream.avail_in = uInt(input.size());
    while (output->size() < expectedBytes) {
        const int beforeIn = stream.avail_in;
        const int oldSize = output->size();
        output->resize(expectedBytes);
        stream.next_out = reinterpret_cast<Bytef*>(output->data() + oldSize);
        stream.avail_out = uInt(expectedBytes - oldSize);
        int ret = inflate(&stream, Z_SYNC_FLUSH);
        output->resize(expectedBytes - int(stream.avail_out));
        if (ret != Z_OK && ret != Z_STREAM_END) {
            fail(QString("Tight zlib inflate failed: %1").arg(ret));
            return false;
        }
        if (stream.avail_in == beforeIn && output->size() == oldSize) {
            fail("Tight zlib stream made no progress");
            return false;
        }
    }
    return output->size() == expectedBytes;
}

bool RfbClientWorker::inflateZrle(const QByteArray& input, QByteArray* output)
{
    output->clear();
    if (!zrleStreamReady) {
        zrleStream = z_stream{};
        if (inflateInit(&zrleStream) != Z_OK) {
            fail("Failed to initialize ZRLE zlib stream");
            return false;
        }
        zrleStreamReady = true;
    }

    zrleStream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.constData()));
    zrleStream.avail_in = uInt(input.size());
    do {
        const qsizetype oldSize = output->size();
        output->resize(oldSize + 16384);
        zrleStream.next_out = reinterpret_cast<Bytef*>(output->data() + oldSize);
        zrleStream.avail_out = 16384;
        int ret = inflate(&zrleStream, Z_SYNC_FLUSH);
        const qsizetype produced = 16384 - zrleStream.avail_out;
        output->resize(oldSize + produced);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            fail(QString("ZRLE zlib inflate failed: %1").arg(ret));
            return false;
        }
        if (produced == 0 && zrleStream.avail_in > 0) {
            fail("ZRLE zlib stream made no progress");
            return false;
        }
    } while (zrleStream.avail_in > 0 || zrleStream.avail_out == 0);
    return true;
}

void RfbClientWorker::resetCompressionStreams()
{
    for (int i = 0; i < 4; ++i) {
        if (tightStreamsReady[i])
            inflateEnd(&tightStreams[i]);
        tightStreams[i] = z_stream{};
        tightStreamsReady[i] = false;
    }
    if (zrleStreamReady)
        inflateEnd(&zrleStream);
    zrleStream = z_stream{};
    zrleStreamReady = false;
}

void RfbClientWorker::resizeFramebuffer(int width, int height)
{
    if (width <= 0 || height <= 0)
        return;

    framebuffer = QImage(width, height, QImage::Format_RGB32);
    framebuffer.fill(Qt::black);
    Q_EMIT desktopResized(width, height);
}

void RfbClientWorker::fillRectRgb(QRgb color)
{
    for (int y = 0; y < int(rectH); ++y)
        for (int x = 0; x < int(rectW); ++x)
            setRectPixel(x, y, color);
}

void RfbClientWorker::setRectPixel(int x, int y, QRgb color)
{
    const int dstX = int(rectX) + x;
    const int dstY = int(rectY) + y;
    if (framebuffer.isNull() || dstX < 0 || dstY < 0 || dstX >= framebuffer.width() || dstY >= framebuffer.height())
        return;
    reinterpret_cast<QRgb*>(framebuffer.scanLine(dstY))[dstX] = color;
}

quint16 RfbClientWorker::readU16(const char* p)
{
    const auto* b = reinterpret_cast<const uchar*>(p);
    return quint16((quint16(b[0]) << 8) | quint16(b[1]));
}

quint32 RfbClientWorker::readU32(const char* p)
{
    const auto* b = reinterpret_cast<const uchar*>(p);
    return (quint32(b[0]) << 24) | (quint32(b[1]) << 16) | (quint32(b[2]) << 8) | quint32(b[3]);
}

qint32 RfbClientWorker::readS32(const char* p)
{
    return qint32(readU32(p));
}

QRgb RfbClientWorker::readCpixel(const char* p)
{
    const auto* b = reinterpret_cast<const uchar*>(p);
    return qRgb(b[2], b[1], b[0]);
}

bool RfbClientWorker::readCompactLength(const QByteArray& data, qsizetype offset, int* value, qsizetype* bytesRead)
{
    if (offset >= data.size())
        return false;

    const quint8 b0 = quint8(data[offset]);
    int length = b0 & 0x7f;
    if ((b0 & 0x80) == 0) {
        *value = length;
        *bytesRead = 1;
        return true;
    }

    if (offset + 1 >= data.size())
        return false;
    const quint8 b1 = quint8(data[offset + 1]);
    length |= int(b1 & 0x7f) << 7;
    if ((b1 & 0x80) == 0) {
        *value = length;
        *bytesRead = 2;
        return true;
    }

    if (offset + 2 >= data.size())
        return false;
    const quint8 b2 = quint8(data[offset + 2]);
    length |= int(b2) << 14;
    *value = length;
    *bytesRead = 3;
    return true;
}

void RfbClientWorker::appendU16(QByteArray& out, quint16 value)
{
    out.append(char((value >> 8) & 0xff));
    out.append(char(value & 0xff));
}

void RfbClientWorker::appendU32(QByteArray& out, quint32 value)
{
    out.append(char((value >> 24) & 0xff));
    out.append(char((value >> 16) & 0xff));
    out.append(char((value >> 8) & 0xff));
    out.append(char(value & 0xff));
}

void RfbClientWorker::appendS32(QByteArray& out, qint32 value)
{
    appendU32(out, quint32(value));
}
