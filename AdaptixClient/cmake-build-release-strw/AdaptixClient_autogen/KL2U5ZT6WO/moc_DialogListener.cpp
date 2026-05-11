/****************************************************************************
** Meta object code from reading C++ file 'DialogListener.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../Headers/UI/Dialogs/DialogListener.h"
#include <QtGui/qtextcursor.h>
#include <QtNetwork/QSslError>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'DialogListener.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN14DialogListenerE_t {};
} // unnamed namespace

template <> constexpr inline auto DialogListener::qt_create_metaobjectdata<qt_meta_tag_ZN14DialogListenerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "DialogListener",
        "changeConfig",
        "",
        "fn",
        "changeType",
        "type",
        "onButtonCreate",
        "onButtonNewProfile",
        "onButtonLoad",
        "onButtonSave",
        "onProfileSelected",
        "handleProfileContextMenu",
        "QPoint",
        "pos",
        "onProfileRemove",
        "onProfileRename",
        "onListenerNameChanged",
        "text",
        "onProfileNameEdited",
        "onSaveProfileToggled",
        "checked"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'changeConfig'
        QtMocHelpers::SlotData<void(const QString &)>(1, 2, QMC::AccessProtected, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Slot 'changeType'
        QtMocHelpers::SlotData<void(const QString &)>(4, 2, QMC::AccessProtected, QMetaType::Void, {{
            { QMetaType::QString, 5 },
        }}),
        // Slot 'onButtonCreate'
        QtMocHelpers::SlotData<void()>(6, 2, QMC::AccessProtected, QMetaType::Void),
        // Slot 'onButtonNewProfile'
        QtMocHelpers::SlotData<void()>(7, 2, QMC::AccessProtected, QMetaType::Void),
        // Slot 'onButtonLoad'
        QtMocHelpers::SlotData<void()>(8, 2, QMC::AccessProtected, QMetaType::Void),
        // Slot 'onButtonSave'
        QtMocHelpers::SlotData<void()>(9, 2, QMC::AccessProtected, QMetaType::Void),
        // Slot 'onProfileSelected'
        QtMocHelpers::SlotData<void()>(10, 2, QMC::AccessProtected, QMetaType::Void),
        // Slot 'handleProfileContextMenu'
        QtMocHelpers::SlotData<void(const QPoint &)>(11, 2, QMC::AccessProtected, QMetaType::Void, {{
            { 0x80000000 | 12, 13 },
        }}),
        // Slot 'onProfileRemove'
        QtMocHelpers::SlotData<void()>(14, 2, QMC::AccessProtected, QMetaType::Void),
        // Slot 'onProfileRename'
        QtMocHelpers::SlotData<void()>(15, 2, QMC::AccessProtected, QMetaType::Void),
        // Slot 'onListenerNameChanged'
        QtMocHelpers::SlotData<void(const QString &)>(16, 2, QMC::AccessProtected, QMetaType::Void, {{
            { QMetaType::QString, 17 },
        }}),
        // Slot 'onProfileNameEdited'
        QtMocHelpers::SlotData<void(const QString &)>(18, 2, QMC::AccessProtected, QMetaType::Void, {{
            { QMetaType::QString, 17 },
        }}),
        // Slot 'onSaveProfileToggled'
        QtMocHelpers::SlotData<void(bool)>(19, 2, QMC::AccessProtected, QMetaType::Void, {{
            { QMetaType::Bool, 20 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<DialogListener, qt_meta_tag_ZN14DialogListenerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject DialogListener::staticMetaObject = { {
    QMetaObject::SuperData::link<QDialog::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14DialogListenerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14DialogListenerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN14DialogListenerE_t>.metaTypes,
    nullptr
} };

void DialogListener::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<DialogListener *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->changeConfig((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 1: _t->changeType((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 2: _t->onButtonCreate(); break;
        case 3: _t->onButtonNewProfile(); break;
        case 4: _t->onButtonLoad(); break;
        case 5: _t->onButtonSave(); break;
        case 6: _t->onProfileSelected(); break;
        case 7: _t->handleProfileContextMenu((*reinterpret_cast<std::add_pointer_t<QPoint>>(_a[1]))); break;
        case 8: _t->onProfileRemove(); break;
        case 9: _t->onProfileRename(); break;
        case 10: _t->onListenerNameChanged((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 11: _t->onProfileNameEdited((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 12: _t->onSaveProfileToggled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        default: ;
        }
    }
}

const QMetaObject *DialogListener::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *DialogListener::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14DialogListenerE_t>.strings))
        return static_cast<void*>(this);
    return QDialog::qt_metacast(_clname);
}

int DialogListener::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QDialog::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 13)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 13;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 13)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 13;
    }
    return _id;
}
QT_WARNING_POP
