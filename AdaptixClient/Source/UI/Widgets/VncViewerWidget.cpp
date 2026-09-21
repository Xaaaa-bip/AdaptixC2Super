#include <UI/Widgets/VncViewerWidget.h>
#include <Workers/RfbClientWorker.h>
#include <UI/Widgets/AdaptixWidget.h>
#include <UI/Widgets/DockWidgetRegister.h>
#include <Client/AuthProfile.h>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimer>
#include <QWheelEvent>

REGISTER_DOCK_WIDGET(VncViewerWidget, "VNC Viewer", false)

VncFramebufferWidget::VncFramebufferWidget(QWidget* parent) : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(320, 240);
}

void VncFramebufferWidget::setFrame(const QImage& image)
{
    frame = image;
    update();
}

void VncFramebufferWidget::setScaleToFit(bool enabled)
{
    scaleToFit = enabled;
    update();
}

void VncFramebufferWidget::setViewOnly(bool enabled)
{
    viewOnly = enabled;
}

void VncFramebufferWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.fillRect(rect(), QColor(8, 10, 14));

    if (frame.isNull()) {
        painter.setPen(QColor(150, 150, 150));
        painter.drawText(rect(), Qt::AlignCenter, "No VNC frame");
        return;
    }

    painter.setRenderHint(QPainter::SmoothPixmapTransform, scaleToFit);
    painter.drawImage(targetRect(), frame);
}

void VncFramebufferWidget::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);
    if (viewOnly)
        return;
    buttonMask |= maskForButton(event->button());
    sendPointerFor(event->pos());
}

void VncFramebufferWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (viewOnly)
        return;
    buttonMask &= ~maskForButton(event->button());
    sendPointerFor(event->pos());
}

void VncFramebufferWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (viewOnly)
        return;
    sendPointerFor(event->pos());
}

void VncFramebufferWidget::wheelEvent(QWheelEvent* event)
{
    if (viewOnly)
        return;

    QPoint p = imagePointFor(event->position().toPoint());
    if (p.x() < 0 || p.y() < 0)
        return;

    quint8 wheelMask = event->angleDelta().y() > 0 ? 0x08 : 0x10;
    Q_EMIT pointerEvent(buttonMask | wheelMask, quint16(p.x()), quint16(p.y()));
    Q_EMIT pointerEvent(buttonMask, quint16(p.x()), quint16(p.y()));
    event->accept();
}

void VncFramebufferWidget::keyPressEvent(QKeyEvent* event)
{
    if (viewOnly || event->isAutoRepeat())
        return;
    quint32 sym = keySymForEvent(event);
    if (sym != 0) {
        Q_EMIT keyEvent(true, sym);
        event->accept();
    }
}

void VncFramebufferWidget::keyReleaseEvent(QKeyEvent* event)
{
    if (viewOnly || event->isAutoRepeat())
        return;
    quint32 sym = keySymForEvent(event);
    if (sym != 0) {
        Q_EMIT keyEvent(false, sym);
        event->accept();
    }
}

QRect VncFramebufferWidget::targetRect() const
{
    if (frame.isNull())
        return rect();

    if (!scaleToFit)
        return QRect(0, 0, frame.width(), frame.height());

    QSize scaled = frame.size();
    scaled.scale(size(), Qt::KeepAspectRatio);
    const int x = (width() - scaled.width()) / 2;
    const int y = (height() - scaled.height()) / 2;
    return QRect(QPoint(x, y), scaled);
}

QPoint VncFramebufferWidget::imagePointFor(const QPoint& widgetPoint) const
{
    if (frame.isNull())
        return QPoint(-1, -1);

    QRect dst = targetRect();
    if (!dst.contains(widgetPoint))
        return QPoint(-1, -1);

    const double sx = double(frame.width()) / double(dst.width());
    const double sy = double(frame.height()) / double(dst.height());
    int x = int((widgetPoint.x() - dst.x()) * sx);
    int y = int((widgetPoint.y() - dst.y()) * sy);
    x = qBound(0, x, frame.width() - 1);
    y = qBound(0, y, frame.height() - 1);
    return QPoint(x, y);
}

void VncFramebufferWidget::sendPointerFor(const QPoint& widgetPoint)
{
    QPoint p = imagePointFor(widgetPoint);
    if (p.x() < 0 || p.y() < 0)
        return;
    Q_EMIT pointerEvent(buttonMask, quint16(p.x()), quint16(p.y()));
}

quint8 VncFramebufferWidget::maskForButton(Qt::MouseButton button)
{
    switch (button) {
        case Qt::LeftButton: return 0x01;
        case Qt::MiddleButton: return 0x02;
        case Qt::RightButton: return 0x04;
        default: return 0;
    }
}

quint32 VncFramebufferWidget::keySymForEvent(QKeyEvent* event)
{
    if (!event)
        return 0;

    if (!event->text().isEmpty()) {
        QChar ch = event->text().at(0);
        if (!ch.isNull() && ch.unicode() >= 0x20 && ch.unicode() <= 0x7e)
            return ch.unicode();
    }

    switch (event->key()) {
        case Qt::Key_Backspace: return 0xff08;
        case Qt::Key_Tab: return 0xff09;
        case Qt::Key_Return:
        case Qt::Key_Enter: return 0xff0d;
        case Qt::Key_Escape: return 0xff1b;
        case Qt::Key_Insert: return 0xff63;
        case Qt::Key_Delete: return 0xffff;
        case Qt::Key_Home: return 0xff50;
        case Qt::Key_End: return 0xff57;
        case Qt::Key_PageUp: return 0xff55;
        case Qt::Key_PageDown: return 0xff56;
        case Qt::Key_Left: return 0xff51;
        case Qt::Key_Up: return 0xff52;
        case Qt::Key_Right: return 0xff53;
        case Qt::Key_Down: return 0xff54;
        case Qt::Key_Shift: return 0xffe1;
        case Qt::Key_Control: return 0xffe3;
        case Qt::Key_Meta: return 0xffeb;
        case Qt::Key_Alt: return 0xffe9;
        case Qt::Key_CapsLock: return 0xffe5;
        case Qt::Key_F1: return 0xffbe;
        case Qt::Key_F2: return 0xffbf;
        case Qt::Key_F3: return 0xffc0;
        case Qt::Key_F4: return 0xffc1;
        case Qt::Key_F5: return 0xffc2;
        case Qt::Key_F6: return 0xffc3;
        case Qt::Key_F7: return 0xffc4;
        case Qt::Key_F8: return 0xffc5;
        case Qt::Key_F9: return 0xffc6;
        case Qt::Key_F10: return 0xffc7;
        case Qt::Key_F11: return 0xffc8;
        case Qt::Key_F12: return 0xffc9;
        default: break;
    }

    if (event->key() >= Qt::Key_Space && event->key() <= Qt::Key_AsciiTilde)
        return quint32(event->key());

    return 0;
}

VncViewerWidget::VncViewerWidget(AdaptixWidget* w, const QString& host, quint16 port, const QString& title, bool autoConnect)
    : DockTab(title, w->GetProfile()->GetProject(), ":/icons/picture")
{
    adaptixWidget = w;
    autoReconnect = autoConnect;
    createUI(host, port);
    dockWidget->setWidget(this);
    if (autoConnect)
        QTimer::singleShot(250, this, &VncViewerWidget::onConnect);
}

VncViewerWidget::~VncViewerWidget()
{
    cleanupWorker();
}

void VncViewerWidget::createUI(const QString& host, quint16 port)
{
    topWidget = new QWidget(this);
    hostInput = new QLineEdit(host, topWidget);
    hostInput->setMinimumWidth(180);

    portInput = new QSpinBox(topWidget);
    portInput->setRange(1, 65535);
    portInput->setValue(port);

    sharedCheck = new QCheckBox("Shared", topWidget);
    sharedCheck->setChecked(true);

    scaleCheck = new QCheckBox("Scale", topWidget);
    scaleCheck->setChecked(true);

    viewOnlyCheck = new QCheckBox("View only", topWidget);
    viewOnlyCheck->setChecked(false);

    connectButton = new QPushButton("Connect", topWidget);
    disconnectButton = new QPushButton("Disconnect", topWidget);
    disconnectButton->setEnabled(false);

    statusLabel = new QLabel("Idle", topWidget);

    topLayout = new QHBoxLayout(topWidget);
    topLayout->setContentsMargins(4, 4, 4, 4);
    topLayout->setSpacing(6);
    topLayout->addWidget(new QLabel("Host:", topWidget));
    topLayout->addWidget(hostInput);
    topLayout->addWidget(new QLabel("Port:", topWidget));
    topLayout->addWidget(portInput);
    topLayout->addWidget(sharedCheck);
    topLayout->addWidget(scaleCheck);
    topLayout->addWidget(viewOnlyCheck);
    topLayout->addWidget(connectButton);
    topLayout->addWidget(disconnectButton);
    topLayout->addStretch(1);
    topLayout->addWidget(statusLabel);

    framebuffer = new VncFramebufferWidget(this);
    framebuffer->setScaleToFit(scaleCheck->isChecked());
    framebuffer->setViewOnly(viewOnlyCheck->isChecked());

    mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(topWidget);
    mainLayout->addWidget(framebuffer, 1);
    setLayout(mainLayout);

    connect(connectButton, &QPushButton::clicked, this, &VncViewerWidget::onConnect);
    connect(disconnectButton, &QPushButton::clicked, this, &VncViewerWidget::onDisconnect);
    connect(scaleCheck, &QCheckBox::toggled, framebuffer, &VncFramebufferWidget::setScaleToFit);
    connect(viewOnlyCheck, &QCheckBox::toggled, framebuffer, &VncFramebufferWidget::setViewOnly);
}

void VncViewerWidget::onConnect()
{
    manualDisconnect = false;
    cleanupWorker();

    connectButton->setEnabled(false);
    disconnectButton->setEnabled(true);
    setStatus(reconnectAttempts > 0 ? QString("Waiting for VNC server, retry %1").arg(reconnectAttempts + 1) : "Starting");

    workerThread = new QThread(this);
    worker = new RfbClientWorker(hostInput->text().trimmed(), quint16(portInput->value()), sharedCheck->isChecked());
    worker->moveToThread(workerThread);

    connect(workerThread, &QThread::started, worker, &RfbClientWorker::start);
    connect(worker, &RfbClientWorker::finished, workerThread, &QThread::quit);
    connect(worker, &RfbClientWorker::finished, worker, &RfbClientWorker::deleteLater);
    connect(workerThread, &QThread::finished, workerThread, &QThread::deleteLater);
    connect(workerThread, &QThread::finished, this, [this]() {
        workerThread = nullptr;
        worker = nullptr;
        connectButton->setEnabled(true);
        disconnectButton->setEnabled(false);
        scheduleReconnect();
    });

    connect(worker, &RfbClientWorker::connected, this, [this]() {
        everConnected = true;
        autoReconnect = false;
        lastError.clear();
    }, Qt::QueuedConnection);
    connect(worker, &RfbClientWorker::statusChanged, this, &VncViewerWidget::setStatus, Qt::QueuedConnection);
    connect(worker, &RfbClientWorker::errorOccurred, this, [this](const QString& error) {
        lastError = error;
        setStatus(error);
    }, Qt::QueuedConnection);
    connect(worker, &RfbClientWorker::frameReady, this, &VncViewerWidget::onFrameReady, Qt::QueuedConnection);
    connect(framebuffer, &VncFramebufferWidget::pointerEvent, worker, &RfbClientWorker::sendPointerEvent, Qt::QueuedConnection);
    connect(framebuffer, &VncFramebufferWidget::keyEvent, worker, &RfbClientWorker::sendKeyEvent, Qt::QueuedConnection);

    workerThread->start();
}

void VncViewerWidget::onDisconnect()
{
    manualDisconnect = true;
    autoReconnect = false;
    cleanupWorker();
    setStatus("Disconnected");
}

void VncViewerWidget::onFrameReady(const QImage& image)
{
    framebuffer->setFrame(image);
}

void VncViewerWidget::setStatus(const QString& status)
{
    statusLabel->setText(status);
}

void VncViewerWidget::cleanupWorker()
{
    if (!worker && !workerThread)
        return;

    if (worker)
        QMetaObject::invokeMethod(worker, "stop", Qt::QueuedConnection);

    if (workerThread) {
        workerThread->quit();
        workerThread->wait(1500);
    }

    worker = nullptr;
    workerThread = nullptr;
    connectButton->setEnabled(true);
    disconnectButton->setEnabled(false);
}

void VncViewerWidget::scheduleReconnect()
{
    constexpr int MaxAutoReconnectAttempts = 90;
    if (!autoReconnect || manualDisconnect || everConnected || reconnectAttempts >= MaxAutoReconnectAttempts)
        return;

    ++reconnectAttempts;
    QString status = QString("Waiting for VNC server, retrying in 2s (%1/%2)").arg(reconnectAttempts).arg(MaxAutoReconnectAttempts);
    if (!lastError.isEmpty())
        status += QString(" - last error: %1").arg(lastError);
    setStatus(status);
    QTimer::singleShot(2000, this, [this]() {
        if (autoReconnect && !manualDisconnect && !everConnected)
            onConnect();
    });
}
