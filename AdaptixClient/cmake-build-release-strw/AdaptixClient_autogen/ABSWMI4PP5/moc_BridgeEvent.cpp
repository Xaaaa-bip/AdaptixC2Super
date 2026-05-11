/****************************************************************************
** Meta object code from reading C++ file 'BridgeEvent.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../Headers/Client/AxScript/BridgeEvent.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'BridgeEvent.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN11BridgeEventE_t {};
} // unnamed namespace

template <> constexpr inline auto BridgeEvent::qt_create_metaobjectdata<qt_meta_tag_ZN11BridgeEventE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "BridgeEvent",
        "scriptError",
        "",
        "msg",
        "on_filebrowser_disks",
        "QJSValue",
        "handler",
        "agents",
        "os",
        "listeners",
        "event_id",
        "on_filebrowser_list",
        "on_filebrowser_upload",
        "on_processbrowser_list",
        "on_new_agent",
        "on_disconnect",
        "on_ready",
        "on_interval",
        "delay",
        "on_timeout",
        "list",
        "remove"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'scriptError'
        QtMocHelpers::SignalData<void(const QString &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Slot 'on_filebrowser_disks'
        QtMocHelpers::SlotData<void(const QJSValue &, const QJSValue &, const QJSValue &, const QJSValue &, const QString &)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 5, 6 }, { 0x80000000 | 5, 7 }, { 0x80000000 | 5, 8 }, { 0x80000000 | 5, 9 },
            { QMetaType::QString, 10 },
        }}),
        // Slot 'on_filebrowser_disks'
        QtMocHelpers::SlotData<void(const QJSValue &, const QJSValue &, const QJSValue &, const QJSValue &)>(4, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 5, 6 }, { 0x80000000 | 5, 7 }, { 0x80000000 | 5, 8 }, { 0x80000000 | 5, 9 },
        }}),
        // Slot 'on_filebrowser_disks'
        QtMocHelpers::SlotData<void(const QJSValue &, const QJSValue &, const QJSValue &)>(4, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 5, 6 }, { 0x80000000 | 5, 7 }, { 0x80000000 | 5, 8 },
        }}),
        // Slot 'on_filebrowser_disks'
        QtMocHelpers::SlotData<void(const QJSValue &, const QJSValue &)>(4, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 5, 6 }, { 0x80000000 | 5, 7 },
        }}),
        // Slot 'on_filebrowser_list'
        QtMocHelpers::SlotData<void(const QJSValue &, const QJSValue &, const QJSValue &, const QJSValue &, const QString &)>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 5, 6 }, { 0x80000000 | 5, 7 }, { 0x80000000 | 5, 8 }, { 0x80000000 | 5, 9 },
            { QMetaType::QString, 10 },
        }}),
        // Slot 'on_filebrowser_list'
        QtMocHelpers::SlotData<void(const QJSValue &, const QJSValue &, const QJSValue &, const QJSValue &)>(11, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 5, 6 }, { 0x80000000 | 5, 7 }, { 0x80000000 | 5, 8 }, { 0x80000000 | 5, 9 },
        }}),
        // Slot 'on_filebrowser_list'
        QtMocHelpers::SlotData<void(const QJSValue &, const QJSValue &, const QJSValue &)>(11, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 5, 6 }, { 0x80000000 | 5, 7 }, { 0x80000000 | 5, 8 },
        }}),
        // Slot 'on_filebrowser_list'
        QtMocHelpers::SlotData<void(const QJSValue &, const QJSValue &)>(11, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 5, 6 }, { 0x80000000 | 5, 7 },
        }}),
        // Slot 'on_filebrowser_upload'
        QtMocHelpers::SlotData<void(const QJSValue &, const QJSValue &, const QJSValue &, const QJSValue &, const QString &)>(12, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 5, 6 }, { 0x80000000 | 5, 7 }, { 0x80000000 | 5, 8 }, { 0x80000000 | 5, 9 },
            { QMetaType::QString, 10 },
        }}),
        // Slot 'on_filebrowser_upload'
        QtMocHelpers::SlotData<void(const QJSValue &, const QJSValue &, const QJSValue &, const QJSValue &)>(12, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 5, 6 }, { 0x80000000 | 5, 7 }, { 0x80000000 | 5, 8 }, { 0x80000000 | 5, 9 },
        }}),
        // Slot 'on_filebrowser_upload'
        QtMocHelpers::SlotData<void(const QJSValue &, const QJSValue &, const QJSValue &)>(12, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 5, 6 }, { 0x80000000 | 5, 7 }, { 0x80000000 | 5, 8 },
        }}),
        // Slot 'on_filebrowser_upload'
        QtMocHelpers::SlotData<void(const QJSValue &, const QJSValue &)>(12, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 5, 6 }, { 0x80000000 | 5, 7 },
        }}),
        // Slot 'on_processbrowser_list'
        QtMocHelpers::SlotData<void(const QJSValue &, const QJSValue &, const QJSValue &, const QJSValue &, const QString &)>(13, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 5, 6 }, { 0x80000000 | 5, 7 }, { 0x80000000 | 5, 8 }, { 0x80000000 | 5, 9 },
            { QMetaType::QString, 10 },
        }}),
        // Slot 'on_processbrowser_list'
        QtMocHelpers::SlotData<void(const QJSValue &, const QJSValue &, const QJSValue &, const QJSValue &)>(13, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 5, 6 }, { 0x80000000 | 5, 7 }, { 0x80000000 | 5, 8 }, { 0x80000000 | 5, 9 },
        }}),
        // Slot 'on_processbrowser_list'
        QtMocHelpers::SlotData<void(const QJSValue &, const QJSValue &, const QJSValue &)>(13, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 5, 6 }, { 0x80000000 | 5, 7 }, { 0x80000000 | 5, 8 },
        }}),
        // Slot 'on_processbrowser_list'
        QtMocHelpers::SlotData<void(const QJSValue &, const QJSValue &)>(13, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 5, 6 }, { 0x80000000 | 5, 7 },
        }}),
        // Slot 'on_new_agent'
        QtMocHelpers::SlotData<void(const QJSValue &, const QJSValue &, const QJSValue &, const QJSValue &, const QString &)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 5, 6 }, { 0x80000000 | 5, 7 }, { 0x80000000 | 5, 8 }, { 0x80000000 | 5, 9 },
            { QMetaType::QString, 10 },
        }}),
        // Slot 'on_new_agent'
        QtMocHelpers::SlotData<void(const QJSValue &, const QJSValue &, const QJSValue &, const QJSValue &)>(14, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 5, 6 }, { 0x80000000 | 5, 7 }, { 0x80000000 | 5, 8 }, { 0x80000000 | 5, 9 },
        }}),
        // Slot 'on_new_agent'
        QtMocHelpers::SlotData<void(const QJSValue &, const QJSValue &, const QJSValue &)>(14, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 5, 6 }, { 0x80000000 | 5, 7 }, { 0x80000000 | 5, 8 },
        }}),
        // Slot 'on_new_agent'
        QtMocHelpers::SlotData<void(const QJSValue &, const QJSValue &)>(14, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 5, 6 }, { 0x80000000 | 5, 7 },
        }}),
        // Slot 'on_disconnect'
        QtMocHelpers::SlotData<void(const QJSValue &, const QString &)>(15, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 5, 6 }, { QMetaType::QString, 10 },
        }}),
        // Slot 'on_disconnect'
        QtMocHelpers::SlotData<void(const QJSValue &)>(15, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 5, 6 },
        }}),
        // Slot 'on_ready'
        QtMocHelpers::SlotData<void(const QJSValue &, const QString &)>(16, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 5, 6 }, { QMetaType::QString, 10 },
        }}),
        // Slot 'on_ready'
        QtMocHelpers::SlotData<void(const QJSValue &)>(16, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 5, 6 },
        }}),
        // Slot 'on_interval'
        QtMocHelpers::SlotData<QString(const QJSValue &, int, QString)>(17, 2, QMC::AccessPublic, QMetaType::QString, {{
            { 0x80000000 | 5, 6 }, { QMetaType::Int, 18 }, { QMetaType::QString, 10 },
        }}),
        // Slot 'on_interval'
        QtMocHelpers::SlotData<QString(const QJSValue &, int)>(17, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QString, {{
            { 0x80000000 | 5, 6 }, { QMetaType::Int, 18 },
        }}),
        // Slot 'on_timeout'
        QtMocHelpers::SlotData<QString(const QJSValue &, int, QString)>(19, 2, QMC::AccessPublic, QMetaType::QString, {{
            { 0x80000000 | 5, 6 }, { QMetaType::Int, 18 }, { QMetaType::QString, 10 },
        }}),
        // Slot 'on_timeout'
        QtMocHelpers::SlotData<QString(const QJSValue &, int)>(19, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QString, {{
            { 0x80000000 | 5, 6 }, { QMetaType::Int, 18 },
        }}),
        // Slot 'list'
        QtMocHelpers::SlotData<QJSValue()>(20, 2, QMC::AccessPublic, 0x80000000 | 5),
        // Slot 'remove'
        QtMocHelpers::SlotData<void(const QString &)>(21, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 10 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<BridgeEvent, qt_meta_tag_ZN11BridgeEventE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject BridgeEvent::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11BridgeEventE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11BridgeEventE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN11BridgeEventE_t>.metaTypes,
    nullptr
} };

void BridgeEvent::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<BridgeEvent *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->scriptError((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 1: _t->on_filebrowser_disks((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[5]))); break;
        case 2: _t->on_filebrowser_disks((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[4]))); break;
        case 3: _t->on_filebrowser_disks((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3]))); break;
        case 4: _t->on_filebrowser_disks((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2]))); break;
        case 5: _t->on_filebrowser_list((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[5]))); break;
        case 6: _t->on_filebrowser_list((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[4]))); break;
        case 7: _t->on_filebrowser_list((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3]))); break;
        case 8: _t->on_filebrowser_list((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2]))); break;
        case 9: _t->on_filebrowser_upload((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[5]))); break;
        case 10: _t->on_filebrowser_upload((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[4]))); break;
        case 11: _t->on_filebrowser_upload((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3]))); break;
        case 12: _t->on_filebrowser_upload((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2]))); break;
        case 13: _t->on_processbrowser_list((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[5]))); break;
        case 14: _t->on_processbrowser_list((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[4]))); break;
        case 15: _t->on_processbrowser_list((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3]))); break;
        case 16: _t->on_processbrowser_list((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2]))); break;
        case 17: _t->on_new_agent((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[5]))); break;
        case 18: _t->on_new_agent((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[4]))); break;
        case 19: _t->on_new_agent((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3]))); break;
        case 20: _t->on_new_agent((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2]))); break;
        case 21: _t->on_disconnect((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 22: _t->on_disconnect((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1]))); break;
        case 23: _t->on_ready((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 24: _t->on_ready((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1]))); break;
        case 25: { QString _r = _t->on_interval((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 26: { QString _r = _t->on_interval((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 27: { QString _r = _t->on_timeout((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 28: { QString _r = _t->on_timeout((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 29: { QJSValue _r = _t->list();
            if (_a[0]) *reinterpret_cast<QJSValue*>(_a[0]) = std::move(_r); }  break;
        case 30: _t->remove((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 1:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 3:
            case 2:
            case 1:
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 2:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 3:
            case 2:
            case 1:
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 3:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 2:
            case 1:
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 4:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 1:
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 5:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 3:
            case 2:
            case 1:
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 6:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 3:
            case 2:
            case 1:
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 7:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 2:
            case 1:
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 8:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 1:
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 9:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 3:
            case 2:
            case 1:
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 10:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 3:
            case 2:
            case 1:
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 11:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 2:
            case 1:
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 12:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 1:
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 13:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 3:
            case 2:
            case 1:
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 14:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 3:
            case 2:
            case 1:
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 15:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 2:
            case 1:
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 16:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 1:
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 17:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 3:
            case 2:
            case 1:
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 18:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 3:
            case 2:
            case 1:
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 19:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 2:
            case 1:
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 20:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 1:
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 21:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 22:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 23:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 24:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 25:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 26:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 27:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 28:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (BridgeEvent::*)(const QString & )>(_a, &BridgeEvent::scriptError, 0))
            return;
    }
}

const QMetaObject *BridgeEvent::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *BridgeEvent::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11BridgeEventE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int BridgeEvent::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 31)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 31;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 31)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 31;
    }
    return _id;
}

// SIGNAL 0
void BridgeEvent::scriptError(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}
QT_WARNING_POP
