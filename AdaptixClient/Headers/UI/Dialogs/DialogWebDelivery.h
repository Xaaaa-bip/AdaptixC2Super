#ifndef ADAPTIXCLIENT_DIALOGWEBDELIVERY_H
#define ADAPTIXCLIENT_DIALOGWEBDELIVERY_H

#include <main.h>
#include <Client/AuthProfile.h>
#include <UI/Widgets/AdaptixWidget.h>

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QVector>

class DialogWebDelivery : public QDialog {
    Q_OBJECT
public:
    explicit DialogWebDelivery(AuthProfile profile, 
                                const QVector<ListenerData>& listeners,
                                QWidget* parent = nullptr);
private Q_SLOTS:
    void onApplyConfig();
    void onRefreshStatus();
    void onCopyWinCmd();
    void onCopyLinuxCmd();
    void updateCommandPreview();

private:
    void createUI();
    void loadStatus();

    AuthProfile authProfile;
    QVector<ListenerData> availableListeners;

    // UI elements
    QLineEdit*   inputScriptRoute;
    QLineEdit*   inputPayloadRoute;
    QComboBox*   comboListener;
    QComboBox*   comboAgentName;
    QComboBox*   comboFormat;
    QLineEdit*   inputSleep;
    QSpinBox*    spinJitter;
    QSpinBox*    spinCacheTTL;
    QLineEdit*   inputHost;
    QLineEdit*   outputWinCmd;
    QLineEdit*   outputLinuxCmd;
    QLabel*      labelStatus;
    QPushButton* btnApply;
    QPushButton* btnRefresh;
};

#endif // ADAPTIXCLIENT_DIALOGWEBDELIVERY_H
