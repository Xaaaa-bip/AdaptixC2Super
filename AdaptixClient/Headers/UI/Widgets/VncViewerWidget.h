#ifndef ADAPTIXCLIENT_VNCVIEWERWIDGET_H
#define ADAPTIXCLIENT_VNCVIEWERWIDGET_H

#include <main.h>
#include <UI/Widgets/AbstractDock.h>

class RfbClientWorker;
class AdaptixWidget;

class VncFramebufferWidget : public QWidget
{
Q_OBJECT

public:
    explicit VncFramebufferWidget(QWidget* parent = nullptr);
    void setFrame(const QImage& image);
    void setScaleToFit(bool enabled);
    void setViewOnly(bool enabled);

Q_SIGNALS:
    void pointerEvent(quint8 buttonMask, quint16 x, quint16 y);
    void keyEvent(bool down, quint32 keySym);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    QImage frame;
    bool scaleToFit = true;
    bool viewOnly = false;
    quint8 buttonMask = 0;

    QRect targetRect() const;
    QPoint imagePointFor(const QPoint& widgetPoint) const;
    void sendPointerFor(const QPoint& widgetPoint);
    static quint8 maskForButton(Qt::MouseButton button);
    static quint32 keySymForEvent(QKeyEvent* event);
};

class VncViewerWidget : public DockTab
{
Q_OBJECT

public:
    VncViewerWidget(AdaptixWidget* w, const QString& host, quint16 port, const QString& title, bool autoConnect = false);
    ~VncViewerWidget() override;

private Q_SLOTS:
    void onConnect();
    void onDisconnect();
    void onFrameReady(const QImage& image);
    void setStatus(const QString& status);

private:
    AdaptixWidget* adaptixWidget = nullptr;
    QWidget* topWidget = nullptr;
    QHBoxLayout* topLayout = nullptr;
    QLineEdit* hostInput = nullptr;
    QSpinBox* portInput = nullptr;
    QCheckBox* sharedCheck = nullptr;
    QCheckBox* scaleCheck = nullptr;
    QCheckBox* viewOnlyCheck = nullptr;
    QPushButton* connectButton = nullptr;
    QPushButton* disconnectButton = nullptr;
    QLabel* statusLabel = nullptr;
    VncFramebufferWidget* framebuffer = nullptr;
    QVBoxLayout* mainLayout = nullptr;

    QThread* workerThread = nullptr;
    RfbClientWorker* worker = nullptr;
    bool autoReconnect = false;
    bool manualDisconnect = false;
    bool everConnected = false;
    int reconnectAttempts = 0;
    QString lastError;

    void createUI(const QString& host, quint16 port);
    void cleanupWorker();
    void scheduleReconnect();
};

#endif
