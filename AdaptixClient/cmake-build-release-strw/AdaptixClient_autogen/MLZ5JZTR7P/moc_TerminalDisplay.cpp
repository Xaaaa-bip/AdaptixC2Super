/****************************************************************************
** Meta object code from reading C++ file 'TerminalDisplay.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../Libs/Konsole/TerminalDisplay.h"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'TerminalDisplay.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN15TerminalDisplayE_t {};
} // unnamed namespace

template <> constexpr inline auto TerminalDisplay::qt_create_metaobjectdata<qt_meta_tag_ZN15TerminalDisplayE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "TerminalDisplay",
        "keyPressedSignal",
        "",
        "QKeyEvent*",
        "e",
        "fromPaste",
        "mouseSignal",
        "button",
        "column",
        "line",
        "eventType",
        "mousePressEventForwarded",
        "QMouseEvent*",
        "event",
        "changedFontMetricSignal",
        "height",
        "width",
        "changedContentSizeSignal",
        "changedContentCountSignal",
        "configureRequest",
        "QPoint",
        "position",
        "overrideShortcutCheck",
        "keyEvent",
        "bool&",
        "override",
        "isBusySelecting",
        "sendStringToEmu",
        "const char*",
        "copyAvailable",
        "termGetFocus",
        "termLostFocus",
        "notifyBell",
        "usesMouseChanged",
        "handleCtrlC",
        "updateImage",
        "updateFilters",
        "updateLineProperties",
        "copyClipboard",
        "QClipboard::Mode",
        "mode",
        "pasteClipboard",
        "pasteSelection",
        "selectAll",
        "setFlowControlWarningEnabled",
        "enabled",
        "flowControlWarningEnabled",
        "outputSuspended",
        "suspended",
        "setUsesMouse",
        "usesMouse",
        "usingPrimaryScreen",
        "use",
        "setBracketedPasteMode",
        "bracketedPasteMode",
        "bell",
        "setBackgroundColor",
        "QColor",
        "color",
        "setForegroundColor",
        "setColorTableColor",
        "colorId",
        "selectionChanged",
        "setSelectionOpacity",
        "opacity",
        "getCursorX",
        "getCursorY",
        "setCursorX",
        "x",
        "setCursorY",
        "y",
        "screenGet",
        "row1",
        "col1",
        "row2",
        "col2",
        "setLocked",
        "repaintDisplay",
        "setMessageParentWidget",
        "QWidget*",
        "parent",
        "set_fix_quardCRT_issue33",
        "fix",
        "scrollBarPositionChanged",
        "value",
        "blinkEvent",
        "blinkCursorEvent",
        "enableBell",
        "swapColorTable",
        "tripleClickTimeout"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'keyPressedSignal'
        QtMocHelpers::SignalData<void(QKeyEvent *, bool)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 3, 4 }, { QMetaType::Bool, 5 },
        }}),
        // Signal 'mouseSignal'
        QtMocHelpers::SignalData<void(int, int, int, int)>(6, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 7 }, { QMetaType::Int, 8 }, { QMetaType::Int, 9 }, { QMetaType::Int, 10 },
        }}),
        // Signal 'mousePressEventForwarded'
        QtMocHelpers::SignalData<void(QMouseEvent *)>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 12, 13 },
        }}),
        // Signal 'changedFontMetricSignal'
        QtMocHelpers::SignalData<void(int, int)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 15 }, { QMetaType::Int, 16 },
        }}),
        // Signal 'changedContentSizeSignal'
        QtMocHelpers::SignalData<void(int, int)>(17, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 15 }, { QMetaType::Int, 16 },
        }}),
        // Signal 'changedContentCountSignal'
        QtMocHelpers::SignalData<void(int, int)>(18, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 9 }, { QMetaType::Int, 8 },
        }}),
        // Signal 'configureRequest'
        QtMocHelpers::SignalData<void(const QPoint &)>(19, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 20, 21 },
        }}),
        // Signal 'overrideShortcutCheck'
        QtMocHelpers::SignalData<void(QKeyEvent *, bool &)>(22, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 3, 23 }, { 0x80000000 | 24, 25 },
        }}),
        // Signal 'isBusySelecting'
        QtMocHelpers::SignalData<void(bool)>(26, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 2 },
        }}),
        // Signal 'sendStringToEmu'
        QtMocHelpers::SignalData<void(const char *)>(27, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 28, 2 },
        }}),
        // Signal 'copyAvailable'
        QtMocHelpers::SignalData<void(bool)>(29, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 2 },
        }}),
        // Signal 'termGetFocus'
        QtMocHelpers::SignalData<void()>(30, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'termLostFocus'
        QtMocHelpers::SignalData<void()>(31, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'notifyBell'
        QtMocHelpers::SignalData<void()>(32, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'usesMouseChanged'
        QtMocHelpers::SignalData<void()>(33, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'handleCtrlC'
        QtMocHelpers::SignalData<void()>(34, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'updateImage'
        QtMocHelpers::SlotData<void()>(35, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'updateFilters'
        QtMocHelpers::SlotData<void()>(36, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'updateLineProperties'
        QtMocHelpers::SlotData<void()>(37, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'copyClipboard'
        QtMocHelpers::SlotData<void(QClipboard::Mode)>(38, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 39, 40 },
        }}),
        // Slot 'copyClipboard'
        QtMocHelpers::SlotData<void()>(38, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void),
        // Slot 'pasteClipboard'
        QtMocHelpers::SlotData<void()>(41, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'pasteSelection'
        QtMocHelpers::SlotData<void()>(42, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'selectAll'
        QtMocHelpers::SlotData<void()>(43, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'setFlowControlWarningEnabled'
        QtMocHelpers::SlotData<void(bool)>(44, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 45 },
        }}),
        // Slot 'flowControlWarningEnabled'
        QtMocHelpers::SlotData<bool() const>(46, 2, QMC::AccessPublic, QMetaType::Bool),
        // Slot 'outputSuspended'
        QtMocHelpers::SlotData<void(bool)>(47, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 48 },
        }}),
        // Slot 'setUsesMouse'
        QtMocHelpers::SlotData<void(bool)>(49, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 50 },
        }}),
        // Slot 'usesMouse'
        QtMocHelpers::SlotData<bool() const>(50, 2, QMC::AccessPublic, QMetaType::Bool),
        // Slot 'usingPrimaryScreen'
        QtMocHelpers::SlotData<void(bool)>(51, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 52 },
        }}),
        // Slot 'setBracketedPasteMode'
        QtMocHelpers::SlotData<void(bool)>(53, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 54 },
        }}),
        // Slot 'bracketedPasteMode'
        QtMocHelpers::SlotData<bool() const>(54, 2, QMC::AccessPublic, QMetaType::Bool),
        // Slot 'bell'
        QtMocHelpers::SlotData<void()>(55, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'setBackgroundColor'
        QtMocHelpers::SlotData<void(const QColor &)>(56, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 57, 58 },
        }}),
        // Slot 'setForegroundColor'
        QtMocHelpers::SlotData<void(const QColor &)>(59, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 57, 58 },
        }}),
        // Slot 'setColorTableColor'
        QtMocHelpers::SlotData<void(const int, const QColor &)>(60, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 61 }, { 0x80000000 | 57, 58 },
        }}),
        // Slot 'selectionChanged'
        QtMocHelpers::SlotData<void()>(62, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'setSelectionOpacity'
        QtMocHelpers::SlotData<void(qreal)>(63, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QReal, 64 },
        }}),
        // Slot 'getCursorX'
        QtMocHelpers::SlotData<int() const>(65, 2, QMC::AccessPublic, QMetaType::Int),
        // Slot 'getCursorY'
        QtMocHelpers::SlotData<int() const>(66, 2, QMC::AccessPublic, QMetaType::Int),
        // Slot 'setCursorX'
        QtMocHelpers::SlotData<void(int)>(67, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 68 },
        }}),
        // Slot 'setCursorY'
        QtMocHelpers::SlotData<void(int)>(69, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 70 },
        }}),
        // Slot 'screenGet'
        QtMocHelpers::SlotData<QString(int, int, int, int, int)>(71, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::Int, 72 }, { QMetaType::Int, 73 }, { QMetaType::Int, 74 }, { QMetaType::Int, 75 },
            { QMetaType::Int, 40 },
        }}),
        // Slot 'setLocked'
        QtMocHelpers::SlotData<void(bool)>(76, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 45 },
        }}),
        // Slot 'repaintDisplay'
        QtMocHelpers::SlotData<void()>(77, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'setMessageParentWidget'
        QtMocHelpers::SlotData<void(QWidget *)>(78, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 79, 80 },
        }}),
        // Slot 'set_fix_quardCRT_issue33'
        QtMocHelpers::SlotData<void(bool)>(81, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 82 },
        }}),
        // Slot 'scrollBarPositionChanged'
        QtMocHelpers::SlotData<void(int)>(83, 2, QMC::AccessProtected, QMetaType::Void, {{
            { QMetaType::Int, 84 },
        }}),
        // Slot 'blinkEvent'
        QtMocHelpers::SlotData<void()>(85, 2, QMC::AccessProtected, QMetaType::Void),
        // Slot 'blinkCursorEvent'
        QtMocHelpers::SlotData<void()>(86, 2, QMC::AccessProtected, QMetaType::Void),
        // Slot 'enableBell'
        QtMocHelpers::SlotData<void()>(87, 2, QMC::AccessProtected, QMetaType::Void),
        // Slot 'swapColorTable'
        QtMocHelpers::SlotData<void()>(88, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'tripleClickTimeout'
        QtMocHelpers::SlotData<void()>(89, 2, QMC::AccessPrivate, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<TerminalDisplay, qt_meta_tag_ZN15TerminalDisplayE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject TerminalDisplay::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15TerminalDisplayE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15TerminalDisplayE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN15TerminalDisplayE_t>.metaTypes,
    nullptr
} };

void TerminalDisplay::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<TerminalDisplay *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->keyPressedSignal((*reinterpret_cast<std::add_pointer_t<QKeyEvent*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2]))); break;
        case 1: _t->mouseSignal((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[4]))); break;
        case 2: _t->mousePressEventForwarded((*reinterpret_cast<std::add_pointer_t<QMouseEvent*>>(_a[1]))); break;
        case 3: _t->changedFontMetricSignal((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 4: _t->changedContentSizeSignal((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 5: _t->changedContentCountSignal((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 6: _t->configureRequest((*reinterpret_cast<std::add_pointer_t<QPoint>>(_a[1]))); break;
        case 7: _t->overrideShortcutCheck((*reinterpret_cast<std::add_pointer_t<QKeyEvent*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool&>>(_a[2]))); break;
        case 8: _t->isBusySelecting((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 9: _t->sendStringToEmu((*reinterpret_cast<std::add_pointer_t<const char*>>(_a[1]))); break;
        case 10: _t->copyAvailable((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 11: _t->termGetFocus(); break;
        case 12: _t->termLostFocus(); break;
        case 13: _t->notifyBell(); break;
        case 14: _t->usesMouseChanged(); break;
        case 15: _t->handleCtrlC(); break;
        case 16: _t->updateImage(); break;
        case 17: _t->updateFilters(); break;
        case 18: _t->updateLineProperties(); break;
        case 19: _t->copyClipboard((*reinterpret_cast<std::add_pointer_t<QClipboard::Mode>>(_a[1]))); break;
        case 20: _t->copyClipboard(); break;
        case 21: _t->pasteClipboard(); break;
        case 22: _t->pasteSelection(); break;
        case 23: _t->selectAll(); break;
        case 24: _t->setFlowControlWarningEnabled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 25: { bool _r = _t->flowControlWarningEnabled();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 26: _t->outputSuspended((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 27: _t->setUsesMouse((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 28: { bool _r = _t->usesMouse();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 29: _t->usingPrimaryScreen((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 30: _t->setBracketedPasteMode((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 31: { bool _r = _t->bracketedPasteMode();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 32: _t->bell(); break;
        case 33: _t->setBackgroundColor((*reinterpret_cast<std::add_pointer_t<QColor>>(_a[1]))); break;
        case 34: _t->setForegroundColor((*reinterpret_cast<std::add_pointer_t<QColor>>(_a[1]))); break;
        case 35: _t->setColorTableColor((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QColor>>(_a[2]))); break;
        case 36: _t->selectionChanged(); break;
        case 37: _t->setSelectionOpacity((*reinterpret_cast<std::add_pointer_t<qreal>>(_a[1]))); break;
        case 38: { int _r = _t->getCursorX();
            if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
        case 39: { int _r = _t->getCursorY();
            if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
        case 40: _t->setCursorX((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 41: _t->setCursorY((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 42: { QString _r = _t->screenGet((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[5])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 43: _t->setLocked((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 44: _t->repaintDisplay(); break;
        case 45: _t->setMessageParentWidget((*reinterpret_cast<std::add_pointer_t<QWidget*>>(_a[1]))); break;
        case 46: _t->set_fix_quardCRT_issue33((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 47: _t->scrollBarPositionChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 48: _t->blinkEvent(); break;
        case 49: _t->blinkCursorEvent(); break;
        case 50: _t->enableBell(); break;
        case 51: _t->swapColorTable(); break;
        case 52: _t->tripleClickTimeout(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 45:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QWidget* >(); break;
            }
            break;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (TerminalDisplay::*)(QKeyEvent * , bool )>(_a, &TerminalDisplay::keyPressedSignal, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (TerminalDisplay::*)(int , int , int , int )>(_a, &TerminalDisplay::mouseSignal, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (TerminalDisplay::*)(QMouseEvent * )>(_a, &TerminalDisplay::mousePressEventForwarded, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (TerminalDisplay::*)(int , int )>(_a, &TerminalDisplay::changedFontMetricSignal, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (TerminalDisplay::*)(int , int )>(_a, &TerminalDisplay::changedContentSizeSignal, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (TerminalDisplay::*)(int , int )>(_a, &TerminalDisplay::changedContentCountSignal, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (TerminalDisplay::*)(const QPoint & )>(_a, &TerminalDisplay::configureRequest, 6))
            return;
        if (QtMocHelpers::indexOfMethod<void (TerminalDisplay::*)(QKeyEvent * , bool & )>(_a, &TerminalDisplay::overrideShortcutCheck, 7))
            return;
        if (QtMocHelpers::indexOfMethod<void (TerminalDisplay::*)(bool )>(_a, &TerminalDisplay::isBusySelecting, 8))
            return;
        if (QtMocHelpers::indexOfMethod<void (TerminalDisplay::*)(const char * )>(_a, &TerminalDisplay::sendStringToEmu, 9))
            return;
        if (QtMocHelpers::indexOfMethod<void (TerminalDisplay::*)(bool )>(_a, &TerminalDisplay::copyAvailable, 10))
            return;
        if (QtMocHelpers::indexOfMethod<void (TerminalDisplay::*)()>(_a, &TerminalDisplay::termGetFocus, 11))
            return;
        if (QtMocHelpers::indexOfMethod<void (TerminalDisplay::*)()>(_a, &TerminalDisplay::termLostFocus, 12))
            return;
        if (QtMocHelpers::indexOfMethod<void (TerminalDisplay::*)()>(_a, &TerminalDisplay::notifyBell, 13))
            return;
        if (QtMocHelpers::indexOfMethod<void (TerminalDisplay::*)()>(_a, &TerminalDisplay::usesMouseChanged, 14))
            return;
        if (QtMocHelpers::indexOfMethod<void (TerminalDisplay::*)()>(_a, &TerminalDisplay::handleCtrlC, 15))
            return;
    }
}

const QMetaObject *TerminalDisplay::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *TerminalDisplay::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15TerminalDisplayE_t>.strings))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int TerminalDisplay::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 53)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 53;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 53)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 53;
    }
    return _id;
}

// SIGNAL 0
void TerminalDisplay::keyPressedSignal(QKeyEvent * _t1, bool _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1, _t2);
}

// SIGNAL 1
void TerminalDisplay::mouseSignal(int _t1, int _t2, int _t3, int _t4)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1, _t2, _t3, _t4);
}

// SIGNAL 2
void TerminalDisplay::mousePressEventForwarded(QMouseEvent * _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void TerminalDisplay::changedFontMetricSignal(int _t1, int _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1, _t2);
}

// SIGNAL 4
void TerminalDisplay::changedContentSizeSignal(int _t1, int _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1, _t2);
}

// SIGNAL 5
void TerminalDisplay::changedContentCountSignal(int _t1, int _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1, _t2);
}

// SIGNAL 6
void TerminalDisplay::configureRequest(const QPoint & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 6, nullptr, _t1);
}

// SIGNAL 7
void TerminalDisplay::overrideShortcutCheck(QKeyEvent * _t1, bool & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 7, nullptr, _t1, _t2);
}

// SIGNAL 8
void TerminalDisplay::isBusySelecting(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 8, nullptr, _t1);
}

// SIGNAL 9
void TerminalDisplay::sendStringToEmu(const char * _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 9, nullptr, _t1);
}

// SIGNAL 10
void TerminalDisplay::copyAvailable(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 10, nullptr, _t1);
}

// SIGNAL 11
void TerminalDisplay::termGetFocus()
{
    QMetaObject::activate(this, &staticMetaObject, 11, nullptr);
}

// SIGNAL 12
void TerminalDisplay::termLostFocus()
{
    QMetaObject::activate(this, &staticMetaObject, 12, nullptr);
}

// SIGNAL 13
void TerminalDisplay::notifyBell()
{
    QMetaObject::activate(this, &staticMetaObject, 13, nullptr);
}

// SIGNAL 14
void TerminalDisplay::usesMouseChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 14, nullptr);
}

// SIGNAL 15
void TerminalDisplay::handleCtrlC()
{
    QMetaObject::activate(this, &staticMetaObject, 15, nullptr);
}
namespace {
struct qt_meta_tag_ZN17AutoScrollHandlerE_t {};
} // unnamed namespace

template <> constexpr inline auto AutoScrollHandler::qt_create_metaobjectdata<qt_meta_tag_ZN17AutoScrollHandlerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AutoScrollHandler"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AutoScrollHandler, qt_meta_tag_ZN17AutoScrollHandlerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AutoScrollHandler::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AutoScrollHandlerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AutoScrollHandlerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN17AutoScrollHandlerE_t>.metaTypes,
    nullptr
} };

void AutoScrollHandler::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AutoScrollHandler *>(_o);
    (void)_t;
    (void)_c;
    (void)_id;
    (void)_a;
}

const QMetaObject *AutoScrollHandler::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AutoScrollHandler::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AutoScrollHandlerE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int AutoScrollHandler::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    return _id;
}
namespace {
struct qt_meta_tag_ZN9ScrollBarE_t {};
} // unnamed namespace

template <> constexpr inline auto ScrollBar::qt_create_metaobjectdata<qt_meta_tag_ZN9ScrollBarE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "ScrollBar"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<ScrollBar, qt_meta_tag_ZN9ScrollBarE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject ScrollBar::staticMetaObject = { {
    QMetaObject::SuperData::link<QScrollBar::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN9ScrollBarE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN9ScrollBarE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN9ScrollBarE_t>.metaTypes,
    nullptr
} };

void ScrollBar::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<ScrollBar *>(_o);
    (void)_t;
    (void)_c;
    (void)_id;
    (void)_a;
}

const QMetaObject *ScrollBar::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ScrollBar::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN9ScrollBarE_t>.strings))
        return static_cast<void*>(this);
    return QScrollBar::qt_metacast(_clname);
}

int ScrollBar::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QScrollBar::qt_metacall(_c, _id, _a);
    return _id;
}
namespace {
struct qt_meta_tag_ZN31MultilineConfirmationMessageBoxE_t {};
} // unnamed namespace

template <> constexpr inline auto MultilineConfirmationMessageBox::qt_create_metaobjectdata<qt_meta_tag_ZN31MultilineConfirmationMessageBoxE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "MultilineConfirmationMessageBox"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<MultilineConfirmationMessageBox, qt_meta_tag_ZN31MultilineConfirmationMessageBoxE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject MultilineConfirmationMessageBox::staticMetaObject = { {
    QMetaObject::SuperData::link<QDialog::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN31MultilineConfirmationMessageBoxE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN31MultilineConfirmationMessageBoxE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN31MultilineConfirmationMessageBoxE_t>.metaTypes,
    nullptr
} };

void MultilineConfirmationMessageBox::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<MultilineConfirmationMessageBox *>(_o);
    (void)_t;
    (void)_c;
    (void)_id;
    (void)_a;
}

const QMetaObject *MultilineConfirmationMessageBox::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MultilineConfirmationMessageBox::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN31MultilineConfirmationMessageBoxE_t>.strings))
        return static_cast<void*>(this);
    return QDialog::qt_metacast(_clname);
}

int MultilineConfirmationMessageBox::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QDialog::qt_metacall(_c, _id, _a);
    return _id;
}
QT_WARNING_POP
