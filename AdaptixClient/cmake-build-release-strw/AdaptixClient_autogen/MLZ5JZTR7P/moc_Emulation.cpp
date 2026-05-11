/****************************************************************************
** Meta object code from reading C++ file 'Emulation.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../Libs/Konsole/Emulation.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'Emulation.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN9EmulationE_t {};
} // unnamed namespace

template <> constexpr inline auto Emulation::qt_create_metaobjectdata<qt_meta_tag_ZN9EmulationE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "Emulation",
        "sendData",
        "",
        "const char*",
        "data",
        "len",
        "dupDisplayOutput",
        "lockPtyRequest",
        "suspend",
        "useUtf8Request",
        "stateSet",
        "state",
        "zmodemSendDetected",
        "zmodemRecvDetected",
        "changeTabTextColorRequest",
        "color",
        "programUsesMouseChanged",
        "usesMouse",
        "programBracketedPasteModeChanged",
        "bracketedPasteMode",
        "outputChanged",
        "titleChanged",
        "title",
        "newTitle",
        "imageSizeChanged",
        "lineCount",
        "columnCount",
        "imageSizeInitialized",
        "imageResizeRequest",
        "QSize",
        "size",
        "profileChangeCommandReceived",
        "text",
        "flowControlKeyPressed",
        "suspendKeyPressed",
        "primaryScreenInUse",
        "use",
        "cursorChanged",
        "KeyboardCursorShape",
        "cursorShape",
        "blinkingCursorEnabled",
        "handleCommandFromKeyboard",
        "KeyboardTranslator::Command",
        "command",
        "handleCtrlC",
        "outputFromKeypressEvent",
        "setImageSize",
        "lines",
        "columns",
        "sendText",
        "sendKeyEvent",
        "QKeyEvent*",
        "fromPaste",
        "sendMouseEvent",
        "buttons",
        "column",
        "line",
        "eventType",
        "sendString",
        "string",
        "length",
        "receiveData",
        "buffer",
        "dupDisplayCharacter",
        "wchar_t",
        "cc",
        "bufferedUpdate",
        "checkScreenInUse",
        "showBulk",
        "usesMouseChanged",
        "bracketedPasteModeChanged"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'sendData'
        QtMocHelpers::SignalData<void(const char *, int)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 3, 4 }, { QMetaType::Int, 5 },
        }}),
        // Signal 'dupDisplayOutput'
        QtMocHelpers::SignalData<void(const char *, int)>(6, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 3, 4 }, { QMetaType::Int, 5 },
        }}),
        // Signal 'lockPtyRequest'
        QtMocHelpers::SignalData<void(bool)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 8 },
        }}),
        // Signal 'useUtf8Request'
        QtMocHelpers::SignalData<void(bool)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 2 },
        }}),
        // Signal 'stateSet'
        QtMocHelpers::SignalData<void(int)>(10, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 11 },
        }}),
        // Signal 'zmodemSendDetected'
        QtMocHelpers::SignalData<void()>(12, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'zmodemRecvDetected'
        QtMocHelpers::SignalData<void()>(13, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'changeTabTextColorRequest'
        QtMocHelpers::SignalData<void(int)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 15 },
        }}),
        // Signal 'programUsesMouseChanged'
        QtMocHelpers::SignalData<void(bool)>(16, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 17 },
        }}),
        // Signal 'programBracketedPasteModeChanged'
        QtMocHelpers::SignalData<void(bool)>(18, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 19 },
        }}),
        // Signal 'outputChanged'
        QtMocHelpers::SignalData<void()>(20, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'titleChanged'
        QtMocHelpers::SignalData<void(int, const QString &)>(21, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 22 }, { QMetaType::QString, 23 },
        }}),
        // Signal 'imageSizeChanged'
        QtMocHelpers::SignalData<void(int, int)>(24, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 25 }, { QMetaType::Int, 26 },
        }}),
        // Signal 'imageSizeInitialized'
        QtMocHelpers::SignalData<void()>(27, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'imageResizeRequest'
        QtMocHelpers::SignalData<void(const QSize &)>(28, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 29, 30 },
        }}),
        // Signal 'profileChangeCommandReceived'
        QtMocHelpers::SignalData<void(const QString &)>(31, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 32 },
        }}),
        // Signal 'flowControlKeyPressed'
        QtMocHelpers::SignalData<void(bool)>(33, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 34 },
        }}),
        // Signal 'primaryScreenInUse'
        QtMocHelpers::SignalData<void(bool)>(35, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 36 },
        }}),
        // Signal 'cursorChanged'
        QtMocHelpers::SignalData<void(enum KeyboardCursorShape, bool)>(37, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 38, 39 }, { QMetaType::Bool, 40 },
        }}),
        // Signal 'handleCommandFromKeyboard'
        QtMocHelpers::SignalData<void(KeyboardTranslator::Command)>(41, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 42, 43 },
        }}),
        // Signal 'handleCtrlC'
        QtMocHelpers::SignalData<void()>(44, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'outputFromKeypressEvent'
        QtMocHelpers::SignalData<void()>(45, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'setImageSize'
        QtMocHelpers::SlotData<void(int, int)>(46, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 47 }, { QMetaType::Int, 48 },
        }}),
        // Slot 'sendText'
        QtMocHelpers::SlotData<void(const QString &)>(49, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 32 },
        }}),
        // Slot 'sendKeyEvent'
        QtMocHelpers::SlotData<void(QKeyEvent *, bool)>(50, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 51, 2 }, { QMetaType::Bool, 52 },
        }}),
        // Slot 'sendMouseEvent'
        QtMocHelpers::SlotData<void(int, int, int, int)>(53, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 54 }, { QMetaType::Int, 55 }, { QMetaType::Int, 56 }, { QMetaType::Int, 57 },
        }}),
        // Slot 'sendString'
        QtMocHelpers::SlotData<void(const char *, int)>(58, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 3, 59 }, { QMetaType::Int, 60 },
        }}),
        // Slot 'sendString'
        QtMocHelpers::SlotData<void(const char *)>(58, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 3, 59 },
        }}),
        // Slot 'receiveData'
        QtMocHelpers::SlotData<void(const char *, int)>(61, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 3, 62 }, { QMetaType::Int, 5 },
        }}),
        // Slot 'dupDisplayCharacter'
        QtMocHelpers::SlotData<void(wchar_t)>(63, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 64, 65 },
        }}),
        // Slot 'bufferedUpdate'
        QtMocHelpers::SlotData<void()>(66, 2, QMC::AccessProtected, QMetaType::Void),
        // Slot 'checkScreenInUse'
        QtMocHelpers::SlotData<void()>(67, 2, QMC::AccessProtected, QMetaType::Void),
        // Slot 'showBulk'
        QtMocHelpers::SlotData<void()>(68, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'usesMouseChanged'
        QtMocHelpers::SlotData<void(bool)>(69, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Bool, 17 },
        }}),
        // Slot 'bracketedPasteModeChanged'
        QtMocHelpers::SlotData<void(bool)>(70, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Bool, 19 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<Emulation, qt_meta_tag_ZN9EmulationE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject Emulation::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN9EmulationE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN9EmulationE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN9EmulationE_t>.metaTypes,
    nullptr
} };

void Emulation::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<Emulation *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->sendData((*reinterpret_cast<std::add_pointer_t<const char*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 1: _t->dupDisplayOutput((*reinterpret_cast<std::add_pointer_t<const char*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 2: _t->lockPtyRequest((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 3: _t->useUtf8Request((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 4: _t->stateSet((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 5: _t->zmodemSendDetected(); break;
        case 6: _t->zmodemRecvDetected(); break;
        case 7: _t->changeTabTextColorRequest((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 8: _t->programUsesMouseChanged((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 9: _t->programBracketedPasteModeChanged((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 10: _t->outputChanged(); break;
        case 11: _t->titleChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 12: _t->imageSizeChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 13: _t->imageSizeInitialized(); break;
        case 14: _t->imageResizeRequest((*reinterpret_cast<std::add_pointer_t<QSize>>(_a[1]))); break;
        case 15: _t->profileChangeCommandReceived((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 16: _t->flowControlKeyPressed((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 17: _t->primaryScreenInUse((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 18: _t->cursorChanged((*reinterpret_cast<std::add_pointer_t<enum KeyboardCursorShape>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2]))); break;
        case 19: _t->handleCommandFromKeyboard((*reinterpret_cast<std::add_pointer_t<KeyboardTranslator::Command>>(_a[1]))); break;
        case 20: _t->handleCtrlC(); break;
        case 21: _t->outputFromKeypressEvent(); break;
        case 22: _t->setImageSize((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 23: _t->sendText((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 24: _t->sendKeyEvent((*reinterpret_cast<std::add_pointer_t<QKeyEvent*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2]))); break;
        case 25: _t->sendMouseEvent((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[4]))); break;
        case 26: _t->sendString((*reinterpret_cast<std::add_pointer_t<const char*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 27: _t->sendString((*reinterpret_cast<std::add_pointer_t<const char*>>(_a[1]))); break;
        case 28: _t->receiveData((*reinterpret_cast<std::add_pointer_t<const char*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 29: _t->dupDisplayCharacter((*reinterpret_cast<std::add_pointer_t<wchar_t>>(_a[1]))); break;
        case 30: _t->bufferedUpdate(); break;
        case 31: _t->checkScreenInUse(); break;
        case 32: _t->showBulk(); break;
        case 33: _t->usesMouseChanged((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 34: _t->bracketedPasteModeChanged((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (Emulation::*)(const char * , int )>(_a, &Emulation::sendData, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (Emulation::*)(const char * , int )>(_a, &Emulation::dupDisplayOutput, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (Emulation::*)(bool )>(_a, &Emulation::lockPtyRequest, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (Emulation::*)(bool )>(_a, &Emulation::useUtf8Request, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (Emulation::*)(int )>(_a, &Emulation::stateSet, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (Emulation::*)()>(_a, &Emulation::zmodemSendDetected, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (Emulation::*)()>(_a, &Emulation::zmodemRecvDetected, 6))
            return;
        if (QtMocHelpers::indexOfMethod<void (Emulation::*)(int )>(_a, &Emulation::changeTabTextColorRequest, 7))
            return;
        if (QtMocHelpers::indexOfMethod<void (Emulation::*)(bool )>(_a, &Emulation::programUsesMouseChanged, 8))
            return;
        if (QtMocHelpers::indexOfMethod<void (Emulation::*)(bool )>(_a, &Emulation::programBracketedPasteModeChanged, 9))
            return;
        if (QtMocHelpers::indexOfMethod<void (Emulation::*)()>(_a, &Emulation::outputChanged, 10))
            return;
        if (QtMocHelpers::indexOfMethod<void (Emulation::*)(int , const QString & )>(_a, &Emulation::titleChanged, 11))
            return;
        if (QtMocHelpers::indexOfMethod<void (Emulation::*)(int , int )>(_a, &Emulation::imageSizeChanged, 12))
            return;
        if (QtMocHelpers::indexOfMethod<void (Emulation::*)()>(_a, &Emulation::imageSizeInitialized, 13))
            return;
        if (QtMocHelpers::indexOfMethod<void (Emulation::*)(const QSize & )>(_a, &Emulation::imageResizeRequest, 14))
            return;
        if (QtMocHelpers::indexOfMethod<void (Emulation::*)(const QString & )>(_a, &Emulation::profileChangeCommandReceived, 15))
            return;
        if (QtMocHelpers::indexOfMethod<void (Emulation::*)(bool )>(_a, &Emulation::flowControlKeyPressed, 16))
            return;
        if (QtMocHelpers::indexOfMethod<void (Emulation::*)(bool )>(_a, &Emulation::primaryScreenInUse, 17))
            return;
        if (QtMocHelpers::indexOfMethod<void (Emulation::*)(KeyboardCursorShape , bool )>(_a, &Emulation::cursorChanged, 18))
            return;
        if (QtMocHelpers::indexOfMethod<void (Emulation::*)(KeyboardTranslator::Command )>(_a, &Emulation::handleCommandFromKeyboard, 19))
            return;
        if (QtMocHelpers::indexOfMethod<void (Emulation::*)()>(_a, &Emulation::handleCtrlC, 20))
            return;
        if (QtMocHelpers::indexOfMethod<void (Emulation::*)()>(_a, &Emulation::outputFromKeypressEvent, 21))
            return;
    }
}

const QMetaObject *Emulation::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Emulation::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN9EmulationE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int Emulation::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 35)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 35;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 35)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 35;
    }
    return _id;
}

// SIGNAL 0
void Emulation::sendData(const char * _t1, int _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1, _t2);
}

// SIGNAL 1
void Emulation::dupDisplayOutput(const char * _t1, int _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1, _t2);
}

// SIGNAL 2
void Emulation::lockPtyRequest(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void Emulation::useUtf8Request(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}

// SIGNAL 4
void Emulation::stateSet(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1);
}

// SIGNAL 5
void Emulation::zmodemSendDetected()
{
    QMetaObject::activate(this, &staticMetaObject, 5, nullptr);
}

// SIGNAL 6
void Emulation::zmodemRecvDetected()
{
    QMetaObject::activate(this, &staticMetaObject, 6, nullptr);
}

// SIGNAL 7
void Emulation::changeTabTextColorRequest(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 7, nullptr, _t1);
}

// SIGNAL 8
void Emulation::programUsesMouseChanged(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 8, nullptr, _t1);
}

// SIGNAL 9
void Emulation::programBracketedPasteModeChanged(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 9, nullptr, _t1);
}

// SIGNAL 10
void Emulation::outputChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 10, nullptr);
}

// SIGNAL 11
void Emulation::titleChanged(int _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 11, nullptr, _t1, _t2);
}

// SIGNAL 12
void Emulation::imageSizeChanged(int _t1, int _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 12, nullptr, _t1, _t2);
}

// SIGNAL 13
void Emulation::imageSizeInitialized()
{
    QMetaObject::activate(this, &staticMetaObject, 13, nullptr);
}

// SIGNAL 14
void Emulation::imageResizeRequest(const QSize & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 14, nullptr, _t1);
}

// SIGNAL 15
void Emulation::profileChangeCommandReceived(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 15, nullptr, _t1);
}

// SIGNAL 16
void Emulation::flowControlKeyPressed(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 16, nullptr, _t1);
}

// SIGNAL 17
void Emulation::primaryScreenInUse(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 17, nullptr, _t1);
}

// SIGNAL 18
void Emulation::cursorChanged(KeyboardCursorShape _t1, bool _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 18, nullptr, _t1, _t2);
}

// SIGNAL 19
void Emulation::handleCommandFromKeyboard(KeyboardTranslator::Command _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 19, nullptr, _t1);
}

// SIGNAL 20
void Emulation::handleCtrlC()
{
    QMetaObject::activate(this, &staticMetaObject, 20, nullptr);
}

// SIGNAL 21
void Emulation::outputFromKeypressEvent()
{
    QMetaObject::activate(this, &staticMetaObject, 21, nullptr);
}
QT_WARNING_POP
