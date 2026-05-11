/****************************************************************************
** Meta object code from reading C++ file 'AdaptixWidget.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../Headers/UI/Widgets/AdaptixWidget.h"
#include <QtGui/qtextcursor.h>
#include <QtNetwork/QSslError>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'AdaptixWidget.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.11.0. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN13AdaptixWidgetE_t {};
} // unnamed namespace

template <> constexpr inline auto AdaptixWidget::qt_create_metaobjectdata<qt_meta_tag_ZN13AdaptixWidgetE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AdaptixWidget",
        "SyncedSignal",
        "",
        "SyncedOnReloadSignal",
        "project",
        "LoadGlobalScriptSignal",
        "path",
        "UnloadGlobalScriptSignal",
        "eventNewAgent",
        "agentId",
        "eventFileBrowserDisks",
        "eventFileBrowserList",
        "eventFileBrowserUpload",
        "localFilename",
        "eventProcessBrowserList",
        "ChannelClose",
        "DataHandler",
        "data",
        "DataHandlerJson",
        "QJsonObject",
        "jsonObj",
        "OnWebSocketConnected",
        "OnSynced",
        "SetSessionsTableUI",
        "SetGraphUI",
        "SetTasksUI",
        "LoadAxConsoleUI",
        "LoadLogsUI",
        "LoadChatUI",
        "LoadListenersUI",
        "LoadTunnelsUI",
        "LoadDownloadsUI",
        "LoadScreenshotsUI",
        "LoadCredentialsUI",
        "LoadTargetsUI",
        "OnReconnect"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'SyncedSignal'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'SyncedOnReloadSignal'
        QtMocHelpers::SignalData<void(QString)>(3, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 4 },
        }}),
        // Signal 'LoadGlobalScriptSignal'
        QtMocHelpers::SignalData<void(QString)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 6 },
        }}),
        // Signal 'UnloadGlobalScriptSignal'
        QtMocHelpers::SignalData<void(QString)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 6 },
        }}),
        // Signal 'eventNewAgent'
        QtMocHelpers::SignalData<void(QString)>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 9 },
        }}),
        // Signal 'eventFileBrowserDisks'
        QtMocHelpers::SignalData<void(QString)>(10, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 9 },
        }}),
        // Signal 'eventFileBrowserList'
        QtMocHelpers::SignalData<void(QString, QString)>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 9 }, { QMetaType::QString, 6 },
        }}),
        // Signal 'eventFileBrowserUpload'
        QtMocHelpers::SignalData<void(QString, QString, QString)>(12, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 9 }, { QMetaType::QString, 6 }, { QMetaType::QString, 13 },
        }}),
        // Signal 'eventProcessBrowserList'
        QtMocHelpers::SignalData<void(QString)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 9 },
        }}),
        // Slot 'ChannelClose'
        QtMocHelpers::SlotData<void() const>(15, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'DataHandler'
        QtMocHelpers::SlotData<void(const QByteArray &)>(16, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QByteArray, 17 },
        }}),
        // Slot 'DataHandlerJson'
        QtMocHelpers::SlotData<void(const QJsonObject &)>(18, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 19, 20 },
        }}),
        // Slot 'OnWebSocketConnected'
        QtMocHelpers::SlotData<void()>(21, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'OnSynced'
        QtMocHelpers::SlotData<void()>(22, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'SetSessionsTableUI'
        QtMocHelpers::SlotData<void() const>(23, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'SetGraphUI'
        QtMocHelpers::SlotData<void() const>(24, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'SetTasksUI'
        QtMocHelpers::SlotData<void() const>(25, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'LoadAxConsoleUI'
        QtMocHelpers::SlotData<void() const>(26, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'LoadLogsUI'
        QtMocHelpers::SlotData<void() const>(27, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'LoadChatUI'
        QtMocHelpers::SlotData<void() const>(28, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'LoadListenersUI'
        QtMocHelpers::SlotData<void() const>(29, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'LoadTunnelsUI'
        QtMocHelpers::SlotData<void() const>(30, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'LoadDownloadsUI'
        QtMocHelpers::SlotData<void() const>(31, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'LoadScreenshotsUI'
        QtMocHelpers::SlotData<void() const>(32, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'LoadCredentialsUI'
        QtMocHelpers::SlotData<void() const>(33, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'LoadTargetsUI'
        QtMocHelpers::SlotData<void() const>(34, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'OnReconnect'
        QtMocHelpers::SlotData<void()>(35, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AdaptixWidget, qt_meta_tag_ZN13AdaptixWidgetE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AdaptixWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN13AdaptixWidgetE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN13AdaptixWidgetE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN13AdaptixWidgetE_t>.metaTypes,
    nullptr
} };

void AdaptixWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AdaptixWidget *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->SyncedSignal(); break;
        case 1: _t->SyncedOnReloadSignal((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 2: _t->LoadGlobalScriptSignal((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 3: _t->UnloadGlobalScriptSignal((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 4: _t->eventNewAgent((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 5: _t->eventFileBrowserDisks((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 6: _t->eventFileBrowserList((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 7: _t->eventFileBrowserUpload((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3]))); break;
        case 8: _t->eventProcessBrowserList((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 9: _t->ChannelClose(); break;
        case 10: _t->DataHandler((*reinterpret_cast<std::add_pointer_t<QByteArray>>(_a[1]))); break;
        case 11: _t->DataHandlerJson((*reinterpret_cast<std::add_pointer_t<QJsonObject>>(_a[1]))); break;
        case 12: _t->OnWebSocketConnected(); break;
        case 13: _t->OnSynced(); break;
        case 14: _t->SetSessionsTableUI(); break;
        case 15: _t->SetGraphUI(); break;
        case 16: _t->SetTasksUI(); break;
        case 17: _t->LoadAxConsoleUI(); break;
        case 18: _t->LoadLogsUI(); break;
        case 19: _t->LoadChatUI(); break;
        case 20: _t->LoadListenersUI(); break;
        case 21: _t->LoadTunnelsUI(); break;
        case 22: _t->LoadDownloadsUI(); break;
        case 23: _t->LoadScreenshotsUI(); break;
        case 24: _t->LoadCredentialsUI(); break;
        case 25: _t->LoadTargetsUI(); break;
        case 26: _t->OnReconnect(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (AdaptixWidget::*)()>(_a, &AdaptixWidget::SyncedSignal, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (AdaptixWidget::*)(QString )>(_a, &AdaptixWidget::SyncedOnReloadSignal, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (AdaptixWidget::*)(QString )>(_a, &AdaptixWidget::LoadGlobalScriptSignal, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (AdaptixWidget::*)(QString )>(_a, &AdaptixWidget::UnloadGlobalScriptSignal, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (AdaptixWidget::*)(QString )>(_a, &AdaptixWidget::eventNewAgent, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (AdaptixWidget::*)(QString )>(_a, &AdaptixWidget::eventFileBrowserDisks, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (AdaptixWidget::*)(QString , QString )>(_a, &AdaptixWidget::eventFileBrowserList, 6))
            return;
        if (QtMocHelpers::indexOfMethod<void (AdaptixWidget::*)(QString , QString , QString )>(_a, &AdaptixWidget::eventFileBrowserUpload, 7))
            return;
        if (QtMocHelpers::indexOfMethod<void (AdaptixWidget::*)(QString )>(_a, &AdaptixWidget::eventProcessBrowserList, 8))
            return;
    }
}

const QMetaObject *AdaptixWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AdaptixWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN13AdaptixWidgetE_t>.strings))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int AdaptixWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 27)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 27;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 27)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 27;
    }
    return _id;
}

// SIGNAL 0
void AdaptixWidget::SyncedSignal()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void AdaptixWidget::SyncedOnReloadSignal(QString _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void AdaptixWidget::LoadGlobalScriptSignal(QString _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void AdaptixWidget::UnloadGlobalScriptSignal(QString _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}

// SIGNAL 4
void AdaptixWidget::eventNewAgent(QString _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1);
}

// SIGNAL 5
void AdaptixWidget::eventFileBrowserDisks(QString _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1);
}

// SIGNAL 6
void AdaptixWidget::eventFileBrowserList(QString _t1, QString _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 6, nullptr, _t1, _t2);
}

// SIGNAL 7
void AdaptixWidget::eventFileBrowserUpload(QString _t1, QString _t2, QString _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 7, nullptr, _t1, _t2, _t3);
}

// SIGNAL 8
void AdaptixWidget::eventProcessBrowserList(QString _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 8, nullptr, _t1);
}
QT_WARNING_POP
