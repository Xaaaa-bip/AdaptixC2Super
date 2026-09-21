/****************************************************************************
** Meta object code from reading C++ file 'BridgeMenu.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../Headers/Client/AxScript/BridgeMenu.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'BridgeMenu.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN10BridgeMenuE_t {};
} // unnamed namespace

template <> constexpr inline auto BridgeMenu::qt_create_metaobjectdata<qt_meta_tag_ZN10BridgeMenuE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "BridgeMenu",
        "create_action",
        "AxActionWrapper*",
        "",
        "text",
        "QJSValue",
        "handler",
        "create_menu",
        "AxMenuWrapper*",
        "title",
        "create_separator",
        "AxSeparatorWrapper*",
        "add_session_main",
        "AbstractAxMenuItem*",
        "item",
        "agents",
        "os",
        "listeners",
        "add_session_agent",
        "add_session_browser",
        "add_session_access",
        "add_filebrowser",
        "add_processbrowser",
        "add_downloads_running",
        "add_downloads_finished",
        "add_tasks",
        "add_tasks_job",
        "add_targets",
        "position",
        "add_credentials",
        "add_main",
        "add_main_projects",
        "add_main_axscript",
        "add_main_settings"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'create_action'
        QtMocHelpers::SlotData<AxActionWrapper *(const QString &, const QJSValue &)>(1, 3, QMC::AccessPublic, 0x80000000 | 2, {{
            { QMetaType::QString, 4 }, { 0x80000000 | 5, 6 },
        }}),
        // Slot 'create_menu'
        QtMocHelpers::SlotData<AxMenuWrapper *(const QString &)>(7, 3, QMC::AccessPublic, 0x80000000 | 8, {{
            { QMetaType::QString, 9 },
        }}),
        // Slot 'create_separator'
        QtMocHelpers::SlotData<AxSeparatorWrapper *()>(10, 3, QMC::AccessPublic, 0x80000000 | 11),
        // Slot 'add_session_main'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *, const QJSValue &, const QJSValue &, const QJSValue &)>(12, 3, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 13, 14 }, { 0x80000000 | 5, 15 }, { 0x80000000 | 5, 16 }, { 0x80000000 | 5, 17 },
        }}),
        // Slot 'add_session_main'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *, const QJSValue &, const QJSValue &)>(12, 3, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 13, 14 }, { 0x80000000 | 5, 15 }, { 0x80000000 | 5, 16 },
        }}),
        // Slot 'add_session_main'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *, const QJSValue &)>(12, 3, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 13, 14 }, { 0x80000000 | 5, 15 },
        }}),
        // Slot 'add_session_agent'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *, const QJSValue &, const QJSValue &, const QJSValue &)>(18, 3, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 13, 14 }, { 0x80000000 | 5, 15 }, { 0x80000000 | 5, 16 }, { 0x80000000 | 5, 17 },
        }}),
        // Slot 'add_session_agent'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *, const QJSValue &, const QJSValue &)>(18, 3, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 13, 14 }, { 0x80000000 | 5, 15 }, { 0x80000000 | 5, 16 },
        }}),
        // Slot 'add_session_agent'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *, const QJSValue &)>(18, 3, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 13, 14 }, { 0x80000000 | 5, 15 },
        }}),
        // Slot 'add_session_browser'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *, const QJSValue &, const QJSValue &, const QJSValue &)>(19, 3, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 13, 14 }, { 0x80000000 | 5, 15 }, { 0x80000000 | 5, 16 }, { 0x80000000 | 5, 17 },
        }}),
        // Slot 'add_session_browser'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *, const QJSValue &, const QJSValue &)>(19, 3, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 13, 14 }, { 0x80000000 | 5, 15 }, { 0x80000000 | 5, 16 },
        }}),
        // Slot 'add_session_browser'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *, const QJSValue &)>(19, 3, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 13, 14 }, { 0x80000000 | 5, 15 },
        }}),
        // Slot 'add_session_access'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *, const QJSValue &, const QJSValue &, const QJSValue &)>(20, 3, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 13, 14 }, { 0x80000000 | 5, 15 }, { 0x80000000 | 5, 16 }, { 0x80000000 | 5, 17 },
        }}),
        // Slot 'add_session_access'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *, const QJSValue &, const QJSValue &)>(20, 3, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 13, 14 }, { 0x80000000 | 5, 15 }, { 0x80000000 | 5, 16 },
        }}),
        // Slot 'add_session_access'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *, const QJSValue &)>(20, 3, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 13, 14 }, { 0x80000000 | 5, 15 },
        }}),
        // Slot 'add_filebrowser'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *, const QJSValue &, const QJSValue &, const QJSValue &)>(21, 3, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 13, 14 }, { 0x80000000 | 5, 15 }, { 0x80000000 | 5, 16 }, { 0x80000000 | 5, 17 },
        }}),
        // Slot 'add_filebrowser'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *, const QJSValue &, const QJSValue &)>(21, 3, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 13, 14 }, { 0x80000000 | 5, 15 }, { 0x80000000 | 5, 16 },
        }}),
        // Slot 'add_filebrowser'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *, const QJSValue &)>(21, 3, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 13, 14 }, { 0x80000000 | 5, 15 },
        }}),
        // Slot 'add_processbrowser'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *, const QJSValue &, const QJSValue &, const QJSValue &)>(22, 3, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 13, 14 }, { 0x80000000 | 5, 15 }, { 0x80000000 | 5, 16 }, { 0x80000000 | 5, 17 },
        }}),
        // Slot 'add_processbrowser'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *, const QJSValue &, const QJSValue &)>(22, 3, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 13, 14 }, { 0x80000000 | 5, 15 }, { 0x80000000 | 5, 16 },
        }}),
        // Slot 'add_processbrowser'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *, const QJSValue &)>(22, 3, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 13, 14 }, { 0x80000000 | 5, 15 },
        }}),
        // Slot 'add_downloads_running'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *, const QJSValue &, const QJSValue &, const QJSValue &)>(23, 3, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 13, 14 }, { 0x80000000 | 5, 15 }, { 0x80000000 | 5, 16 }, { 0x80000000 | 5, 17 },
        }}),
        // Slot 'add_downloads_running'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *, const QJSValue &, const QJSValue &)>(23, 3, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 13, 14 }, { 0x80000000 | 5, 15 }, { 0x80000000 | 5, 16 },
        }}),
        // Slot 'add_downloads_running'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *, const QJSValue &)>(23, 3, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 13, 14 }, { 0x80000000 | 5, 15 },
        }}),
        // Slot 'add_downloads_finished'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *, const QJSValue &, const QJSValue &, const QJSValue &)>(24, 3, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 13, 14 }, { 0x80000000 | 5, 15 }, { 0x80000000 | 5, 16 }, { 0x80000000 | 5, 17 },
        }}),
        // Slot 'add_downloads_finished'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *, const QJSValue &, const QJSValue &)>(24, 3, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 13, 14 }, { 0x80000000 | 5, 15 }, { 0x80000000 | 5, 16 },
        }}),
        // Slot 'add_downloads_finished'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *, const QJSValue &)>(24, 3, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 13, 14 }, { 0x80000000 | 5, 15 },
        }}),
        // Slot 'add_downloads_finished'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *)>(24, 3, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 13, 14 },
        }}),
        // Slot 'add_tasks'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *, const QJSValue &, const QJSValue &, const QJSValue &)>(25, 3, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 13, 14 }, { 0x80000000 | 5, 15 }, { 0x80000000 | 5, 16 }, { 0x80000000 | 5, 17 },
        }}),
        // Slot 'add_tasks'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *, const QJSValue &, const QJSValue &)>(25, 3, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 13, 14 }, { 0x80000000 | 5, 15 }, { 0x80000000 | 5, 16 },
        }}),
        // Slot 'add_tasks'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *, const QJSValue &)>(25, 3, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 13, 14 }, { 0x80000000 | 5, 15 },
        }}),
        // Slot 'add_tasks_job'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *, const QJSValue &, const QJSValue &, const QJSValue &)>(26, 3, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 13, 14 }, { 0x80000000 | 5, 15 }, { 0x80000000 | 5, 16 }, { 0x80000000 | 5, 17 },
        }}),
        // Slot 'add_tasks_job'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *, const QJSValue &, const QJSValue &)>(26, 3, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 13, 14 }, { 0x80000000 | 5, 15 }, { 0x80000000 | 5, 16 },
        }}),
        // Slot 'add_tasks_job'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *, const QJSValue &)>(26, 3, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 13, 14 }, { 0x80000000 | 5, 15 },
        }}),
        // Slot 'add_targets'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *, const QString &)>(27, 3, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 13, 14 }, { QMetaType::QString, 28 },
        }}),
        // Slot 'add_targets'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *)>(27, 3, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 13, 14 },
        }}),
        // Slot 'add_credentials'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *)>(29, 3, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 13, 14 },
        }}),
        // Slot 'add_main'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *)>(30, 3, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 13, 14 },
        }}),
        // Slot 'add_main_projects'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *)>(31, 3, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 13, 14 },
        }}),
        // Slot 'add_main_axscript'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *)>(32, 3, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 13, 14 },
        }}),
        // Slot 'add_main_settings'
        QtMocHelpers::SlotData<void(AbstractAxMenuItem *)>(33, 3, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 13, 14 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<BridgeMenu, qt_meta_tag_ZN10BridgeMenuE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject BridgeMenu::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10BridgeMenuE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10BridgeMenuE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN10BridgeMenuE_t>.metaTypes,
    nullptr
} };

void BridgeMenu::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<BridgeMenu *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: { AxActionWrapper* _r = _t->create_action((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])));
            if (_a[0]) *reinterpret_cast<AxActionWrapper**>(_a[0]) = std::move(_r); }  break;
        case 1: { AxMenuWrapper* _r = _t->create_menu((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<AxMenuWrapper**>(_a[0]) = std::move(_r); }  break;
        case 2: { AxSeparatorWrapper* _r = _t->create_separator();
            if (_a[0]) *reinterpret_cast<AxSeparatorWrapper**>(_a[0]) = std::move(_r); }  break;
        case 3: _t->add_session_main((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[4]))); break;
        case 4: _t->add_session_main((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3]))); break;
        case 5: _t->add_session_main((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2]))); break;
        case 6: _t->add_session_agent((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[4]))); break;
        case 7: _t->add_session_agent((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3]))); break;
        case 8: _t->add_session_agent((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2]))); break;
        case 9: _t->add_session_browser((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[4]))); break;
        case 10: _t->add_session_browser((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3]))); break;
        case 11: _t->add_session_browser((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2]))); break;
        case 12: _t->add_session_access((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[4]))); break;
        case 13: _t->add_session_access((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3]))); break;
        case 14: _t->add_session_access((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2]))); break;
        case 15: _t->add_filebrowser((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[4]))); break;
        case 16: _t->add_filebrowser((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3]))); break;
        case 17: _t->add_filebrowser((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2]))); break;
        case 18: _t->add_processbrowser((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[4]))); break;
        case 19: _t->add_processbrowser((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3]))); break;
        case 20: _t->add_processbrowser((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2]))); break;
        case 21: _t->add_downloads_running((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[4]))); break;
        case 22: _t->add_downloads_running((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3]))); break;
        case 23: _t->add_downloads_running((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2]))); break;
        case 24: _t->add_downloads_finished((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[4]))); break;
        case 25: _t->add_downloads_finished((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3]))); break;
        case 26: _t->add_downloads_finished((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2]))); break;
        case 27: _t->add_downloads_finished((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1]))); break;
        case 28: _t->add_tasks((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[4]))); break;
        case 29: _t->add_tasks((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3]))); break;
        case 30: _t->add_tasks((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2]))); break;
        case 31: _t->add_tasks_job((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[4]))); break;
        case 32: _t->add_tasks_job((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3]))); break;
        case 33: _t->add_tasks_job((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2]))); break;
        case 34: _t->add_targets((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 35: _t->add_targets((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1]))); break;
        case 36: _t->add_credentials((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1]))); break;
        case 37: _t->add_main((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1]))); break;
        case 38: _t->add_main_projects((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1]))); break;
        case 39: _t->add_main_axscript((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1]))); break;
        case 40: _t->add_main_settings((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 0:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 3:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 3:
            case 2:
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 4:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 2:
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 5:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 6:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 3:
            case 2:
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 7:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 2:
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 8:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 9:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 3:
            case 2:
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 10:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 2:
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 11:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 12:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 3:
            case 2:
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 13:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 2:
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 14:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 15:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 3:
            case 2:
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 16:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 2:
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 17:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 18:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 3:
            case 2:
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 19:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 2:
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 20:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 21:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 3:
            case 2:
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 22:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 2:
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 23:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 24:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 3:
            case 2:
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 25:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 2:
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 26:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 28:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 3:
            case 2:
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 29:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 2:
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 30:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 31:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 3:
            case 2:
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 32:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 2:
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 33:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        }
    }
}

const QMetaObject *BridgeMenu::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *BridgeMenu::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10BridgeMenuE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int BridgeMenu::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 41)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 41;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 41)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 41;
    }
    return _id;
}
QT_WARNING_POP
