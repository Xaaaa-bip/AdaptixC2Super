/****************************************************************************
** Meta object code from reading C++ file 'CredentialsWidget.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../Headers/UI/Widgets/CredentialsWidget.h"
#include <QtGui/qtextcursor.h>
#include <QtNetwork/QSslError>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'CredentialsWidget.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN21CredsFilterProxyModelE_t {};
} // unnamed namespace

template <> constexpr inline auto CredsFilterProxyModel::qt_create_metaobjectdata<qt_meta_tag_ZN21CredsFilterProxyModelE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "CredsFilterProxyModel"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<CredsFilterProxyModel, qt_meta_tag_ZN21CredsFilterProxyModelE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject CredsFilterProxyModel::staticMetaObject = { {
    QMetaObject::SuperData::link<QSortFilterProxyModel::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN21CredsFilterProxyModelE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN21CredsFilterProxyModelE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN21CredsFilterProxyModelE_t>.metaTypes,
    nullptr
} };

void CredsFilterProxyModel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<CredsFilterProxyModel *>(_o);
    (void)_t;
    (void)_c;
    (void)_id;
    (void)_a;
}

const QMetaObject *CredsFilterProxyModel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *CredsFilterProxyModel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN21CredsFilterProxyModelE_t>.strings))
        return static_cast<void*>(this);
    return QSortFilterProxyModel::qt_metacast(_clname);
}

int CredsFilterProxyModel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QSortFilterProxyModel::qt_metacall(_c, _id, _a);
    return _id;
}
namespace {
struct qt_meta_tag_ZN15CredsTableModelE_t {};
} // unnamed namespace

template <> constexpr inline auto CredsTableModel::qt_create_metaobjectdata<qt_meta_tag_ZN15CredsTableModelE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "CredsTableModel"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<CredsTableModel, qt_meta_tag_ZN15CredsTableModelE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject CredsTableModel::staticMetaObject = { {
    QMetaObject::SuperData::link<QAbstractTableModel::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15CredsTableModelE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15CredsTableModelE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN15CredsTableModelE_t>.metaTypes,
    nullptr
} };

void CredsTableModel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<CredsTableModel *>(_o);
    (void)_t;
    (void)_c;
    (void)_id;
    (void)_a;
}

const QMetaObject *CredsTableModel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *CredsTableModel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15CredsTableModelE_t>.strings))
        return static_cast<void*>(this);
    return QAbstractTableModel::qt_metacast(_clname);
}

int CredsTableModel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QAbstractTableModel::qt_metacall(_c, _id, _a);
    return _id;
}
namespace {
struct qt_meta_tag_ZN17CredentialsWidgetE_t {};
} // unnamed namespace

template <> constexpr inline auto CredentialsWidget::qt_create_metaobjectdata<qt_meta_tag_ZN17CredentialsWidgetE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "CredentialsWidget",
        "toggleSearchPanel",
        "",
        "onFilterUpdate",
        "onTypeFilterUpdate",
        "text",
        "onStorageFilterUpdate",
        "handleCredentialsMenu",
        "QPoint",
        "pos",
        "onCreateCreds",
        "onEditCreds",
        "onRemoveCreds",
        "onSetTag",
        "onExportCreds",
        "onCopyToClipboard"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'toggleSearchPanel'
        QtMocHelpers::SlotData<void() const>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'onFilterUpdate'
        QtMocHelpers::SlotData<void() const>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'onTypeFilterUpdate'
        QtMocHelpers::SlotData<void(const QString &) const>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 5 },
        }}),
        // Slot 'onStorageFilterUpdate'
        QtMocHelpers::SlotData<void(const QString &) const>(6, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 5 },
        }}),
        // Slot 'handleCredentialsMenu'
        QtMocHelpers::SlotData<void(const QPoint &) const>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 8, 9 },
        }}),
        // Slot 'onCreateCreds'
        QtMocHelpers::SlotData<void()>(10, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'onEditCreds'
        QtMocHelpers::SlotData<void() const>(11, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'onRemoveCreds'
        QtMocHelpers::SlotData<void() const>(12, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'onSetTag'
        QtMocHelpers::SlotData<void() const>(13, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'onExportCreds'
        QtMocHelpers::SlotData<void() const>(14, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'onCopyToClipboard'
        QtMocHelpers::SlotData<void() const>(15, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<CredentialsWidget, qt_meta_tag_ZN17CredentialsWidgetE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject CredentialsWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<DockTab::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17CredentialsWidgetE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17CredentialsWidgetE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN17CredentialsWidgetE_t>.metaTypes,
    nullptr
} };

void CredentialsWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<CredentialsWidget *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->toggleSearchPanel(); break;
        case 1: _t->onFilterUpdate(); break;
        case 2: _t->onTypeFilterUpdate((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 3: _t->onStorageFilterUpdate((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 4: _t->handleCredentialsMenu((*reinterpret_cast<std::add_pointer_t<QPoint>>(_a[1]))); break;
        case 5: _t->onCreateCreds(); break;
        case 6: _t->onEditCreds(); break;
        case 7: _t->onRemoveCreds(); break;
        case 8: _t->onSetTag(); break;
        case 9: _t->onExportCreds(); break;
        case 10: _t->onCopyToClipboard(); break;
        default: ;
        }
    }
}

const QMetaObject *CredentialsWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *CredentialsWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17CredentialsWidgetE_t>.strings))
        return static_cast<void*>(this);
    return DockTab::qt_metacast(_clname);
}

int CredentialsWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = DockTab::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 11)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 11;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 11)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 11;
    }
    return _id;
}
QT_WARNING_POP
