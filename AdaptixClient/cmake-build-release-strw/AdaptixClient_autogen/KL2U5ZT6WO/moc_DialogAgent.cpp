/****************************************************************************
** Meta object code from reading C++ file 'DialogAgent.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../Headers/UI/Dialogs/DialogAgent.h"
#include <QtGui/qtextcursor.h>
#include <QtNetwork/QSslError>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'DialogAgent.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN11DialogAgentE_t {};
} // unnamed namespace

template <> constexpr inline auto DialogAgent::qt_create_metaobjectdata<qt_meta_tag_ZN11DialogAgentE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "DialogAgent",
        "onButtonLoad",
        "",
        "onButtonSave",
        "changeConfig",
        "agentName",
        "onButtonNewProfile",
        "onProfileSelected",
        "handleProfileContextMenu",
        "QPoint",
        "pos",
        "onProfileRemove",
        "onProfileRename",
        "onProfileNameEdited",
        "text",
        "onSaveProfileToggled",
        "checked",
        "onButtonBuild",
        "onBuildConnected",
        "onBuildMessage",
        "msg",
        "onBuildFinished",
        "stopBuild",
        "onListenerSelectionChanged",
        "const QListWidgetItem*",
        "item",
        "onMoveListenerUp",
        "onMoveListenerDown",
        "showListenerPopup",
        "updateListenerDisplay"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'onButtonLoad'
        QtMocHelpers::SlotData<void()>(1, 2, QMC::AccessProtected, QMetaType::Void),
        // Slot 'onButtonSave'
        QtMocHelpers::SlotData<void()>(3, 2, QMC::AccessProtected, QMetaType::Void),
        // Slot 'changeConfig'
        QtMocHelpers::SlotData<void(const QString &)>(4, 2, QMC::AccessProtected, QMetaType::Void, {{
            { QMetaType::QString, 5 },
        }}),
        // Slot 'onButtonNewProfile'
        QtMocHelpers::SlotData<void()>(6, 2, QMC::AccessProtected, QMetaType::Void),
        // Slot 'onProfileSelected'
        QtMocHelpers::SlotData<void()>(7, 2, QMC::AccessProtected, QMetaType::Void),
        // Slot 'handleProfileContextMenu'
        QtMocHelpers::SlotData<void(const QPoint &)>(8, 2, QMC::AccessProtected, QMetaType::Void, {{
            { 0x80000000 | 9, 10 },
        }}),
        // Slot 'onProfileRemove'
        QtMocHelpers::SlotData<void()>(11, 2, QMC::AccessProtected, QMetaType::Void),
        // Slot 'onProfileRename'
        QtMocHelpers::SlotData<void()>(12, 2, QMC::AccessProtected, QMetaType::Void),
        // Slot 'onProfileNameEdited'
        QtMocHelpers::SlotData<void(const QString &)>(13, 2, QMC::AccessProtected, QMetaType::Void, {{
            { QMetaType::QString, 14 },
        }}),
        // Slot 'onSaveProfileToggled'
        QtMocHelpers::SlotData<void(bool)>(15, 2, QMC::AccessProtected, QMetaType::Void, {{
            { QMetaType::Bool, 16 },
        }}),
        // Slot 'onButtonBuild'
        QtMocHelpers::SlotData<void()>(17, 2, QMC::AccessProtected, QMetaType::Void),
        // Slot 'onBuildConnected'
        QtMocHelpers::SlotData<void()>(18, 2, QMC::AccessProtected, QMetaType::Void),
        // Slot 'onBuildMessage'
        QtMocHelpers::SlotData<void(const QString &)>(19, 2, QMC::AccessProtected, QMetaType::Void, {{
            { QMetaType::QString, 20 },
        }}),
        // Slot 'onBuildFinished'
        QtMocHelpers::SlotData<void()>(21, 2, QMC::AccessProtected, QMetaType::Void),
        // Slot 'stopBuild'
        QtMocHelpers::SlotData<void()>(22, 2, QMC::AccessProtected, QMetaType::Void),
        // Slot 'onListenerSelectionChanged'
        QtMocHelpers::SlotData<void(const QListWidgetItem *)>(23, 2, QMC::AccessProtected, QMetaType::Void, {{
            { 0x80000000 | 24, 25 },
        }}),
        // Slot 'onMoveListenerUp'
        QtMocHelpers::SlotData<void()>(26, 2, QMC::AccessProtected, QMetaType::Void),
        // Slot 'onMoveListenerDown'
        QtMocHelpers::SlotData<void()>(27, 2, QMC::AccessProtected, QMetaType::Void),
        // Slot 'showListenerPopup'
        QtMocHelpers::SlotData<void()>(28, 2, QMC::AccessProtected, QMetaType::Void),
        // Slot 'updateListenerDisplay'
        QtMocHelpers::SlotData<void()>(29, 2, QMC::AccessProtected, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<DialogAgent, qt_meta_tag_ZN11DialogAgentE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject DialogAgent::staticMetaObject = { {
    QMetaObject::SuperData::link<QDialog::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11DialogAgentE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11DialogAgentE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN11DialogAgentE_t>.metaTypes,
    nullptr
} };

void DialogAgent::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<DialogAgent *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->onButtonLoad(); break;
        case 1: _t->onButtonSave(); break;
        case 2: _t->changeConfig((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 3: _t->onButtonNewProfile(); break;
        case 4: _t->onProfileSelected(); break;
        case 5: _t->handleProfileContextMenu((*reinterpret_cast<std::add_pointer_t<QPoint>>(_a[1]))); break;
        case 6: _t->onProfileRemove(); break;
        case 7: _t->onProfileRename(); break;
        case 8: _t->onProfileNameEdited((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 9: _t->onSaveProfileToggled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 10: _t->onButtonBuild(); break;
        case 11: _t->onBuildConnected(); break;
        case 12: _t->onBuildMessage((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 13: _t->onBuildFinished(); break;
        case 14: _t->stopBuild(); break;
        case 15: _t->onListenerSelectionChanged((*reinterpret_cast<std::add_pointer_t<const QListWidgetItem*>>(_a[1]))); break;
        case 16: _t->onMoveListenerUp(); break;
        case 17: _t->onMoveListenerDown(); break;
        case 18: _t->showListenerPopup(); break;
        case 19: _t->updateListenerDisplay(); break;
        default: ;
        }
    }
}

const QMetaObject *DialogAgent::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *DialogAgent::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11DialogAgentE_t>.strings))
        return static_cast<void*>(this);
    return QDialog::qt_metacast(_clname);
}

int DialogAgent::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QDialog::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 20)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 20;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 20)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 20;
    }
    return _id;
}
QT_WARNING_POP
