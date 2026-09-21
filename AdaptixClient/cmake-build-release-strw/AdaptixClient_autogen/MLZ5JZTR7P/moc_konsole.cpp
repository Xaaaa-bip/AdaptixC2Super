/****************************************************************************
** Meta object code from reading C++ file 'konsole.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../Libs/Konsole/konsole.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'konsole.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN11QTermWidgetE_t {};
} // unnamed namespace

template <> constexpr inline auto QTermWidget::qt_create_metaobjectdata<qt_meta_tag_ZN11QTermWidgetE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "QTermWidget",
        "finished",
        "",
        "copyAvailable",
        "termGetFocus",
        "termLostFocus",
        "termKeyPressed",
        "QKeyEvent*",
        "urlActivated",
        "QUrl",
        "uint32_t",
        "opcode",
        "notifyBell",
        "activity",
        "silence",
        "stateChanged",
        "state",
        "flowControlEnabledChanged",
        "enabled",
        "sendData",
        "const char*",
        "dupDisplayOutput",
        "data",
        "len",
        "profileChanged",
        "profile",
        "titleChanged",
        "title",
        "newTitle",
        "changeTabTextColorRequest",
        "termSizeChange",
        "lines",
        "columns",
        "mousePressEventForwarded",
        "QMouseEvent*",
        "event",
        "zmodemSendDetected",
        "zmodemRecvDetected",
        "handleCtrlC",
        "copyClipboard",
        "copySelection",
        "pasteClipboard",
        "pasteSelection",
        "selectAll",
        "zoomIn",
        "zoomOut",
        "setSize",
        "QSize",
        "setKeyBindings",
        "kb",
        "clearScrollback",
        "clearScreen",
        "clear",
        "toggleShowSearchBar",
        "saveHistory",
        "QIODevice*",
        "device",
        "format",
        "start",
        "end",
        "QTextStream*",
        "stream",
        "screenShot",
        "QPixmap*",
        "pixmap",
        "fileName",
        "repaintDisplay",
        "sessionFinished",
        "updateTerminalSize",
        "selectionChanged",
        "textSelected",
        "monitorTimerDone",
        "activityStateSet",
        "cursorChanged",
        "Emulation::KeyboardCursorShape",
        "cursorShape",
        "blinkingCursorEnabled"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'finished'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'copyAvailable'
        QtMocHelpers::SignalData<void(bool)>(3, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 2 },
        }}),
        // Signal 'termGetFocus'
        QtMocHelpers::SignalData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'termLostFocus'
        QtMocHelpers::SignalData<void()>(5, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'termKeyPressed'
        QtMocHelpers::SignalData<void(QKeyEvent *)>(6, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 7, 2 },
        }}),
        // Signal 'urlActivated'
        QtMocHelpers::SignalData<void(const QUrl &, uint32_t)>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 9, 2 }, { 0x80000000 | 10, 11 },
        }}),
        // Signal 'notifyBell'
        QtMocHelpers::SignalData<void()>(12, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'activity'
        QtMocHelpers::SignalData<void()>(13, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'silence'
        QtMocHelpers::SignalData<void()>(14, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'stateChanged'
        QtMocHelpers::SignalData<void(int)>(15, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 16 },
        }}),
        // Signal 'flowControlEnabledChanged'
        QtMocHelpers::SignalData<void(bool)>(17, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 18 },
        }}),
        // Signal 'sendData'
        QtMocHelpers::SignalData<void(const char *, int)>(19, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 20, 2 }, { QMetaType::Int, 2 },
        }}),
        // Signal 'dupDisplayOutput'
        QtMocHelpers::SignalData<void(const char *, int)>(21, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 20, 22 }, { QMetaType::Int, 23 },
        }}),
        // Signal 'profileChanged'
        QtMocHelpers::SignalData<void(const QString &)>(24, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 25 },
        }}),
        // Signal 'titleChanged'
        QtMocHelpers::SignalData<void(int, const QString &)>(26, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 27 }, { QMetaType::QString, 28 },
        }}),
        // Signal 'changeTabTextColorRequest'
        QtMocHelpers::SignalData<void(int)>(29, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 2 },
        }}),
        // Signal 'termSizeChange'
        QtMocHelpers::SignalData<void(int, int)>(30, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 31 }, { QMetaType::Int, 32 },
        }}),
        // Signal 'mousePressEventForwarded'
        QtMocHelpers::SignalData<void(QMouseEvent *)>(33, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 34, 35 },
        }}),
        // Signal 'zmodemSendDetected'
        QtMocHelpers::SignalData<void()>(36, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'zmodemRecvDetected'
        QtMocHelpers::SignalData<void()>(37, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'handleCtrlC'
        QtMocHelpers::SignalData<void()>(38, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'copyClipboard'
        QtMocHelpers::SlotData<void()>(39, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'copySelection'
        QtMocHelpers::SlotData<void()>(40, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'pasteClipboard'
        QtMocHelpers::SlotData<void()>(41, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'pasteSelection'
        QtMocHelpers::SlotData<void()>(42, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'selectAll'
        QtMocHelpers::SlotData<void()>(43, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'zoomIn'
        QtMocHelpers::SlotData<int()>(44, 2, QMC::AccessPublic, QMetaType::Int),
        // Slot 'zoomOut'
        QtMocHelpers::SlotData<int()>(45, 2, QMC::AccessPublic, QMetaType::Int),
        // Slot 'setSize'
        QtMocHelpers::SlotData<void(const QSize &)>(46, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 47, 2 },
        }}),
        // Slot 'setKeyBindings'
        QtMocHelpers::SlotData<void(const QString &)>(48, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 49 },
        }}),
        // Slot 'clearScrollback'
        QtMocHelpers::SlotData<void()>(50, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'clearScreen'
        QtMocHelpers::SlotData<void()>(51, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'clear'
        QtMocHelpers::SlotData<void()>(52, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'toggleShowSearchBar'
        QtMocHelpers::SlotData<void()>(53, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'saveHistory'
        QtMocHelpers::SlotData<void(QIODevice *, int, int, int)>(54, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 55, 56 }, { QMetaType::Int, 57 }, { QMetaType::Int, 58 }, { QMetaType::Int, 59 },
        }}),
        // Slot 'saveHistory'
        QtMocHelpers::SlotData<void(QIODevice *, int, int)>(54, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 55, 56 }, { QMetaType::Int, 57 }, { QMetaType::Int, 58 },
        }}),
        // Slot 'saveHistory'
        QtMocHelpers::SlotData<void(QIODevice *, int)>(54, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 55, 56 }, { QMetaType::Int, 57 },
        }}),
        // Slot 'saveHistory'
        QtMocHelpers::SlotData<void(QIODevice *)>(54, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 55, 56 },
        }}),
        // Slot 'saveHistory'
        QtMocHelpers::SlotData<void(QTextStream *, int, int, int)>(54, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 60, 61 }, { QMetaType::Int, 57 }, { QMetaType::Int, 58 }, { QMetaType::Int, 59 },
        }}),
        // Slot 'saveHistory'
        QtMocHelpers::SlotData<void(QTextStream *, int, int)>(54, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 60, 61 }, { QMetaType::Int, 57 }, { QMetaType::Int, 58 },
        }}),
        // Slot 'saveHistory'
        QtMocHelpers::SlotData<void(QTextStream *, int)>(54, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 60, 61 }, { QMetaType::Int, 57 },
        }}),
        // Slot 'saveHistory'
        QtMocHelpers::SlotData<void(QTextStream *)>(54, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 60, 61 },
        }}),
        // Slot 'screenShot'
        QtMocHelpers::SlotData<void(QPixmap *)>(62, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 63, 64 },
        }}),
        // Slot 'screenShot'
        QtMocHelpers::SlotData<void(const QString &)>(62, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 65 },
        }}),
        // Slot 'repaintDisplay'
        QtMocHelpers::SlotData<void()>(66, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'sessionFinished'
        QtMocHelpers::SlotData<void()>(67, 2, QMC::AccessProtected, QMetaType::Void),
        // Slot 'updateTerminalSize'
        QtMocHelpers::SlotData<void()>(68, 2, QMC::AccessProtected, QMetaType::Void),
        // Slot 'selectionChanged'
        QtMocHelpers::SlotData<void(bool)>(69, 2, QMC::AccessProtected, QMetaType::Void, {{
            { QMetaType::Bool, 70 },
        }}),
        // Slot 'monitorTimerDone'
        QtMocHelpers::SlotData<void()>(71, 2, QMC::AccessProtected, QMetaType::Void),
        // Slot 'activityStateSet'
        QtMocHelpers::SlotData<void(int)>(72, 2, QMC::AccessProtected, QMetaType::Void, {{
            { QMetaType::Int, 2 },
        }}),
        // Slot 'cursorChanged'
        QtMocHelpers::SlotData<void(Emulation::KeyboardCursorShape, bool)>(73, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 74, 75 }, { QMetaType::Bool, 76 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<QTermWidget, qt_meta_tag_ZN11QTermWidgetE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject QTermWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11QTermWidgetE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11QTermWidgetE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN11QTermWidgetE_t>.metaTypes,
    nullptr
} };

void QTermWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<QTermWidget *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->finished(); break;
        case 1: _t->copyAvailable((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 2: _t->termGetFocus(); break;
        case 3: _t->termLostFocus(); break;
        case 4: _t->termKeyPressed((*reinterpret_cast<std::add_pointer_t<QKeyEvent*>>(_a[1]))); break;
        case 5: _t->urlActivated((*reinterpret_cast<std::add_pointer_t<QUrl>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<uint32_t>>(_a[2]))); break;
        case 6: _t->notifyBell(); break;
        case 7: _t->activity(); break;
        case 8: _t->silence(); break;
        case 9: _t->stateChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 10: _t->flowControlEnabledChanged((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 11: _t->sendData((*reinterpret_cast<std::add_pointer_t<const char*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 12: _t->dupDisplayOutput((*reinterpret_cast<std::add_pointer_t<const char*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 13: _t->profileChanged((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 14: _t->titleChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 15: _t->changeTabTextColorRequest((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 16: _t->termSizeChange((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 17: _t->mousePressEventForwarded((*reinterpret_cast<std::add_pointer_t<QMouseEvent*>>(_a[1]))); break;
        case 18: _t->zmodemSendDetected(); break;
        case 19: _t->zmodemRecvDetected(); break;
        case 20: _t->handleCtrlC(); break;
        case 21: _t->copyClipboard(); break;
        case 22: _t->copySelection(); break;
        case 23: _t->pasteClipboard(); break;
        case 24: _t->pasteSelection(); break;
        case 25: _t->selectAll(); break;
        case 26: { int _r = _t->zoomIn();
            if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
        case 27: { int _r = _t->zoomOut();
            if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
        case 28: _t->setSize((*reinterpret_cast<std::add_pointer_t<QSize>>(_a[1]))); break;
        case 29: _t->setKeyBindings((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 30: _t->clearScrollback(); break;
        case 31: _t->clearScreen(); break;
        case 32: _t->clear(); break;
        case 33: _t->toggleShowSearchBar(); break;
        case 34: _t->saveHistory((*reinterpret_cast<std::add_pointer_t<QIODevice*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[4]))); break;
        case 35: _t->saveHistory((*reinterpret_cast<std::add_pointer_t<QIODevice*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3]))); break;
        case 36: _t->saveHistory((*reinterpret_cast<std::add_pointer_t<QIODevice*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 37: _t->saveHistory((*reinterpret_cast<std::add_pointer_t<QIODevice*>>(_a[1]))); break;
        case 38: _t->saveHistory((*reinterpret_cast<std::add_pointer_t<QTextStream*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[4]))); break;
        case 39: _t->saveHistory((*reinterpret_cast<std::add_pointer_t<QTextStream*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3]))); break;
        case 40: _t->saveHistory((*reinterpret_cast<std::add_pointer_t<QTextStream*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 41: _t->saveHistory((*reinterpret_cast<std::add_pointer_t<QTextStream*>>(_a[1]))); break;
        case 42: _t->screenShot((*reinterpret_cast<std::add_pointer_t<QPixmap*>>(_a[1]))); break;
        case 43: _t->screenShot((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 44: _t->repaintDisplay(); break;
        case 45: _t->sessionFinished(); break;
        case 46: _t->updateTerminalSize(); break;
        case 47: _t->selectionChanged((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 48: _t->monitorTimerDone(); break;
        case 49: _t->activityStateSet((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 50: _t->cursorChanged((*reinterpret_cast<std::add_pointer_t<Emulation::KeyboardCursorShape>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 34:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QIODevice* >(); break;
            }
            break;
        case 35:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QIODevice* >(); break;
            }
            break;
        case 36:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QIODevice* >(); break;
            }
            break;
        case 37:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QIODevice* >(); break;
            }
            break;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (QTermWidget::*)()>(_a, &QTermWidget::finished, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (QTermWidget::*)(bool )>(_a, &QTermWidget::copyAvailable, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (QTermWidget::*)()>(_a, &QTermWidget::termGetFocus, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (QTermWidget::*)()>(_a, &QTermWidget::termLostFocus, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (QTermWidget::*)(QKeyEvent * )>(_a, &QTermWidget::termKeyPressed, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (QTermWidget::*)(const QUrl & , uint32_t )>(_a, &QTermWidget::urlActivated, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (QTermWidget::*)()>(_a, &QTermWidget::notifyBell, 6))
            return;
        if (QtMocHelpers::indexOfMethod<void (QTermWidget::*)()>(_a, &QTermWidget::activity, 7))
            return;
        if (QtMocHelpers::indexOfMethod<void (QTermWidget::*)()>(_a, &QTermWidget::silence, 8))
            return;
        if (QtMocHelpers::indexOfMethod<void (QTermWidget::*)(int )>(_a, &QTermWidget::stateChanged, 9))
            return;
        if (QtMocHelpers::indexOfMethod<void (QTermWidget::*)(bool )>(_a, &QTermWidget::flowControlEnabledChanged, 10))
            return;
        if (QtMocHelpers::indexOfMethod<void (QTermWidget::*)(const char * , int )>(_a, &QTermWidget::sendData, 11))
            return;
        if (QtMocHelpers::indexOfMethod<void (QTermWidget::*)(const char * , int )>(_a, &QTermWidget::dupDisplayOutput, 12))
            return;
        if (QtMocHelpers::indexOfMethod<void (QTermWidget::*)(const QString & )>(_a, &QTermWidget::profileChanged, 13))
            return;
        if (QtMocHelpers::indexOfMethod<void (QTermWidget::*)(int , const QString & )>(_a, &QTermWidget::titleChanged, 14))
            return;
        if (QtMocHelpers::indexOfMethod<void (QTermWidget::*)(int )>(_a, &QTermWidget::changeTabTextColorRequest, 15))
            return;
        if (QtMocHelpers::indexOfMethod<void (QTermWidget::*)(int , int )>(_a, &QTermWidget::termSizeChange, 16))
            return;
        if (QtMocHelpers::indexOfMethod<void (QTermWidget::*)(QMouseEvent * )>(_a, &QTermWidget::mousePressEventForwarded, 17))
            return;
        if (QtMocHelpers::indexOfMethod<void (QTermWidget::*)()>(_a, &QTermWidget::zmodemSendDetected, 18))
            return;
        if (QtMocHelpers::indexOfMethod<void (QTermWidget::*)()>(_a, &QTermWidget::zmodemRecvDetected, 19))
            return;
        if (QtMocHelpers::indexOfMethod<void (QTermWidget::*)()>(_a, &QTermWidget::handleCtrlC, 20))
            return;
    }
}

const QMetaObject *QTermWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *QTermWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11QTermWidgetE_t>.strings))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int QTermWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 51)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 51;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 51)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 51;
    }
    return _id;
}

// SIGNAL 0
void QTermWidget::finished()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void QTermWidget::copyAvailable(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void QTermWidget::termGetFocus()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void QTermWidget::termLostFocus()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void QTermWidget::termKeyPressed(QKeyEvent * _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1);
}

// SIGNAL 5
void QTermWidget::urlActivated(const QUrl & _t1, uint32_t _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1, _t2);
}

// SIGNAL 6
void QTermWidget::notifyBell()
{
    QMetaObject::activate(this, &staticMetaObject, 6, nullptr);
}

// SIGNAL 7
void QTermWidget::activity()
{
    QMetaObject::activate(this, &staticMetaObject, 7, nullptr);
}

// SIGNAL 8
void QTermWidget::silence()
{
    QMetaObject::activate(this, &staticMetaObject, 8, nullptr);
}

// SIGNAL 9
void QTermWidget::stateChanged(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 9, nullptr, _t1);
}

// SIGNAL 10
void QTermWidget::flowControlEnabledChanged(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 10, nullptr, _t1);
}

// SIGNAL 11
void QTermWidget::sendData(const char * _t1, int _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 11, nullptr, _t1, _t2);
}

// SIGNAL 12
void QTermWidget::dupDisplayOutput(const char * _t1, int _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 12, nullptr, _t1, _t2);
}

// SIGNAL 13
void QTermWidget::profileChanged(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 13, nullptr, _t1);
}

// SIGNAL 14
void QTermWidget::titleChanged(int _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 14, nullptr, _t1, _t2);
}

// SIGNAL 15
void QTermWidget::changeTabTextColorRequest(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 15, nullptr, _t1);
}

// SIGNAL 16
void QTermWidget::termSizeChange(int _t1, int _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 16, nullptr, _t1, _t2);
}

// SIGNAL 17
void QTermWidget::mousePressEventForwarded(QMouseEvent * _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 17, nullptr, _t1);
}

// SIGNAL 18
void QTermWidget::zmodemSendDetected()
{
    QMetaObject::activate(this, &staticMetaObject, 18, nullptr);
}

// SIGNAL 19
void QTermWidget::zmodemRecvDetected()
{
    QMetaObject::activate(this, &staticMetaObject, 19, nullptr);
}

// SIGNAL 20
void QTermWidget::handleCtrlC()
{
    QMetaObject::activate(this, &staticMetaObject, 20, nullptr);
}
QT_WARNING_POP
