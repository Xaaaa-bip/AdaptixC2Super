/****************************************************************************
** Meta object code from reading C++ file 'Popover.hpp'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../../../Libs/qlementine/lib/include/oclero/qlementine/widgets/Popover.hpp"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'Popover.hpp' doesn't include <QObject>."
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
struct qt_meta_tag_ZN6oclero10qlementine7PopoverE_t {};
} // unnamed namespace

template <> constexpr inline auto oclero::qlementine::Popover::qt_create_metaobjectdata<qt_meta_tag_ZN6oclero10qlementine7PopoverE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "oclero::qlementine::Popover",
        "manualPositioningChanged",
        "",
        "preferredPositionChanged",
        "preferredAlignmentChanged",
        "contentWidgetChanged",
        "openedChanged",
        "paddingChanged",
        "screenPaddingChanged",
        "anchorWidgetChanged",
        "verticalSpacingChanged",
        "horizontalSpacingChanged",
        "dropShadowColorChanged",
        "radiusChanged",
        "borderWidthChanged",
        "dropShadowRadiusChanged",
        "dropShadowOffsetChanged",
        "canBeOverAnchorChanged",
        "deleteContentAfterClosingChanged",
        "backgroundColorChanged",
        "borderColorChanged",
        "contentMaskEnabledChanged",
        "aboutToOpen",
        "aboutToClose",
        "opened",
        "closed",
        "pressed",
        "released",
        "manualPositionRequested",
        "openPopover",
        "closePopover",
        "togglePopover",
        "manualPositioning",
        "preferredPosition",
        "oclero::qlementine::Popover::Position",
        "preferredAlignment",
        "oclero::qlementine::Popover::Alignment",
        "isOpened",
        "padding",
        "QMargins",
        "horizontalSpacing",
        "verticalSpacing",
        "screenPadding",
        "dropShadowColor",
        "QColor",
        "radius",
        "borderWidth",
        "dropShadowRadius",
        "dropShadowOffset",
        "QPointF",
        "canBeOverAnchor",
        "deleteContentAfterClosing",
        "contentMaskEnabled",
        "backgroundColor",
        "borderColor",
        "Position",
        "Left",
        "Top",
        "Right",
        "Bottom",
        "Alignment",
        "Begin",
        "Center",
        "End"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'manualPositioningChanged'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'preferredPositionChanged'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'preferredAlignmentChanged'
        QtMocHelpers::SignalData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'contentWidgetChanged'
        QtMocHelpers::SignalData<void()>(5, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'openedChanged'
        QtMocHelpers::SignalData<void()>(6, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'paddingChanged'
        QtMocHelpers::SignalData<void()>(7, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'screenPaddingChanged'
        QtMocHelpers::SignalData<void()>(8, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'anchorWidgetChanged'
        QtMocHelpers::SignalData<void()>(9, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'verticalSpacingChanged'
        QtMocHelpers::SignalData<void()>(10, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'horizontalSpacingChanged'
        QtMocHelpers::SignalData<void()>(11, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'dropShadowColorChanged'
        QtMocHelpers::SignalData<void()>(12, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'radiusChanged'
        QtMocHelpers::SignalData<void()>(13, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'borderWidthChanged'
        QtMocHelpers::SignalData<void()>(14, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'dropShadowRadiusChanged'
        QtMocHelpers::SignalData<void()>(15, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'dropShadowOffsetChanged'
        QtMocHelpers::SignalData<void()>(16, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'canBeOverAnchorChanged'
        QtMocHelpers::SignalData<void()>(17, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'deleteContentAfterClosingChanged'
        QtMocHelpers::SignalData<void()>(18, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'backgroundColorChanged'
        QtMocHelpers::SignalData<void()>(19, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'borderColorChanged'
        QtMocHelpers::SignalData<void()>(20, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'contentMaskEnabledChanged'
        QtMocHelpers::SignalData<void()>(21, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'aboutToOpen'
        QtMocHelpers::SignalData<void()>(22, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'aboutToClose'
        QtMocHelpers::SignalData<void()>(23, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'opened'
        QtMocHelpers::SignalData<void()>(24, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'closed'
        QtMocHelpers::SignalData<void()>(25, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'pressed'
        QtMocHelpers::SignalData<void()>(26, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'released'
        QtMocHelpers::SignalData<void()>(27, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'manualPositionRequested'
        QtMocHelpers::SignalData<void()>(28, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'openPopover'
        QtMocHelpers::SlotData<void()>(29, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'closePopover'
        QtMocHelpers::SlotData<void()>(30, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'togglePopover'
        QtMocHelpers::SlotData<void()>(31, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'manualPositioning'
        QtMocHelpers::PropertyData<bool>(32, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 0),
        // property 'preferredPosition'
        QtMocHelpers::PropertyData<oclero::qlementine::Popover::Position>(33, 0x80000000 | 34, QMC::DefaultPropertyFlags | QMC::Writable | QMC::EnumOrFlag | QMC::StdCppSet, 1),
        // property 'preferredAlignment'
        QtMocHelpers::PropertyData<oclero::qlementine::Popover::Alignment>(35, 0x80000000 | 36, QMC::DefaultPropertyFlags | QMC::Writable | QMC::EnumOrFlag | QMC::StdCppSet, 2),
        // property 'isOpened'
        QtMocHelpers::PropertyData<bool>(37, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable, 4),
        // property 'padding'
        QtMocHelpers::PropertyData<QMargins>(38, 0x80000000 | 39, QMC::DefaultPropertyFlags | QMC::Writable | QMC::EnumOrFlag | QMC::StdCppSet, 5),
        // property 'horizontalSpacing'
        QtMocHelpers::PropertyData<int>(40, QMetaType::Int, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 9),
        // property 'verticalSpacing'
        QtMocHelpers::PropertyData<int>(41, QMetaType::Int, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 8),
        // property 'screenPadding'
        QtMocHelpers::PropertyData<QMargins>(42, 0x80000000 | 39, QMC::DefaultPropertyFlags | QMC::Writable | QMC::EnumOrFlag | QMC::StdCppSet, 6),
        // property 'dropShadowColor'
        QtMocHelpers::PropertyData<QColor>(43, 0x80000000 | 44, QMC::DefaultPropertyFlags | QMC::Writable | QMC::EnumOrFlag | QMC::StdCppSet, 10),
        // property 'radius'
        QtMocHelpers::PropertyData<qreal>(45, QMetaType::QReal, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 11),
        // property 'borderWidth'
        QtMocHelpers::PropertyData<qreal>(46, QMetaType::QReal, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 12),
        // property 'dropShadowRadius'
        QtMocHelpers::PropertyData<qreal>(47, QMetaType::QReal, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 13),
        // property 'dropShadowOffset'
        QtMocHelpers::PropertyData<QPointF>(48, 0x80000000 | 49, QMC::DefaultPropertyFlags | QMC::Writable | QMC::EnumOrFlag | QMC::StdCppSet, 14),
        // property 'canBeOverAnchor'
        QtMocHelpers::PropertyData<bool>(50, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 15),
        // property 'deleteContentAfterClosing'
        QtMocHelpers::PropertyData<bool>(51, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 16),
        // property 'contentMaskEnabled'
        QtMocHelpers::PropertyData<bool>(52, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 19),
        // property 'backgroundColor'
        QtMocHelpers::PropertyData<QColor>(53, 0x80000000 | 44, QMC::DefaultPropertyFlags | QMC::Writable | QMC::EnumOrFlag | QMC::StdCppSet, 17),
        // property 'borderColor'
        QtMocHelpers::PropertyData<QColor>(54, 0x80000000 | 44, QMC::DefaultPropertyFlags | QMC::Writable | QMC::EnumOrFlag | QMC::StdCppSet, 18),
    };
    QtMocHelpers::UintData qt_enums {
        // enum 'Position'
        QtMocHelpers::EnumData<enum Position>(55, 55, QMC::EnumIsScoped).add({
            {   56, Position::Left },
            {   57, Position::Top },
            {   58, Position::Right },
            {   59, Position::Bottom },
        }),
        // enum 'Alignment'
        QtMocHelpers::EnumData<enum Alignment>(60, 60, QMC::EnumIsScoped).add({
            {   61, Alignment::Begin },
            {   62, Alignment::Center },
            {   63, Alignment::End },
        }),
    };
    return QtMocHelpers::metaObjectData<Popover, qt_meta_tag_ZN6oclero10qlementine7PopoverE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject oclero::qlementine::Popover::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN6oclero10qlementine7PopoverE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN6oclero10qlementine7PopoverE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN6oclero10qlementine7PopoverE_t>.metaTypes,
    nullptr
} };

void oclero::qlementine::Popover::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<Popover *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->manualPositioningChanged(); break;
        case 1: _t->preferredPositionChanged(); break;
        case 2: _t->preferredAlignmentChanged(); break;
        case 3: _t->contentWidgetChanged(); break;
        case 4: _t->openedChanged(); break;
        case 5: _t->paddingChanged(); break;
        case 6: _t->screenPaddingChanged(); break;
        case 7: _t->anchorWidgetChanged(); break;
        case 8: _t->verticalSpacingChanged(); break;
        case 9: _t->horizontalSpacingChanged(); break;
        case 10: _t->dropShadowColorChanged(); break;
        case 11: _t->radiusChanged(); break;
        case 12: _t->borderWidthChanged(); break;
        case 13: _t->dropShadowRadiusChanged(); break;
        case 14: _t->dropShadowOffsetChanged(); break;
        case 15: _t->canBeOverAnchorChanged(); break;
        case 16: _t->deleteContentAfterClosingChanged(); break;
        case 17: _t->backgroundColorChanged(); break;
        case 18: _t->borderColorChanged(); break;
        case 19: _t->contentMaskEnabledChanged(); break;
        case 20: _t->aboutToOpen(); break;
        case 21: _t->aboutToClose(); break;
        case 22: _t->opened(); break;
        case 23: _t->closed(); break;
        case 24: _t->pressed(); break;
        case 25: _t->released(); break;
        case 26: _t->manualPositionRequested(); break;
        case 27: _t->openPopover(); break;
        case 28: _t->closePopover(); break;
        case 29: _t->togglePopover(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (Popover::*)()>(_a, &Popover::manualPositioningChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (Popover::*)()>(_a, &Popover::preferredPositionChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (Popover::*)()>(_a, &Popover::preferredAlignmentChanged, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (Popover::*)()>(_a, &Popover::contentWidgetChanged, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (Popover::*)()>(_a, &Popover::openedChanged, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (Popover::*)()>(_a, &Popover::paddingChanged, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (Popover::*)()>(_a, &Popover::screenPaddingChanged, 6))
            return;
        if (QtMocHelpers::indexOfMethod<void (Popover::*)()>(_a, &Popover::anchorWidgetChanged, 7))
            return;
        if (QtMocHelpers::indexOfMethod<void (Popover::*)()>(_a, &Popover::verticalSpacingChanged, 8))
            return;
        if (QtMocHelpers::indexOfMethod<void (Popover::*)()>(_a, &Popover::horizontalSpacingChanged, 9))
            return;
        if (QtMocHelpers::indexOfMethod<void (Popover::*)()>(_a, &Popover::dropShadowColorChanged, 10))
            return;
        if (QtMocHelpers::indexOfMethod<void (Popover::*)()>(_a, &Popover::radiusChanged, 11))
            return;
        if (QtMocHelpers::indexOfMethod<void (Popover::*)()>(_a, &Popover::borderWidthChanged, 12))
            return;
        if (QtMocHelpers::indexOfMethod<void (Popover::*)()>(_a, &Popover::dropShadowRadiusChanged, 13))
            return;
        if (QtMocHelpers::indexOfMethod<void (Popover::*)()>(_a, &Popover::dropShadowOffsetChanged, 14))
            return;
        if (QtMocHelpers::indexOfMethod<void (Popover::*)()>(_a, &Popover::canBeOverAnchorChanged, 15))
            return;
        if (QtMocHelpers::indexOfMethod<void (Popover::*)()>(_a, &Popover::deleteContentAfterClosingChanged, 16))
            return;
        if (QtMocHelpers::indexOfMethod<void (Popover::*)()>(_a, &Popover::backgroundColorChanged, 17))
            return;
        if (QtMocHelpers::indexOfMethod<void (Popover::*)()>(_a, &Popover::borderColorChanged, 18))
            return;
        if (QtMocHelpers::indexOfMethod<void (Popover::*)()>(_a, &Popover::contentMaskEnabledChanged, 19))
            return;
        if (QtMocHelpers::indexOfMethod<void (Popover::*)()>(_a, &Popover::aboutToOpen, 20))
            return;
        if (QtMocHelpers::indexOfMethod<void (Popover::*)()>(_a, &Popover::aboutToClose, 21))
            return;
        if (QtMocHelpers::indexOfMethod<void (Popover::*)()>(_a, &Popover::opened, 22))
            return;
        if (QtMocHelpers::indexOfMethod<void (Popover::*)()>(_a, &Popover::closed, 23))
            return;
        if (QtMocHelpers::indexOfMethod<void (Popover::*)()>(_a, &Popover::pressed, 24))
            return;
        if (QtMocHelpers::indexOfMethod<void (Popover::*)()>(_a, &Popover::released, 25))
            return;
        if (QtMocHelpers::indexOfMethod<void (Popover::*)()>(_a, &Popover::manualPositionRequested, 26))
            return;
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<bool*>(_v) = _t->manualPositioning(); break;
        case 1: *reinterpret_cast<oclero::qlementine::Popover::Position*>(_v) = _t->preferredPosition(); break;
        case 2: *reinterpret_cast<oclero::qlementine::Popover::Alignment*>(_v) = _t->preferredAlignment(); break;
        case 3: *reinterpret_cast<bool*>(_v) = _t->isOpened(); break;
        case 4: *reinterpret_cast<QMargins*>(_v) = _t->padding(); break;
        case 5: *reinterpret_cast<int*>(_v) = _t->horizontalSpacing(); break;
        case 6: *reinterpret_cast<int*>(_v) = _t->verticalSpacing(); break;
        case 7: *reinterpret_cast<QMargins*>(_v) = _t->screenPadding(); break;
        case 8: *reinterpret_cast<QColor*>(_v) = _t->dropShadowColor(); break;
        case 9: *reinterpret_cast<qreal*>(_v) = _t->radius(); break;
        case 10: *reinterpret_cast<qreal*>(_v) = _t->borderWidth(); break;
        case 11: *reinterpret_cast<qreal*>(_v) = _t->dropShadowRadius(); break;
        case 12: *reinterpret_cast<QPointF*>(_v) = _t->dropShadowOffset(); break;
        case 13: *reinterpret_cast<bool*>(_v) = _t->canBeOverAnchor(); break;
        case 14: *reinterpret_cast<bool*>(_v) = _t->deleteContentAfterClosing(); break;
        case 15: *reinterpret_cast<bool*>(_v) = _t->contentMaskEnabled(); break;
        case 16: *reinterpret_cast<QColor*>(_v) = _t->backgroundColor(); break;
        case 17: *reinterpret_cast<QColor*>(_v) = _t->borderColor(); break;
        default: break;
        }
    }
    if (_c == QMetaObject::WriteProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setManualPositioning(*reinterpret_cast<bool*>(_v)); break;
        case 1: _t->setPreferredPosition(*reinterpret_cast<oclero::qlementine::Popover::Position*>(_v)); break;
        case 2: _t->setPreferredAlignment(*reinterpret_cast<oclero::qlementine::Popover::Alignment*>(_v)); break;
        case 3: _t->setOpened(*reinterpret_cast<bool*>(_v)); break;
        case 4: _t->setPadding(*reinterpret_cast<QMargins*>(_v)); break;
        case 5: _t->setHorizontalSpacing(*reinterpret_cast<int*>(_v)); break;
        case 6: _t->setVerticalSpacing(*reinterpret_cast<int*>(_v)); break;
        case 7: _t->setScreenPadding(*reinterpret_cast<QMargins*>(_v)); break;
        case 8: _t->setDropShadowColor(*reinterpret_cast<QColor*>(_v)); break;
        case 9: _t->setRadius(*reinterpret_cast<qreal*>(_v)); break;
        case 10: _t->setBorderWidth(*reinterpret_cast<qreal*>(_v)); break;
        case 11: _t->setDropShadowRadius(*reinterpret_cast<qreal*>(_v)); break;
        case 12: _t->setDropShadowOffset(*reinterpret_cast<QPointF*>(_v)); break;
        case 13: _t->setCanBeOverAnchor(*reinterpret_cast<bool*>(_v)); break;
        case 14: _t->setDeleteContentAfterClosing(*reinterpret_cast<bool*>(_v)); break;
        case 15: _t->setContentMaskEnabled(*reinterpret_cast<bool*>(_v)); break;
        case 16: _t->setBackgroundColor(*reinterpret_cast<QColor*>(_v)); break;
        case 17: _t->setBorderColor(*reinterpret_cast<QColor*>(_v)); break;
        default: break;
        }
    }
}

const QMetaObject *oclero::qlementine::Popover::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *oclero::qlementine::Popover::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN6oclero10qlementine7PopoverE_t>.strings))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int oclero::qlementine::Popover::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 30)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 30;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 30)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 30;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 18;
    }
    return _id;
}

// SIGNAL 0
void oclero::qlementine::Popover::manualPositioningChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void oclero::qlementine::Popover::preferredPositionChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void oclero::qlementine::Popover::preferredAlignmentChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void oclero::qlementine::Popover::contentWidgetChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void oclero::qlementine::Popover::openedChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}

// SIGNAL 5
void oclero::qlementine::Popover::paddingChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 5, nullptr);
}

// SIGNAL 6
void oclero::qlementine::Popover::screenPaddingChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 6, nullptr);
}

// SIGNAL 7
void oclero::qlementine::Popover::anchorWidgetChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 7, nullptr);
}

// SIGNAL 8
void oclero::qlementine::Popover::verticalSpacingChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 8, nullptr);
}

// SIGNAL 9
void oclero::qlementine::Popover::horizontalSpacingChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 9, nullptr);
}

// SIGNAL 10
void oclero::qlementine::Popover::dropShadowColorChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 10, nullptr);
}

// SIGNAL 11
void oclero::qlementine::Popover::radiusChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 11, nullptr);
}

// SIGNAL 12
void oclero::qlementine::Popover::borderWidthChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 12, nullptr);
}

// SIGNAL 13
void oclero::qlementine::Popover::dropShadowRadiusChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 13, nullptr);
}

// SIGNAL 14
void oclero::qlementine::Popover::dropShadowOffsetChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 14, nullptr);
}

// SIGNAL 15
void oclero::qlementine::Popover::canBeOverAnchorChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 15, nullptr);
}

// SIGNAL 16
void oclero::qlementine::Popover::deleteContentAfterClosingChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 16, nullptr);
}

// SIGNAL 17
void oclero::qlementine::Popover::backgroundColorChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 17, nullptr);
}

// SIGNAL 18
void oclero::qlementine::Popover::borderColorChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 18, nullptr);
}

// SIGNAL 19
void oclero::qlementine::Popover::contentMaskEnabledChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 19, nullptr);
}

// SIGNAL 20
void oclero::qlementine::Popover::aboutToOpen()
{
    QMetaObject::activate(this, &staticMetaObject, 20, nullptr);
}

// SIGNAL 21
void oclero::qlementine::Popover::aboutToClose()
{
    QMetaObject::activate(this, &staticMetaObject, 21, nullptr);
}

// SIGNAL 22
void oclero::qlementine::Popover::opened()
{
    QMetaObject::activate(this, &staticMetaObject, 22, nullptr);
}

// SIGNAL 23
void oclero::qlementine::Popover::closed()
{
    QMetaObject::activate(this, &staticMetaObject, 23, nullptr);
}

// SIGNAL 24
void oclero::qlementine::Popover::pressed()
{
    QMetaObject::activate(this, &staticMetaObject, 24, nullptr);
}

// SIGNAL 25
void oclero::qlementine::Popover::released()
{
    QMetaObject::activate(this, &staticMetaObject, 25, nullptr);
}

// SIGNAL 26
void oclero::qlementine::Popover::manualPositionRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 26, nullptr);
}
QT_WARNING_POP
