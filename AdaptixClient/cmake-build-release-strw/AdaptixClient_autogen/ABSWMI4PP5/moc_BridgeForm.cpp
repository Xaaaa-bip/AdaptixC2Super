/****************************************************************************
** Meta object code from reading C++ file 'BridgeForm.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../Headers/Client/AxScript/BridgeForm.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'BridgeForm.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN10BridgeFormE_t {};
} // unnamed namespace

template <> constexpr inline auto BridgeForm::qt_create_metaobjectdata<qt_meta_tag_ZN10BridgeFormE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "BridgeForm",
        "scriptError",
        "",
        "msg",
        "connect",
        "sender",
        "signal",
        "QJSValue",
        "handler",
        "create_vlayout",
        "create_hlayout",
        "create_gridlayout",
        "create_vline",
        "create_hline",
        "create_vspacer",
        "create_hspacer",
        "create_label",
        "text",
        "create_textline",
        "create_combo",
        "create_check",
        "label",
        "create_spin",
        "create_dateline",
        "format",
        "create_timeline",
        "create_button",
        "create_textmulti",
        "create_list",
        "create_table",
        "headers",
        "create_tabs",
        "create_groupbox",
        "title",
        "checkable",
        "create_hsplitter",
        "create_vsplitter",
        "create_scrollarea",
        "create_panel",
        "create_stack",
        "create_container",
        "create_dialog",
        "create_selector_file",
        "create_selector_credentials",
        "create_selector_agents",
        "create_selector_listeners",
        "create_selector_targets",
        "create_selector_downloads",
        "create_ext_dock",
        "id",
        "location",
        "create_ext_dialog"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'scriptError'
        QtMocHelpers::SignalData<void(const QString &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Slot 'connect'
        QtMocHelpers::SlotData<void(QObject *, const QString &, const QJSValue &)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QObjectStar, 5 }, { QMetaType::QString, 6 }, { 0x80000000 | 7, 8 },
        }}),
        // Slot 'create_vlayout'
        QtMocHelpers::SlotData<QObject *()>(9, 2, QMC::AccessPublic, QMetaType::QObjectStar),
        // Slot 'create_hlayout'
        QtMocHelpers::SlotData<QObject *()>(10, 2, QMC::AccessPublic, QMetaType::QObjectStar),
        // Slot 'create_gridlayout'
        QtMocHelpers::SlotData<QObject *()>(11, 2, QMC::AccessPublic, QMetaType::QObjectStar),
        // Slot 'create_vline'
        QtMocHelpers::SlotData<QObject *()>(12, 2, QMC::AccessPublic, QMetaType::QObjectStar),
        // Slot 'create_hline'
        QtMocHelpers::SlotData<QObject *()>(13, 2, QMC::AccessPublic, QMetaType::QObjectStar),
        // Slot 'create_vspacer'
        QtMocHelpers::SlotData<QObject *()>(14, 2, QMC::AccessPublic, QMetaType::QObjectStar),
        // Slot 'create_hspacer'
        QtMocHelpers::SlotData<QObject *()>(15, 2, QMC::AccessPublic, QMetaType::QObjectStar),
        // Slot 'create_label'
        QtMocHelpers::SlotData<QObject *(const QString &)>(16, 2, QMC::AccessPublic, QMetaType::QObjectStar, {{
            { QMetaType::QString, 17 },
        }}),
        // Slot 'create_label'
        QtMocHelpers::SlotData<QObject *()>(16, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QObjectStar),
        // Slot 'create_textline'
        QtMocHelpers::SlotData<QObject *(const QString &)>(18, 2, QMC::AccessPublic, QMetaType::QObjectStar, {{
            { QMetaType::QString, 17 },
        }}),
        // Slot 'create_textline'
        QtMocHelpers::SlotData<QObject *()>(18, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QObjectStar),
        // Slot 'create_combo'
        QtMocHelpers::SlotData<QObject *()>(19, 2, QMC::AccessPublic, QMetaType::QObjectStar),
        // Slot 'create_check'
        QtMocHelpers::SlotData<QObject *(const QString &)>(20, 2, QMC::AccessPublic, QMetaType::QObjectStar, {{
            { QMetaType::QString, 21 },
        }}),
        // Slot 'create_check'
        QtMocHelpers::SlotData<QObject *()>(20, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QObjectStar),
        // Slot 'create_spin'
        QtMocHelpers::SlotData<QObject *()>(22, 2, QMC::AccessPublic, QMetaType::QObjectStar),
        // Slot 'create_dateline'
        QtMocHelpers::SlotData<QObject *(const QString &)>(23, 2, QMC::AccessPublic, QMetaType::QObjectStar, {{
            { QMetaType::QString, 24 },
        }}),
        // Slot 'create_dateline'
        QtMocHelpers::SlotData<QObject *()>(23, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QObjectStar),
        // Slot 'create_timeline'
        QtMocHelpers::SlotData<QObject *(const QString &)>(25, 2, QMC::AccessPublic, QMetaType::QObjectStar, {{
            { QMetaType::QString, 24 },
        }}),
        // Slot 'create_timeline'
        QtMocHelpers::SlotData<QObject *()>(25, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QObjectStar),
        // Slot 'create_button'
        QtMocHelpers::SlotData<QObject *(const QString &)>(26, 2, QMC::AccessPublic, QMetaType::QObjectStar, {{
            { QMetaType::QString, 17 },
        }}),
        // Slot 'create_button'
        QtMocHelpers::SlotData<QObject *()>(26, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QObjectStar),
        // Slot 'create_textmulti'
        QtMocHelpers::SlotData<QObject *(const QString &)>(27, 2, QMC::AccessPublic, QMetaType::QObjectStar, {{
            { QMetaType::QString, 17 },
        }}),
        // Slot 'create_textmulti'
        QtMocHelpers::SlotData<QObject *()>(27, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QObjectStar),
        // Slot 'create_list'
        QtMocHelpers::SlotData<QObject *()>(28, 2, QMC::AccessPublic, QMetaType::QObjectStar),
        // Slot 'create_table'
        QtMocHelpers::SlotData<QObject *(const QJSValue &)>(29, 2, QMC::AccessPublic, QMetaType::QObjectStar, {{
            { 0x80000000 | 7, 30 },
        }}),
        // Slot 'create_tabs'
        QtMocHelpers::SlotData<QObject *()>(31, 2, QMC::AccessPublic, QMetaType::QObjectStar),
        // Slot 'create_groupbox'
        QtMocHelpers::SlotData<QObject *(const QString &, const bool)>(32, 2, QMC::AccessPublic, QMetaType::QObjectStar, {{
            { QMetaType::QString, 33 }, { QMetaType::Bool, 34 },
        }}),
        // Slot 'create_groupbox'
        QtMocHelpers::SlotData<QObject *(const QString &)>(32, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QObjectStar, {{
            { QMetaType::QString, 33 },
        }}),
        // Slot 'create_groupbox'
        QtMocHelpers::SlotData<QObject *()>(32, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QObjectStar),
        // Slot 'create_hsplitter'
        QtMocHelpers::SlotData<QObject *()>(35, 2, QMC::AccessPublic, QMetaType::QObjectStar),
        // Slot 'create_vsplitter'
        QtMocHelpers::SlotData<QObject *()>(36, 2, QMC::AccessPublic, QMetaType::QObjectStar),
        // Slot 'create_scrollarea'
        QtMocHelpers::SlotData<QObject *()>(37, 2, QMC::AccessPublic, QMetaType::QObjectStar),
        // Slot 'create_panel'
        QtMocHelpers::SlotData<QObject *()>(38, 2, QMC::AccessPublic, QMetaType::QObjectStar),
        // Slot 'create_stack'
        QtMocHelpers::SlotData<QObject *()>(39, 2, QMC::AccessPublic, QMetaType::QObjectStar),
        // Slot 'create_container'
        QtMocHelpers::SlotData<QObject *()>(40, 2, QMC::AccessPublic, QMetaType::QObjectStar),
        // Slot 'create_dialog'
        QtMocHelpers::SlotData<QObject *(const QString &) const>(41, 2, QMC::AccessPublic, QMetaType::QObjectStar, {{
            { QMetaType::QString, 33 },
        }}),
        // Slot 'create_selector_file'
        QtMocHelpers::SlotData<QObject *()>(42, 2, QMC::AccessPublic, QMetaType::QObjectStar),
        // Slot 'create_selector_credentials'
        QtMocHelpers::SlotData<QObject *(const QJSValue &) const>(43, 2, QMC::AccessPublic, QMetaType::QObjectStar, {{
            { 0x80000000 | 7, 30 },
        }}),
        // Slot 'create_selector_agents'
        QtMocHelpers::SlotData<QObject *(const QJSValue &) const>(44, 2, QMC::AccessPublic, QMetaType::QObjectStar, {{
            { 0x80000000 | 7, 30 },
        }}),
        // Slot 'create_selector_listeners'
        QtMocHelpers::SlotData<QObject *(const QJSValue &) const>(45, 2, QMC::AccessPublic, QMetaType::QObjectStar, {{
            { 0x80000000 | 7, 30 },
        }}),
        // Slot 'create_selector_targets'
        QtMocHelpers::SlotData<QObject *(const QJSValue &) const>(46, 2, QMC::AccessPublic, QMetaType::QObjectStar, {{
            { 0x80000000 | 7, 30 },
        }}),
        // Slot 'create_selector_downloads'
        QtMocHelpers::SlotData<QObject *(const QJSValue &) const>(47, 2, QMC::AccessPublic, QMetaType::QObjectStar, {{
            { 0x80000000 | 7, 30 },
        }}),
        // Slot 'create_ext_dock'
        QtMocHelpers::SlotData<QObject *(const QString &, const QString &, const QString &)>(48, 2, QMC::AccessPublic, QMetaType::QObjectStar, {{
            { QMetaType::QString, 49 }, { QMetaType::QString, 33 }, { QMetaType::QString, 50 },
        }}),
        // Slot 'create_ext_dock'
        QtMocHelpers::SlotData<QObject *(const QString &, const QString &)>(48, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QObjectStar, {{
            { QMetaType::QString, 49 }, { QMetaType::QString, 33 },
        }}),
        // Slot 'create_ext_dialog'
        QtMocHelpers::SlotData<QObject *(const QString &)>(51, 2, QMC::AccessPublic, QMetaType::QObjectStar, {{
            { QMetaType::QString, 33 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<BridgeForm, qt_meta_tag_ZN10BridgeFormE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject BridgeForm::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10BridgeFormE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10BridgeFormE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN10BridgeFormE_t>.metaTypes,
    nullptr
} };

void BridgeForm::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<BridgeForm *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->scriptError((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 1: _t->connect((*reinterpret_cast<std::add_pointer_t<QObject*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3]))); break;
        case 2: { QObject* _r = _t->create_vlayout();
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 3: { QObject* _r = _t->create_hlayout();
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 4: { QObject* _r = _t->create_gridlayout();
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 5: { QObject* _r = _t->create_vline();
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 6: { QObject* _r = _t->create_hline();
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 7: { QObject* _r = _t->create_vspacer();
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 8: { QObject* _r = _t->create_hspacer();
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 9: { QObject* _r = _t->create_label((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 10: { QObject* _r = _t->create_label();
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 11: { QObject* _r = _t->create_textline((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 12: { QObject* _r = _t->create_textline();
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 13: { QObject* _r = _t->create_combo();
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 14: { QObject* _r = _t->create_check((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 15: { QObject* _r = _t->create_check();
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 16: { QObject* _r = _t->create_spin();
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 17: { QObject* _r = _t->create_dateline((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 18: { QObject* _r = _t->create_dateline();
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 19: { QObject* _r = _t->create_timeline((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 20: { QObject* _r = _t->create_timeline();
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 21: { QObject* _r = _t->create_button((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 22: { QObject* _r = _t->create_button();
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 23: { QObject* _r = _t->create_textmulti((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 24: { QObject* _r = _t->create_textmulti();
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 25: { QObject* _r = _t->create_list();
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 26: { QObject* _r = _t->create_table((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 27: { QObject* _r = _t->create_tabs();
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 28: { QObject* _r = _t->create_groupbox((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 29: { QObject* _r = _t->create_groupbox((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 30: { QObject* _r = _t->create_groupbox();
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 31: { QObject* _r = _t->create_hsplitter();
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 32: { QObject* _r = _t->create_vsplitter();
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 33: { QObject* _r = _t->create_scrollarea();
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 34: { QObject* _r = _t->create_panel();
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 35: { QObject* _r = _t->create_stack();
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 36: { QObject* _r = _t->create_container();
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 37: { QObject* _r = _t->create_dialog((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 38: { QObject* _r = _t->create_selector_file();
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 39: { QObject* _r = _t->create_selector_credentials((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 40: { QObject* _r = _t->create_selector_agents((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 41: { QObject* _r = _t->create_selector_listeners((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 42: { QObject* _r = _t->create_selector_targets((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 43: { QObject* _r = _t->create_selector_downloads((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 44: { QObject* _r = _t->create_ext_dock((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])));
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 45: { QObject* _r = _t->create_ext_dock((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 46: { QObject* _r = _t->create_ext_dialog((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        default: ;
        }
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 1:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 2:
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
        case 39:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 40:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 41:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 42:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 43:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (BridgeForm::*)(const QString & )>(_a, &BridgeForm::scriptError, 0))
            return;
    }
}

const QMetaObject *BridgeForm::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *BridgeForm::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10BridgeFormE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int BridgeForm::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 47)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 47;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 47)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 47;
    }
    return _id;
}

// SIGNAL 0
void BridgeForm::scriptError(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}
namespace {
struct qt_meta_tag_ZN11SignalProxyE_t {};
} // unnamed namespace

template <> constexpr inline auto SignalProxy::qt_create_metaobjectdata<qt_meta_tag_ZN11SignalProxyE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "SignalProxy",
        "call",
        "",
        "callWithArg",
        "arg",
        "callWithArgs",
        "arg1",
        "arg2"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'call'
        QtMocHelpers::SlotData<void() const>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'callWithArg'
        QtMocHelpers::SlotData<void(const bool &) const>(3, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 4 },
        }}),
        // Slot 'callWithArg'
        QtMocHelpers::SlotData<void(const int &) const>(3, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 4 },
        }}),
        // Slot 'callWithArg'
        QtMocHelpers::SlotData<void(const QString &) const>(3, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 4 },
        }}),
        // Slot 'callWithArgs'
        QtMocHelpers::SlotData<void(const int &, const int &) const>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 6 }, { QMetaType::Int, 7 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<SignalProxy, qt_meta_tag_ZN11SignalProxyE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject SignalProxy::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11SignalProxyE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11SignalProxyE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN11SignalProxyE_t>.metaTypes,
    nullptr
} };

void SignalProxy::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<SignalProxy *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->call(); break;
        case 1: _t->callWithArg((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 2: _t->callWithArg((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 3: _t->callWithArg((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 4: _t->callWithArgs((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        default: ;
        }
    }
}

const QMetaObject *SignalProxy::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *SignalProxy::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11SignalProxyE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int SignalProxy::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 5)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 5)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 5;
    }
    return _id;
}
QT_WARNING_POP
