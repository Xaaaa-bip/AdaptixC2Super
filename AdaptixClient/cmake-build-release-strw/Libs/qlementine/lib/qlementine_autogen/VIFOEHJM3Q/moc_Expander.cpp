/****************************************************************************
** Meta object code from reading C++ file 'Expander.hpp'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../../../Libs/qlementine/lib/include/oclero/qlementine/widgets/Expander.hpp"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'Expander.hpp' doesn't include <QObject>."
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
struct qt_meta_tag_ZN6oclero10qlementine8ExpanderE_t {};
} // unnamed namespace

template <> constexpr inline auto oclero::qlementine::Expander::qt_create_metaobjectdata<qt_meta_tag_ZN6oclero10qlementine8ExpanderE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "oclero::qlementine::Expander",
        "expandedChanged",
        "",
        "aboutToExpand",
        "aboutToShrink",
        "didExpand",
        "didShrink",
        "orientationChanged",
        "contentChanged",
        "setExpanded",
        "expanded",
        "setOrientation",
        "Qt::Orientation",
        "orientation"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'expandedChanged'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'aboutToExpand'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'aboutToShrink'
        QtMocHelpers::SignalData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'didExpand'
        QtMocHelpers::SignalData<void()>(5, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'didShrink'
        QtMocHelpers::SignalData<void()>(6, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'orientationChanged'
        QtMocHelpers::SignalData<void()>(7, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'contentChanged'
        QtMocHelpers::SignalData<void()>(8, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'setExpanded'
        QtMocHelpers::SlotData<void(bool)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 10 },
        }}),
        // Slot 'setOrientation'
        QtMocHelpers::SlotData<void(Qt::Orientation)>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 12, 13 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'expanded'
        QtMocHelpers::PropertyData<bool>(10, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 0),
        // property 'orientation'
        QtMocHelpers::PropertyData<Qt::Orientation>(13, 0x80000000 | 12, QMC::DefaultPropertyFlags | QMC::Writable | QMC::EnumOrFlag | QMC::StdCppSet, 5),
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<Expander, qt_meta_tag_ZN6oclero10qlementine8ExpanderE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject oclero::qlementine::Expander::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN6oclero10qlementine8ExpanderE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN6oclero10qlementine8ExpanderE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN6oclero10qlementine8ExpanderE_t>.metaTypes,
    nullptr
} };

void oclero::qlementine::Expander::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<Expander *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->expandedChanged(); break;
        case 1: _t->aboutToExpand(); break;
        case 2: _t->aboutToShrink(); break;
        case 3: _t->didExpand(); break;
        case 4: _t->didShrink(); break;
        case 5: _t->orientationChanged(); break;
        case 6: _t->contentChanged(); break;
        case 7: _t->setExpanded((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 8: _t->setOrientation((*reinterpret_cast<std::add_pointer_t<Qt::Orientation>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (Expander::*)()>(_a, &Expander::expandedChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (Expander::*)()>(_a, &Expander::aboutToExpand, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (Expander::*)()>(_a, &Expander::aboutToShrink, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (Expander::*)()>(_a, &Expander::didExpand, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (Expander::*)()>(_a, &Expander::didShrink, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (Expander::*)()>(_a, &Expander::orientationChanged, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (Expander::*)()>(_a, &Expander::contentChanged, 6))
            return;
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<bool*>(_v) = _t->expanded(); break;
        case 1: *reinterpret_cast<Qt::Orientation*>(_v) = _t->orientation(); break;
        default: break;
        }
    }
    if (_c == QMetaObject::WriteProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setExpanded(*reinterpret_cast<bool*>(_v)); break;
        case 1: _t->setOrientation(*reinterpret_cast<Qt::Orientation*>(_v)); break;
        default: break;
        }
    }
}

const QMetaObject *oclero::qlementine::Expander::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *oclero::qlementine::Expander::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN6oclero10qlementine8ExpanderE_t>.strings))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int oclero::qlementine::Expander::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 9)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 9;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 9)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 9;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 2;
    }
    return _id;
}

// SIGNAL 0
void oclero::qlementine::Expander::expandedChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void oclero::qlementine::Expander::aboutToExpand()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void oclero::qlementine::Expander::aboutToShrink()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void oclero::qlementine::Expander::didExpand()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void oclero::qlementine::Expander::didShrink()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}

// SIGNAL 5
void oclero::qlementine::Expander::orientationChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 5, nullptr);
}

// SIGNAL 6
void oclero::qlementine::Expander::contentChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 6, nullptr);
}
QT_WARNING_POP
