/****************************************************************************
** Meta object code from reading C++ file 'Vt102Emulation.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../Libs/Konsole/Vt102Emulation.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'Vt102Emulation.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN14Vt102EmulationE_t {};
} // unnamed namespace

template <> constexpr inline auto Vt102Emulation::qt_create_metaobjectdata<qt_meta_tag_ZN14Vt102EmulationE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "Vt102Emulation",
        "changeBackgroundColorRequest",
        "",
        "QColor",
        "openUrlRequest",
        "url",
        "sendString",
        "const char*",
        "length",
        "sendText",
        "text",
        "sendKeyEvent",
        "QKeyEvent*",
        "fromPaste",
        "sendMouseEvent",
        "buttons",
        "column",
        "line",
        "eventType",
        "focusLost",
        "focusGained",
        "updateTitle"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'changeBackgroundColorRequest'
        QtMocHelpers::SignalData<void(const QColor &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 3, 2 },
        }}),
        // Signal 'openUrlRequest'
        QtMocHelpers::SignalData<void(const QString &)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 5 },
        }}),
        // Slot 'sendString'
        QtMocHelpers::SlotData<void(const char *, int)>(6, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 7, 2 }, { QMetaType::Int, 8 },
        }}),
        // Slot 'sendString'
        QtMocHelpers::SlotData<void(const char *)>(6, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 7, 2 },
        }}),
        // Slot 'sendText'
        QtMocHelpers::SlotData<void(const QString &)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 10 },
        }}),
        // Slot 'sendKeyEvent'
        QtMocHelpers::SlotData<void(QKeyEvent *, bool)>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 12, 2 }, { QMetaType::Bool, 13 },
        }}),
        // Slot 'sendMouseEvent'
        QtMocHelpers::SlotData<void(int, int, int, int)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 15 }, { QMetaType::Int, 16 }, { QMetaType::Int, 17 }, { QMetaType::Int, 18 },
        }}),
        // Slot 'focusLost'
        QtMocHelpers::SlotData<void()>(19, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'focusGained'
        QtMocHelpers::SlotData<void()>(20, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'updateTitle'
        QtMocHelpers::SlotData<void()>(21, 2, QMC::AccessPrivate, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<Vt102Emulation, qt_meta_tag_ZN14Vt102EmulationE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject Vt102Emulation::staticMetaObject = { {
    QMetaObject::SuperData::link<Emulation::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14Vt102EmulationE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14Vt102EmulationE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN14Vt102EmulationE_t>.metaTypes,
    nullptr
} };

void Vt102Emulation::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<Vt102Emulation *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->changeBackgroundColorRequest((*reinterpret_cast<std::add_pointer_t<QColor>>(_a[1]))); break;
        case 1: _t->openUrlRequest((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 2: _t->sendString((*reinterpret_cast<std::add_pointer_t<const char*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 3: _t->sendString((*reinterpret_cast<std::add_pointer_t<const char*>>(_a[1]))); break;
        case 4: _t->sendText((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 5: _t->sendKeyEvent((*reinterpret_cast<std::add_pointer_t<QKeyEvent*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2]))); break;
        case 6: _t->sendMouseEvent((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[4]))); break;
        case 7: _t->focusLost(); break;
        case 8: _t->focusGained(); break;
        case 9: _t->updateTitle(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (Vt102Emulation::*)(const QColor & )>(_a, &Vt102Emulation::changeBackgroundColorRequest, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (Vt102Emulation::*)(const QString & )>(_a, &Vt102Emulation::openUrlRequest, 1))
            return;
    }
}

const QMetaObject *Vt102Emulation::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Vt102Emulation::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14Vt102EmulationE_t>.strings))
        return static_cast<void*>(this);
    return Emulation::qt_metacast(_clname);
}

int Vt102Emulation::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = Emulation::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 10)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 10;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 10)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 10;
    }
    return _id;
}

// SIGNAL 0
void Vt102Emulation::changeBackgroundColorRequest(const QColor & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void Vt102Emulation::openUrlRequest(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}
QT_WARNING_POP
