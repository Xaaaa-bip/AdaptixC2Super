/****************************************************************************
** Meta object code from reading C++ file 'ListenersWidget.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../Headers/UI/Widgets/ListenersWidget.h"
#include <QtGui/qtextcursor.h>
#include <QtNetwork/QSslError>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'ListenersWidget.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN25ListenersFilterProxyModelE_t {};
} // unnamed namespace

template <> constexpr inline auto ListenersFilterProxyModel::qt_create_metaobjectdata<qt_meta_tag_ZN25ListenersFilterProxyModelE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "ListenersFilterProxyModel"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<ListenersFilterProxyModel, qt_meta_tag_ZN25ListenersFilterProxyModelE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject ListenersFilterProxyModel::staticMetaObject = { {
    QMetaObject::SuperData::link<QSortFilterProxyModel::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN25ListenersFilterProxyModelE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN25ListenersFilterProxyModelE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN25ListenersFilterProxyModelE_t>.metaTypes,
    nullptr
} };

void ListenersFilterProxyModel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<ListenersFilterProxyModel *>(_o);
    (void)_t;
    (void)_c;
    (void)_id;
    (void)_a;
}

const QMetaObject *ListenersFilterProxyModel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ListenersFilterProxyModel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN25ListenersFilterProxyModelE_t>.strings))
        return static_cast<void*>(this);
    return QSortFilterProxyModel::qt_metacast(_clname);
}

int ListenersFilterProxyModel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QSortFilterProxyModel::qt_metacall(_c, _id, _a);
    return _id;
}
namespace {
struct qt_meta_tag_ZN19ListenersTableModelE_t {};
} // unnamed namespace

template <> constexpr inline auto ListenersTableModel::qt_create_metaobjectdata<qt_meta_tag_ZN19ListenersTableModelE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "ListenersTableModel"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<ListenersTableModel, qt_meta_tag_ZN19ListenersTableModelE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject ListenersTableModel::staticMetaObject = { {
    QMetaObject::SuperData::link<QAbstractTableModel::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN19ListenersTableModelE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN19ListenersTableModelE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN19ListenersTableModelE_t>.metaTypes,
    nullptr
} };

void ListenersTableModel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<ListenersTableModel *>(_o);
    (void)_t;
    (void)_c;
    (void)_id;
    (void)_a;
}

const QMetaObject *ListenersTableModel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ListenersTableModel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN19ListenersTableModelE_t>.strings))
        return static_cast<void*>(this);
    return QAbstractTableModel::qt_metacast(_clname);
}

int ListenersTableModel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QAbstractTableModel::qt_metacall(_c, _id, _a);
    return _id;
}
namespace {
struct qt_meta_tag_ZN15ListenersWidgetE_t {};
} // unnamed namespace

template <> constexpr inline auto ListenersWidget::qt_create_metaobjectdata<qt_meta_tag_ZN15ListenersWidgetE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "ListenersWidget",
        "toggleSearchPanel",
        "",
        "onFilterUpdate",
        "handleListenersMenu",
        "QPoint",
        "pos",
        "onCreateListener",
        "onEditListener",
        "onRemoveListener",
        "onPauseListener",
        "onResumeListener",
        "onGenerateAgent"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'toggleSearchPanel'
        QtMocHelpers::SlotData<void() const>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'onFilterUpdate'
        QtMocHelpers::SlotData<void() const>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'handleListenersMenu'
        QtMocHelpers::SlotData<void(const QPoint &) const>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 5, 6 },
        }}),
        // Slot 'onCreateListener'
        QtMocHelpers::SlotData<void() const>(7, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'onEditListener'
        QtMocHelpers::SlotData<void() const>(8, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'onRemoveListener'
        QtMocHelpers::SlotData<void() const>(9, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'onPauseListener'
        QtMocHelpers::SlotData<void() const>(10, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'onResumeListener'
        QtMocHelpers::SlotData<void() const>(11, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'onGenerateAgent'
        QtMocHelpers::SlotData<void() const>(12, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<ListenersWidget, qt_meta_tag_ZN15ListenersWidgetE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject ListenersWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<DockTab::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15ListenersWidgetE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15ListenersWidgetE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN15ListenersWidgetE_t>.metaTypes,
    nullptr
} };

void ListenersWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<ListenersWidget *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->toggleSearchPanel(); break;
        case 1: _t->onFilterUpdate(); break;
        case 2: _t->handleListenersMenu((*reinterpret_cast<std::add_pointer_t<QPoint>>(_a[1]))); break;
        case 3: _t->onCreateListener(); break;
        case 4: _t->onEditListener(); break;
        case 5: _t->onRemoveListener(); break;
        case 6: _t->onPauseListener(); break;
        case 7: _t->onResumeListener(); break;
        case 8: _t->onGenerateAgent(); break;
        default: ;
        }
    }
}

const QMetaObject *ListenersWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ListenersWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15ListenersWidgetE_t>.strings))
        return static_cast<void*>(this);
    return DockTab::qt_metacast(_clname);
}

int ListenersWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = DockTab::qt_metacall(_c, _id, _a);
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
    return _id;
}
QT_WARNING_POP
