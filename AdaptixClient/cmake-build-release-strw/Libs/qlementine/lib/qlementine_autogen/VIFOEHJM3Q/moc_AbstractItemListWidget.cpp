/****************************************************************************
** Meta object code from reading C++ file 'AbstractItemListWidget.hpp'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../../../Libs/qlementine/lib/include/oclero/qlementine/widgets/AbstractItemListWidget.hpp"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'AbstractItemListWidget.hpp' doesn't include <QObject>."
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
struct qt_meta_tag_ZN6oclero10qlementine22AbstractItemListWidgetE_t {};
} // unnamed namespace

template <> constexpr inline auto oclero::qlementine::AbstractItemListWidget::qt_create_metaobjectdata<qt_meta_tag_ZN6oclero10qlementine22AbstractItemListWidgetE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "oclero::qlementine::AbstractItemListWidget",
        "itemCountChanged",
        "",
        "currentIndexChanged",
        "currentDataChanged",
        "iconSizeChanged",
        "itemsShouldExpandChanged",
        "moveToNextItem",
        "moveToPreviousItem",
        "currentIndex",
        "currentData",
        "QVariant",
        "itemCount",
        "iconSize",
        "QSize",
        "itemsShouldExpand"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'itemCountChanged'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'currentIndexChanged'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'currentDataChanged'
        QtMocHelpers::SignalData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'iconSizeChanged'
        QtMocHelpers::SignalData<void()>(5, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'itemsShouldExpandChanged'
        QtMocHelpers::SignalData<void()>(6, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'moveToNextItem'
        QtMocHelpers::SlotData<void()>(7, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'moveToPreviousItem'
        QtMocHelpers::SlotData<void()>(8, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'currentIndex'
        QtMocHelpers::PropertyData<int>(9, QMetaType::Int, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 1),
        // property 'currentData'
        QtMocHelpers::PropertyData<QVariant>(10, 0x80000000 | 11, QMC::DefaultPropertyFlags | QMC::Writable | QMC::EnumOrFlag | QMC::StdCppSet, 2),
        // property 'itemCount'
        QtMocHelpers::PropertyData<int>(12, QMetaType::Int, QMC::DefaultPropertyFlags, 0),
        // property 'iconSize'
        QtMocHelpers::PropertyData<QSize>(13, 0x80000000 | 14, QMC::DefaultPropertyFlags | QMC::Writable | QMC::EnumOrFlag | QMC::StdCppSet, 3),
        // property 'itemsShouldExpand'
        QtMocHelpers::PropertyData<bool>(15, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 4),
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AbstractItemListWidget, qt_meta_tag_ZN6oclero10qlementine22AbstractItemListWidgetE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject oclero::qlementine::AbstractItemListWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN6oclero10qlementine22AbstractItemListWidgetE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN6oclero10qlementine22AbstractItemListWidgetE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN6oclero10qlementine22AbstractItemListWidgetE_t>.metaTypes,
    nullptr
} };

void oclero::qlementine::AbstractItemListWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AbstractItemListWidget *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->itemCountChanged(); break;
        case 1: _t->currentIndexChanged(); break;
        case 2: _t->currentDataChanged(); break;
        case 3: _t->iconSizeChanged(); break;
        case 4: _t->itemsShouldExpandChanged(); break;
        case 5: _t->moveToNextItem(); break;
        case 6: _t->moveToPreviousItem(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (AbstractItemListWidget::*)()>(_a, &AbstractItemListWidget::itemCountChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (AbstractItemListWidget::*)()>(_a, &AbstractItemListWidget::currentIndexChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (AbstractItemListWidget::*)()>(_a, &AbstractItemListWidget::currentDataChanged, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (AbstractItemListWidget::*)()>(_a, &AbstractItemListWidget::iconSizeChanged, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (AbstractItemListWidget::*)()>(_a, &AbstractItemListWidget::itemsShouldExpandChanged, 4))
            return;
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<int*>(_v) = _t->currentIndex(); break;
        case 1: *reinterpret_cast<QVariant*>(_v) = _t->currentData(); break;
        case 2: *reinterpret_cast<int*>(_v) = _t->itemCount(); break;
        case 3: *reinterpret_cast<QSize*>(_v) = _t->iconSize(); break;
        case 4: *reinterpret_cast<bool*>(_v) = _t->itemsShouldExpand(); break;
        default: break;
        }
    }
    if (_c == QMetaObject::WriteProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setCurrentIndex(*reinterpret_cast<int*>(_v)); break;
        case 1: _t->setCurrentData(*reinterpret_cast<QVariant*>(_v)); break;
        case 3: _t->setIconSize(*reinterpret_cast<QSize*>(_v)); break;
        case 4: _t->setItemsShouldExpand(*reinterpret_cast<bool*>(_v)); break;
        default: break;
        }
    }
}

const QMetaObject *oclero::qlementine::AbstractItemListWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *oclero::qlementine::AbstractItemListWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN6oclero10qlementine22AbstractItemListWidgetE_t>.strings))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int oclero::qlementine::AbstractItemListWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 7)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 7;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    }
    return _id;
}

// SIGNAL 0
void oclero::qlementine::AbstractItemListWidget::itemCountChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void oclero::qlementine::AbstractItemListWidget::currentIndexChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void oclero::qlementine::AbstractItemListWidget::currentDataChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void oclero::qlementine::AbstractItemListWidget::iconSizeChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void oclero::qlementine::AbstractItemListWidget::itemsShouldExpandChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}
QT_WARNING_POP
