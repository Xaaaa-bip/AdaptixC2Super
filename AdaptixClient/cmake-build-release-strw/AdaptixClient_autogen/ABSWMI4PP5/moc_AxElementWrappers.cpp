/****************************************************************************
** Meta object code from reading C++ file 'AxElementWrappers.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../Headers/Client/AxScript/AxElementWrappers.h"
#include <QtGui/qtextcursor.h>
#include <QtNetwork/QSslError>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'AxElementWrappers.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN18AbstractAxMenuItemE_t {};
} // unnamed namespace

template <> constexpr inline auto AbstractAxMenuItem::qt_create_metaobjectdata<qt_meta_tag_ZN18AbstractAxMenuItemE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AbstractAxMenuItem"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AbstractAxMenuItem, qt_meta_tag_ZN18AbstractAxMenuItemE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AbstractAxMenuItem::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18AbstractAxMenuItemE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18AbstractAxMenuItemE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN18AbstractAxMenuItemE_t>.metaTypes,
    nullptr
} };

void AbstractAxMenuItem::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AbstractAxMenuItem *>(_o);
    (void)_t;
    (void)_c;
    (void)_id;
    (void)_a;
}

const QMetaObject *AbstractAxMenuItem::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AbstractAxMenuItem::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18AbstractAxMenuItemE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int AbstractAxMenuItem::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    return _id;
}
namespace {
struct qt_meta_tag_ZN15AxActionWrapperE_t {};
} // unnamed namespace

template <> constexpr inline auto AxActionWrapper::qt_create_metaobjectdata<qt_meta_tag_ZN15AxActionWrapperE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxActionWrapper"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxActionWrapper, qt_meta_tag_ZN15AxActionWrapperE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxActionWrapper::staticMetaObject = { {
    QMetaObject::SuperData::link<AbstractAxMenuItem::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15AxActionWrapperE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15AxActionWrapperE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN15AxActionWrapperE_t>.metaTypes,
    nullptr
} };

void AxActionWrapper::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxActionWrapper *>(_o);
    (void)_t;
    (void)_c;
    (void)_id;
    (void)_a;
}

const QMetaObject *AxActionWrapper::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxActionWrapper::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15AxActionWrapperE_t>.strings))
        return static_cast<void*>(this);
    return AbstractAxMenuItem::qt_metacast(_clname);
}

int AxActionWrapper::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = AbstractAxMenuItem::qt_metacall(_c, _id, _a);
    return _id;
}
namespace {
struct qt_meta_tag_ZN18AxSeparatorWrapperE_t {};
} // unnamed namespace

template <> constexpr inline auto AxSeparatorWrapper::qt_create_metaobjectdata<qt_meta_tag_ZN18AxSeparatorWrapperE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxSeparatorWrapper"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxSeparatorWrapper, qt_meta_tag_ZN18AxSeparatorWrapperE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxSeparatorWrapper::staticMetaObject = { {
    QMetaObject::SuperData::link<AbstractAxMenuItem::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18AxSeparatorWrapperE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18AxSeparatorWrapperE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN18AxSeparatorWrapperE_t>.metaTypes,
    nullptr
} };

void AxSeparatorWrapper::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxSeparatorWrapper *>(_o);
    (void)_t;
    (void)_c;
    (void)_id;
    (void)_a;
}

const QMetaObject *AxSeparatorWrapper::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxSeparatorWrapper::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18AxSeparatorWrapperE_t>.strings))
        return static_cast<void*>(this);
    return AbstractAxMenuItem::qt_metacast(_clname);
}

int AxSeparatorWrapper::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = AbstractAxMenuItem::qt_metacall(_c, _id, _a);
    return _id;
}
namespace {
struct qt_meta_tag_ZN13AxMenuWrapperE_t {};
} // unnamed namespace

template <> constexpr inline auto AxMenuWrapper::qt_create_metaobjectdata<qt_meta_tag_ZN13AxMenuWrapperE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxMenuWrapper",
        "addItem",
        "",
        "AbstractAxMenuItem*",
        "item"
    };

    QtMocHelpers::UintData qt_methods {
        // Method 'addItem'
        QtMocHelpers::MethodData<void(AbstractAxMenuItem *)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 3, 4 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxMenuWrapper, qt_meta_tag_ZN13AxMenuWrapperE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxMenuWrapper::staticMetaObject = { {
    QMetaObject::SuperData::link<AbstractAxMenuItem::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN13AxMenuWrapperE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN13AxMenuWrapperE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN13AxMenuWrapperE_t>.metaTypes,
    nullptr
} };

void AxMenuWrapper::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxMenuWrapper *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->addItem((*reinterpret_cast<std::add_pointer_t<AbstractAxMenuItem*>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 0:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< AbstractAxMenuItem* >(); break;
            }
            break;
        }
    }
}

const QMetaObject *AxMenuWrapper::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxMenuWrapper::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN13AxMenuWrapperE_t>.strings))
        return static_cast<void*>(this);
    return AbstractAxMenuItem::qt_metacast(_clname);
}

int AxMenuWrapper::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = AbstractAxMenuItem::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 1)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 1;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 1)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 1;
    }
    return _id;
}
namespace {
struct qt_meta_tag_ZN18AxBoxLayoutWrapperE_t {};
} // unnamed namespace

template <> constexpr inline auto AxBoxLayoutWrapper::qt_create_metaobjectdata<qt_meta_tag_ZN18AxBoxLayoutWrapperE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxBoxLayoutWrapper",
        "addWidget",
        "",
        "widgetWrapper"
    };

    QtMocHelpers::UintData qt_methods {
        // Method 'addWidget'
        QtMocHelpers::MethodData<void(QObject *) const>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QObjectStar, 3 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxBoxLayoutWrapper, qt_meta_tag_ZN18AxBoxLayoutWrapperE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxBoxLayoutWrapper::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18AxBoxLayoutWrapperE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18AxBoxLayoutWrapperE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN18AxBoxLayoutWrapperE_t>.metaTypes,
    nullptr
} };

void AxBoxLayoutWrapper::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxBoxLayoutWrapper *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->addWidget((*reinterpret_cast<std::add_pointer_t<QObject*>>(_a[1]))); break;
        default: ;
        }
    }
}

const QMetaObject *AxBoxLayoutWrapper::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxBoxLayoutWrapper::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18AxBoxLayoutWrapperE_t>.strings))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "AbstractAxLayout"))
        return static_cast< AbstractAxLayout*>(this);
    return QObject::qt_metacast(_clname);
}

int AxBoxLayoutWrapper::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 1)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 1;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 1)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 1;
    }
    return _id;
}
namespace {
struct qt_meta_tag_ZN19AxGridLayoutWrapperE_t {};
} // unnamed namespace

template <> constexpr inline auto AxGridLayoutWrapper::qt_create_metaobjectdata<qt_meta_tag_ZN19AxGridLayoutWrapperE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxGridLayoutWrapper",
        "addWidget",
        "",
        "widgetWrapper",
        "row",
        "col",
        "rowSpan",
        "colSpan"
    };

    QtMocHelpers::UintData qt_methods {
        // Method 'addWidget'
        QtMocHelpers::MethodData<void(QObject *, int, int, int, int) const>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QObjectStar, 3 }, { QMetaType::Int, 4 }, { QMetaType::Int, 5 }, { QMetaType::Int, 6 },
            { QMetaType::Int, 7 },
        }}),
        // Method 'addWidget'
        QtMocHelpers::MethodData<void(QObject *, int, int, int) const>(1, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::QObjectStar, 3 }, { QMetaType::Int, 4 }, { QMetaType::Int, 5 }, { QMetaType::Int, 6 },
        }}),
        // Method 'addWidget'
        QtMocHelpers::MethodData<void(QObject *, int, int) const>(1, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::QObjectStar, 3 }, { QMetaType::Int, 4 }, { QMetaType::Int, 5 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxGridLayoutWrapper, qt_meta_tag_ZN19AxGridLayoutWrapperE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxGridLayoutWrapper::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN19AxGridLayoutWrapperE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN19AxGridLayoutWrapperE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN19AxGridLayoutWrapperE_t>.metaTypes,
    nullptr
} };

void AxGridLayoutWrapper::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxGridLayoutWrapper *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->addWidget((*reinterpret_cast<std::add_pointer_t<QObject*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[5]))); break;
        case 1: _t->addWidget((*reinterpret_cast<std::add_pointer_t<QObject*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[4]))); break;
        case 2: _t->addWidget((*reinterpret_cast<std::add_pointer_t<QObject*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3]))); break;
        default: ;
        }
    }
}

const QMetaObject *AxGridLayoutWrapper::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxGridLayoutWrapper::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN19AxGridLayoutWrapperE_t>.strings))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "AbstractAxLayout"))
        return static_cast< AbstractAxLayout*>(this);
    return QObject::qt_metacast(_clname);
}

int AxGridLayoutWrapper::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 3)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 3)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 3;
    }
    return _id;
}
namespace {
struct qt_meta_tag_ZN13AxLineWrapperE_t {};
} // unnamed namespace

template <> constexpr inline auto AxLineWrapper::qt_create_metaobjectdata<qt_meta_tag_ZN13AxLineWrapperE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxLineWrapper",
        "setEnabled",
        "",
        "enable",
        "setVisible",
        "getEnabled",
        "getVisible"
    };

    QtMocHelpers::UintData qt_methods {
        // Method 'setEnabled'
        QtMocHelpers::MethodData<void(const bool) const>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 3 },
        }}),
        // Method 'setVisible'
        QtMocHelpers::MethodData<void(const bool) const>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 3 },
        }}),
        // Method 'getEnabled'
        QtMocHelpers::MethodData<bool() const>(5, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'getVisible'
        QtMocHelpers::MethodData<bool() const>(6, 2, QMC::AccessPublic, QMetaType::Bool),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxLineWrapper, qt_meta_tag_ZN13AxLineWrapperE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxLineWrapper::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN13AxLineWrapperE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN13AxLineWrapperE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN13AxLineWrapperE_t>.metaTypes,
    nullptr
} };

void AxLineWrapper::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxLineWrapper *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->setEnabled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 1: _t->setVisible((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 2: { bool _r = _t->getEnabled();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 3: { bool _r = _t->getVisible();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        default: ;
        }
    }
}

const QMetaObject *AxLineWrapper::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxLineWrapper::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN13AxLineWrapperE_t>.strings))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "AbstractAxVisualElement"))
        return static_cast< AbstractAxVisualElement*>(this);
    return QObject::qt_metacast(_clname);
}

int AxLineWrapper::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 4)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 4;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 4)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 4;
    }
    return _id;
}
namespace {
struct qt_meta_tag_ZN15AxSpacerWrapperE_t {};
} // unnamed namespace

template <> constexpr inline auto AxSpacerWrapper::qt_create_metaobjectdata<qt_meta_tag_ZN15AxSpacerWrapperE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxSpacerWrapper"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxSpacerWrapper, qt_meta_tag_ZN15AxSpacerWrapperE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxSpacerWrapper::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15AxSpacerWrapperE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15AxSpacerWrapperE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN15AxSpacerWrapperE_t>.metaTypes,
    nullptr
} };

void AxSpacerWrapper::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxSpacerWrapper *>(_o);
    (void)_t;
    (void)_c;
    (void)_id;
    (void)_a;
}

const QMetaObject *AxSpacerWrapper::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxSpacerWrapper::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15AxSpacerWrapperE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int AxSpacerWrapper::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    return _id;
}
namespace {
struct qt_meta_tag_ZN17AxTextLineWrapperE_t {};
} // unnamed namespace

template <> constexpr inline auto AxTextLineWrapper::qt_create_metaobjectdata<qt_meta_tag_ZN17AxTextLineWrapperE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxTextLineWrapper",
        "textChanged",
        "",
        "text",
        "textEdited",
        "returnPressed",
        "editingFinished",
        "setEnabled",
        "enable",
        "setVisible",
        "getEnabled",
        "getVisible",
        "setText",
        "setPlaceholder",
        "setReadOnly",
        "readonly"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'textChanged'
        QtMocHelpers::SignalData<void(const QString &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Signal 'textEdited'
        QtMocHelpers::SignalData<void(const QString &)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Signal 'returnPressed'
        QtMocHelpers::SignalData<void()>(5, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'editingFinished'
        QtMocHelpers::SignalData<void()>(6, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'setEnabled'
        QtMocHelpers::MethodData<void(const bool) const>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 8 },
        }}),
        // Method 'setVisible'
        QtMocHelpers::MethodData<void(const bool) const>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 8 },
        }}),
        // Method 'getEnabled'
        QtMocHelpers::MethodData<bool() const>(10, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'getVisible'
        QtMocHelpers::MethodData<bool() const>(11, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'text'
        QtMocHelpers::MethodData<QString() const>(3, 2, QMC::AccessPublic, QMetaType::QString),
        // Method 'setText'
        QtMocHelpers::MethodData<void(const QString &) const>(12, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Method 'setPlaceholder'
        QtMocHelpers::MethodData<void(const QString &) const>(13, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Method 'setReadOnly'
        QtMocHelpers::MethodData<void(const bool &) const>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 15 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxTextLineWrapper, qt_meta_tag_ZN17AxTextLineWrapperE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxTextLineWrapper::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AxTextLineWrapperE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AxTextLineWrapperE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN17AxTextLineWrapperE_t>.metaTypes,
    nullptr
} };

void AxTextLineWrapper::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxTextLineWrapper *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->textChanged((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 1: _t->textEdited((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 2: _t->returnPressed(); break;
        case 3: _t->editingFinished(); break;
        case 4: _t->setEnabled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 5: _t->setVisible((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 6: { bool _r = _t->getEnabled();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 7: { bool _r = _t->getVisible();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 8: { QString _r = _t->text();
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 9: _t->setText((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 10: _t->setPlaceholder((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 11: _t->setReadOnly((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (AxTextLineWrapper::*)(const QString & )>(_a, &AxTextLineWrapper::textChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (AxTextLineWrapper::*)(const QString & )>(_a, &AxTextLineWrapper::textEdited, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (AxTextLineWrapper::*)()>(_a, &AxTextLineWrapper::returnPressed, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (AxTextLineWrapper::*)()>(_a, &AxTextLineWrapper::editingFinished, 3))
            return;
    }
}

const QMetaObject *AxTextLineWrapper::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxTextLineWrapper::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AxTextLineWrapperE_t>.strings))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "AbstractAxElement"))
        return static_cast< AbstractAxElement*>(this);
    if (!strcmp(_clname, "AbstractAxVisualElement"))
        return static_cast< AbstractAxVisualElement*>(this);
    return QObject::qt_metacast(_clname);
}

int AxTextLineWrapper::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 12)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 12;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 12)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 12;
    }
    return _id;
}

// SIGNAL 0
void AxTextLineWrapper::textChanged(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void AxTextLineWrapper::textEdited(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void AxTextLineWrapper::returnPressed()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void AxTextLineWrapper::editingFinished()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}
namespace {
struct qt_meta_tag_ZN17AxComboBoxWrapperE_t {};
} // unnamed namespace

template <> constexpr inline auto AxComboBoxWrapper::qt_create_metaobjectdata<qt_meta_tag_ZN17AxComboBoxWrapperE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxComboBoxWrapper",
        "currentTextChanged",
        "",
        "text",
        "currentIndexChanged",
        "index",
        "setEnabled",
        "enable",
        "setVisible",
        "getEnabled",
        "getVisible",
        "addItem",
        "addItems",
        "QJSValue",
        "array",
        "setItems",
        "clear",
        "currentText",
        "setCurrentIndex",
        "currentIndex"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'currentTextChanged'
        QtMocHelpers::SignalData<void(const QString &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Signal 'currentIndexChanged'
        QtMocHelpers::SignalData<void(int)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 5 },
        }}),
        // Method 'setEnabled'
        QtMocHelpers::MethodData<void(const bool) const>(6, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 7 },
        }}),
        // Method 'setVisible'
        QtMocHelpers::MethodData<void(const bool) const>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 7 },
        }}),
        // Method 'getEnabled'
        QtMocHelpers::MethodData<bool() const>(9, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'getVisible'
        QtMocHelpers::MethodData<bool() const>(10, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'addItem'
        QtMocHelpers::MethodData<void(const QString &) const>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Method 'addItems'
        QtMocHelpers::MethodData<void(const QJSValue &) const>(12, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 13, 14 },
        }}),
        // Method 'setItems'
        QtMocHelpers::MethodData<void(const QJSValue &) const>(15, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 13, 14 },
        }}),
        // Method 'clear'
        QtMocHelpers::MethodData<void() const>(16, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'currentText'
        QtMocHelpers::MethodData<QString() const>(17, 2, QMC::AccessPublic, QMetaType::QString),
        // Method 'setCurrentIndex'
        QtMocHelpers::MethodData<void(int) const>(18, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 5 },
        }}),
        // Method 'currentIndex'
        QtMocHelpers::MethodData<int() const>(19, 2, QMC::AccessPublic, QMetaType::Int),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxComboBoxWrapper, qt_meta_tag_ZN17AxComboBoxWrapperE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxComboBoxWrapper::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AxComboBoxWrapperE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AxComboBoxWrapperE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN17AxComboBoxWrapperE_t>.metaTypes,
    nullptr
} };

void AxComboBoxWrapper::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxComboBoxWrapper *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->currentTextChanged((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 1: _t->currentIndexChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 2: _t->setEnabled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 3: _t->setVisible((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 4: { bool _r = _t->getEnabled();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 5: { bool _r = _t->getVisible();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 6: _t->addItem((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 7: _t->addItems((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1]))); break;
        case 8: _t->setItems((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1]))); break;
        case 9: _t->clear(); break;
        case 10: { QString _r = _t->currentText();
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 11: _t->setCurrentIndex((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 12: { int _r = _t->currentIndex();
            if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
        default: ;
        }
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 7:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 8:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (AxComboBoxWrapper::*)(const QString & )>(_a, &AxComboBoxWrapper::currentTextChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (AxComboBoxWrapper::*)(int )>(_a, &AxComboBoxWrapper::currentIndexChanged, 1))
            return;
    }
}

const QMetaObject *AxComboBoxWrapper::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxComboBoxWrapper::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AxComboBoxWrapperE_t>.strings))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "AbstractAxElement"))
        return static_cast< AbstractAxElement*>(this);
    if (!strcmp(_clname, "AbstractAxVisualElement"))
        return static_cast< AbstractAxVisualElement*>(this);
    return QObject::qt_metacast(_clname);
}

int AxComboBoxWrapper::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 13)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 13;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 13)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 13;
    }
    return _id;
}

// SIGNAL 0
void AxComboBoxWrapper::currentTextChanged(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void AxComboBoxWrapper::currentIndexChanged(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}
namespace {
struct qt_meta_tag_ZN16AxSpinBoxWrapperE_t {};
} // unnamed namespace

template <> constexpr inline auto AxSpinBoxWrapper::qt_create_metaobjectdata<qt_meta_tag_ZN16AxSpinBoxWrapperE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxSpinBoxWrapper",
        "valueChanged",
        "",
        "setEnabled",
        "enable",
        "setVisible",
        "getEnabled",
        "getVisible",
        "value",
        "setValue",
        "setRange",
        "min",
        "max"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'valueChanged'
        QtMocHelpers::SignalData<void(int)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 2 },
        }}),
        // Method 'setEnabled'
        QtMocHelpers::MethodData<void(const bool) const>(3, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 4 },
        }}),
        // Method 'setVisible'
        QtMocHelpers::MethodData<void(const bool) const>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 4 },
        }}),
        // Method 'getEnabled'
        QtMocHelpers::MethodData<bool() const>(6, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'getVisible'
        QtMocHelpers::MethodData<bool() const>(7, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'value'
        QtMocHelpers::MethodData<int() const>(8, 2, QMC::AccessPublic, QMetaType::Int),
        // Method 'setValue'
        QtMocHelpers::MethodData<void(int) const>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 8 },
        }}),
        // Method 'setRange'
        QtMocHelpers::MethodData<void(int, int) const>(10, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 11 }, { QMetaType::Int, 12 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxSpinBoxWrapper, qt_meta_tag_ZN16AxSpinBoxWrapperE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxSpinBoxWrapper::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN16AxSpinBoxWrapperE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN16AxSpinBoxWrapperE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN16AxSpinBoxWrapperE_t>.metaTypes,
    nullptr
} };

void AxSpinBoxWrapper::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxSpinBoxWrapper *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->valueChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 1: _t->setEnabled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 2: _t->setVisible((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 3: { bool _r = _t->getEnabled();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 4: { bool _r = _t->getVisible();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 5: { int _r = _t->value();
            if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
        case 6: _t->setValue((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 7: _t->setRange((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (AxSpinBoxWrapper::*)(int )>(_a, &AxSpinBoxWrapper::valueChanged, 0))
            return;
    }
}

const QMetaObject *AxSpinBoxWrapper::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxSpinBoxWrapper::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN16AxSpinBoxWrapperE_t>.strings))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "AbstractAxElement"))
        return static_cast< AbstractAxElement*>(this);
    if (!strcmp(_clname, "AbstractAxVisualElement"))
        return static_cast< AbstractAxVisualElement*>(this);
    return QObject::qt_metacast(_clname);
}

int AxSpinBoxWrapper::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 8)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 8;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 8)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 8;
    }
    return _id;
}

// SIGNAL 0
void AxSpinBoxWrapper::valueChanged(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}
namespace {
struct qt_meta_tag_ZN17AxDateEditWrapperE_t {};
} // unnamed namespace

template <> constexpr inline auto AxDateEditWrapper::qt_create_metaobjectdata<qt_meta_tag_ZN17AxDateEditWrapperE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxDateEditWrapper",
        "setEnabled",
        "",
        "enable",
        "setVisible",
        "getEnabled",
        "getVisible",
        "dateString",
        "setDateString",
        "date"
    };

    QtMocHelpers::UintData qt_methods {
        // Method 'setEnabled'
        QtMocHelpers::MethodData<void(const bool) const>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 3 },
        }}),
        // Method 'setVisible'
        QtMocHelpers::MethodData<void(const bool) const>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 3 },
        }}),
        // Method 'getEnabled'
        QtMocHelpers::MethodData<bool() const>(5, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'getVisible'
        QtMocHelpers::MethodData<bool() const>(6, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'dateString'
        QtMocHelpers::MethodData<QString() const>(7, 2, QMC::AccessPublic, QMetaType::QString),
        // Method 'setDateString'
        QtMocHelpers::MethodData<void(const QString &) const>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 9 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxDateEditWrapper, qt_meta_tag_ZN17AxDateEditWrapperE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxDateEditWrapper::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AxDateEditWrapperE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AxDateEditWrapperE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN17AxDateEditWrapperE_t>.metaTypes,
    nullptr
} };

void AxDateEditWrapper::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxDateEditWrapper *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->setEnabled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 1: _t->setVisible((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 2: { bool _r = _t->getEnabled();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 3: { bool _r = _t->getVisible();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 4: { QString _r = _t->dateString();
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 5: _t->setDateString((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        default: ;
        }
    }
}

const QMetaObject *AxDateEditWrapper::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxDateEditWrapper::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AxDateEditWrapperE_t>.strings))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "AbstractAxElement"))
        return static_cast< AbstractAxElement*>(this);
    if (!strcmp(_clname, "AbstractAxVisualElement"))
        return static_cast< AbstractAxVisualElement*>(this);
    return QObject::qt_metacast(_clname);
}

int AxDateEditWrapper::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 6)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 6;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 6)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 6;
    }
    return _id;
}
namespace {
struct qt_meta_tag_ZN17AxTimeEditWrapperE_t {};
} // unnamed namespace

template <> constexpr inline auto AxTimeEditWrapper::qt_create_metaobjectdata<qt_meta_tag_ZN17AxTimeEditWrapperE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxTimeEditWrapper",
        "setEnabled",
        "",
        "enable",
        "setVisible",
        "getEnabled",
        "getVisible",
        "timeString",
        "setTimeString",
        "time"
    };

    QtMocHelpers::UintData qt_methods {
        // Method 'setEnabled'
        QtMocHelpers::MethodData<void(const bool) const>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 3 },
        }}),
        // Method 'setVisible'
        QtMocHelpers::MethodData<void(const bool) const>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 3 },
        }}),
        // Method 'getEnabled'
        QtMocHelpers::MethodData<bool() const>(5, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'getVisible'
        QtMocHelpers::MethodData<bool() const>(6, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'timeString'
        QtMocHelpers::MethodData<QString() const>(7, 2, QMC::AccessPublic, QMetaType::QString),
        // Method 'setTimeString'
        QtMocHelpers::MethodData<void(const QString &) const>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 9 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxTimeEditWrapper, qt_meta_tag_ZN17AxTimeEditWrapperE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxTimeEditWrapper::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AxTimeEditWrapperE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AxTimeEditWrapperE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN17AxTimeEditWrapperE_t>.metaTypes,
    nullptr
} };

void AxTimeEditWrapper::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxTimeEditWrapper *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->setEnabled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 1: _t->setVisible((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 2: { bool _r = _t->getEnabled();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 3: { bool _r = _t->getVisible();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 4: { QString _r = _t->timeString();
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 5: _t->setTimeString((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        default: ;
        }
    }
}

const QMetaObject *AxTimeEditWrapper::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxTimeEditWrapper::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AxTimeEditWrapperE_t>.strings))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "AbstractAxElement"))
        return static_cast< AbstractAxElement*>(this);
    if (!strcmp(_clname, "AbstractAxVisualElement"))
        return static_cast< AbstractAxVisualElement*>(this);
    return QObject::qt_metacast(_clname);
}

int AxTimeEditWrapper::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 6)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 6;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 6)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 6;
    }
    return _id;
}
namespace {
struct qt_meta_tag_ZN18AxTextMultiWrapperE_t {};
} // unnamed namespace

template <> constexpr inline auto AxTextMultiWrapper::qt_create_metaobjectdata<qt_meta_tag_ZN18AxTextMultiWrapperE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxTextMultiWrapper",
        "setEnabled",
        "",
        "enable",
        "setVisible",
        "getEnabled",
        "getVisible",
        "text",
        "setText",
        "appendText",
        "setPlaceholder",
        "setReadOnly",
        "readonly"
    };

    QtMocHelpers::UintData qt_methods {
        // Method 'setEnabled'
        QtMocHelpers::MethodData<void(const bool) const>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 3 },
        }}),
        // Method 'setVisible'
        QtMocHelpers::MethodData<void(const bool) const>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 3 },
        }}),
        // Method 'getEnabled'
        QtMocHelpers::MethodData<bool() const>(5, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'getVisible'
        QtMocHelpers::MethodData<bool() const>(6, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'text'
        QtMocHelpers::MethodData<QString() const>(7, 2, QMC::AccessPublic, QMetaType::QString),
        // Method 'setText'
        QtMocHelpers::MethodData<void(const QString &) const>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 7 },
        }}),
        // Method 'appendText'
        QtMocHelpers::MethodData<void(const QString &) const>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 7 },
        }}),
        // Method 'setPlaceholder'
        QtMocHelpers::MethodData<void(const QString &) const>(10, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 7 },
        }}),
        // Method 'setReadOnly'
        QtMocHelpers::MethodData<void(const bool &) const>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 12 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxTextMultiWrapper, qt_meta_tag_ZN18AxTextMultiWrapperE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxTextMultiWrapper::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18AxTextMultiWrapperE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18AxTextMultiWrapperE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN18AxTextMultiWrapperE_t>.metaTypes,
    nullptr
} };

void AxTextMultiWrapper::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxTextMultiWrapper *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->setEnabled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 1: _t->setVisible((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 2: { bool _r = _t->getEnabled();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 3: { bool _r = _t->getVisible();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 4: { QString _r = _t->text();
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 5: _t->setText((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 6: _t->appendText((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 7: _t->setPlaceholder((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 8: _t->setReadOnly((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        default: ;
        }
    }
}

const QMetaObject *AxTextMultiWrapper::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxTextMultiWrapper::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18AxTextMultiWrapperE_t>.strings))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "AbstractAxElement"))
        return static_cast< AbstractAxElement*>(this);
    if (!strcmp(_clname, "AbstractAxVisualElement"))
        return static_cast< AbstractAxVisualElement*>(this);
    return QObject::qt_metacast(_clname);
}

int AxTextMultiWrapper::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
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
namespace {
struct qt_meta_tag_ZN17AxCheckBoxWrapperE_t {};
} // unnamed namespace

template <> constexpr inline auto AxCheckBoxWrapper::qt_create_metaobjectdata<qt_meta_tag_ZN17AxCheckBoxWrapperE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxCheckBoxWrapper",
        "stateChanged",
        "",
        "toggled",
        "checked",
        "setEnabled",
        "enable",
        "setVisible",
        "getEnabled",
        "getVisible",
        "isChecked",
        "setChecked"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'stateChanged'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'toggled'
        QtMocHelpers::SignalData<void(bool)>(3, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 4 },
        }}),
        // Method 'setEnabled'
        QtMocHelpers::MethodData<void(const bool) const>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 6 },
        }}),
        // Method 'setVisible'
        QtMocHelpers::MethodData<void(const bool) const>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 6 },
        }}),
        // Method 'getEnabled'
        QtMocHelpers::MethodData<bool() const>(8, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'getVisible'
        QtMocHelpers::MethodData<bool() const>(9, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'isChecked'
        QtMocHelpers::MethodData<bool() const>(10, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'checked'
        QtMocHelpers::MethodData<bool() const>(4, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'setChecked'
        QtMocHelpers::MethodData<void(bool) const>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 4 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxCheckBoxWrapper, qt_meta_tag_ZN17AxCheckBoxWrapperE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxCheckBoxWrapper::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AxCheckBoxWrapperE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AxCheckBoxWrapperE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN17AxCheckBoxWrapperE_t>.metaTypes,
    nullptr
} };

void AxCheckBoxWrapper::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxCheckBoxWrapper *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->stateChanged(); break;
        case 1: _t->toggled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 2: _t->setEnabled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 3: _t->setVisible((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 4: { bool _r = _t->getEnabled();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 5: { bool _r = _t->getVisible();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 6: { bool _r = _t->isChecked();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 7: { bool _r = _t->checked();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 8: _t->setChecked((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (AxCheckBoxWrapper::*)()>(_a, &AxCheckBoxWrapper::stateChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (AxCheckBoxWrapper::*)(bool )>(_a, &AxCheckBoxWrapper::toggled, 1))
            return;
    }
}

const QMetaObject *AxCheckBoxWrapper::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxCheckBoxWrapper::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AxCheckBoxWrapperE_t>.strings))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "AbstractAxElement"))
        return static_cast< AbstractAxElement*>(this);
    if (!strcmp(_clname, "AbstractAxVisualElement"))
        return static_cast< AbstractAxVisualElement*>(this);
    return QObject::qt_metacast(_clname);
}

int AxCheckBoxWrapper::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
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

// SIGNAL 0
void AxCheckBoxWrapper::stateChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void AxCheckBoxWrapper::toggled(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}
namespace {
struct qt_meta_tag_ZN14AxLabelWrapperE_t {};
} // unnamed namespace

template <> constexpr inline auto AxLabelWrapper::qt_create_metaobjectdata<qt_meta_tag_ZN14AxLabelWrapperE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxLabelWrapper",
        "setEnabled",
        "",
        "enable",
        "setVisible",
        "getEnabled",
        "getVisible",
        "setText",
        "text"
    };

    QtMocHelpers::UintData qt_methods {
        // Method 'setEnabled'
        QtMocHelpers::MethodData<void(const bool) const>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 3 },
        }}),
        // Method 'setVisible'
        QtMocHelpers::MethodData<void(const bool) const>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 3 },
        }}),
        // Method 'getEnabled'
        QtMocHelpers::MethodData<bool() const>(5, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'getVisible'
        QtMocHelpers::MethodData<bool() const>(6, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'setText'
        QtMocHelpers::MethodData<void(const QString &) const>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 8 },
        }}),
        // Method 'text'
        QtMocHelpers::MethodData<QString() const>(8, 2, QMC::AccessPublic, QMetaType::QString),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxLabelWrapper, qt_meta_tag_ZN14AxLabelWrapperE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxLabelWrapper::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14AxLabelWrapperE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14AxLabelWrapperE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN14AxLabelWrapperE_t>.metaTypes,
    nullptr
} };

void AxLabelWrapper::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxLabelWrapper *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->setEnabled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 1: _t->setVisible((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 2: { bool _r = _t->getEnabled();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 3: { bool _r = _t->getVisible();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 4: _t->setText((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 5: { QString _r = _t->text();
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        default: ;
        }
    }
}

const QMetaObject *AxLabelWrapper::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxLabelWrapper::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14AxLabelWrapperE_t>.strings))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "AbstractAxVisualElement"))
        return static_cast< AbstractAxVisualElement*>(this);
    return QObject::qt_metacast(_clname);
}

int AxLabelWrapper::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 6)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 6;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 6)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 6;
    }
    return _id;
}
namespace {
struct qt_meta_tag_ZN12AxTabWrapperE_t {};
} // unnamed namespace

template <> constexpr inline auto AxTabWrapper::qt_create_metaobjectdata<qt_meta_tag_ZN12AxTabWrapperE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxTabWrapper",
        "setEnabled",
        "",
        "enable",
        "setVisible",
        "getEnabled",
        "getVisible",
        "addTab",
        "wrapper",
        "title"
    };

    QtMocHelpers::UintData qt_methods {
        // Method 'setEnabled'
        QtMocHelpers::MethodData<void(const bool) const>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 3 },
        }}),
        // Method 'setVisible'
        QtMocHelpers::MethodData<void(const bool) const>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 3 },
        }}),
        // Method 'getEnabled'
        QtMocHelpers::MethodData<bool() const>(5, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'getVisible'
        QtMocHelpers::MethodData<bool() const>(6, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'addTab'
        QtMocHelpers::MethodData<void(QObject *, const QString &) const>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QObjectStar, 8 }, { QMetaType::QString, 9 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxTabWrapper, qt_meta_tag_ZN12AxTabWrapperE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxTabWrapper::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12AxTabWrapperE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12AxTabWrapperE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN12AxTabWrapperE_t>.metaTypes,
    nullptr
} };

void AxTabWrapper::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxTabWrapper *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->setEnabled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 1: _t->setVisible((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 2: { bool _r = _t->getEnabled();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 3: { bool _r = _t->getVisible();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 4: _t->addTab((*reinterpret_cast<std::add_pointer_t<QObject*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        default: ;
        }
    }
}

const QMetaObject *AxTabWrapper::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxTabWrapper::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12AxTabWrapperE_t>.strings))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "AbstractAxVisualElement"))
        return static_cast< AbstractAxVisualElement*>(this);
    return QObject::qt_metacast(_clname);
}

int AxTabWrapper::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 5)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 5)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 5;
    }
    return _id;
}
namespace {
struct qt_meta_tag_ZN20AxTableWidgetWrapperE_t {};
} // unnamed namespace

template <> constexpr inline auto AxTableWidgetWrapper::qt_create_metaobjectdata<qt_meta_tag_ZN20AxTableWidgetWrapperE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxTableWidgetWrapper",
        "cellChanged",
        "",
        "row",
        "column",
        "cellClicked",
        "cellDoubleClicked",
        "setEnabled",
        "enable",
        "setVisible",
        "getEnabled",
        "getVisible",
        "addColumn",
        "header",
        "setColumns",
        "QJSValue",
        "headers",
        "addItem",
        "items",
        "rowCount",
        "columnCount",
        "setRowCount",
        "rows",
        "setColumnCount",
        "cols",
        "currentRow",
        "currentColumn",
        "setSortingEnabled",
        "resizeToContent",
        "text",
        "setText",
        "setReadOnly",
        "read",
        "hideColumn",
        "setHeadersVisible",
        "setColumnAlign",
        "align",
        "clear",
        "selectedRows"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'cellChanged'
        QtMocHelpers::SignalData<void(int, int)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 }, { QMetaType::Int, 4 },
        }}),
        // Signal 'cellClicked'
        QtMocHelpers::SignalData<void(int, int)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 }, { QMetaType::Int, 4 },
        }}),
        // Signal 'cellDoubleClicked'
        QtMocHelpers::SignalData<void(int, int)>(6, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 }, { QMetaType::Int, 4 },
        }}),
        // Method 'setEnabled'
        QtMocHelpers::MethodData<void(const bool) const>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 8 },
        }}),
        // Method 'setVisible'
        QtMocHelpers::MethodData<void(const bool) const>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 8 },
        }}),
        // Method 'getEnabled'
        QtMocHelpers::MethodData<bool() const>(10, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'getVisible'
        QtMocHelpers::MethodData<bool() const>(11, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'addColumn'
        QtMocHelpers::MethodData<void(const QString &) const>(12, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 13 },
        }}),
        // Method 'setColumns'
        QtMocHelpers::MethodData<void(const QJSValue &) const>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 15, 16 },
        }}),
        // Method 'addItem'
        QtMocHelpers::MethodData<void(const QJSValue &) const>(17, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 15, 18 },
        }}),
        // Method 'rowCount'
        QtMocHelpers::MethodData<int() const>(19, 2, QMC::AccessPublic, QMetaType::Int),
        // Method 'columnCount'
        QtMocHelpers::MethodData<int() const>(20, 2, QMC::AccessPublic, QMetaType::Int),
        // Method 'setRowCount'
        QtMocHelpers::MethodData<void(int)>(21, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 22 },
        }}),
        // Method 'setColumnCount'
        QtMocHelpers::MethodData<void(int)>(23, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 24 },
        }}),
        // Method 'currentRow'
        QtMocHelpers::MethodData<int() const>(25, 2, QMC::AccessPublic, QMetaType::Int),
        // Method 'currentColumn'
        QtMocHelpers::MethodData<int() const>(26, 2, QMC::AccessPublic, QMetaType::Int),
        // Method 'setSortingEnabled'
        QtMocHelpers::MethodData<void(bool)>(27, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 8 },
        }}),
        // Method 'resizeToContent'
        QtMocHelpers::MethodData<void(int)>(28, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 4 },
        }}),
        // Method 'text'
        QtMocHelpers::MethodData<QString(int, int) const>(29, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::Int, 3 }, { QMetaType::Int, 4 },
        }}),
        // Method 'setText'
        QtMocHelpers::MethodData<void(int, int, const QString &) const>(30, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 }, { QMetaType::Int, 4 }, { QMetaType::QString, 29 },
        }}),
        // Method 'setReadOnly'
        QtMocHelpers::MethodData<void(bool)>(31, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 32 },
        }}),
        // Method 'hideColumn'
        QtMocHelpers::MethodData<void(int)>(33, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 4 },
        }}),
        // Method 'setHeadersVisible'
        QtMocHelpers::MethodData<void(bool)>(34, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 8 },
        }}),
        // Method 'setColumnAlign'
        QtMocHelpers::MethodData<void(int, const QString &)>(35, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 4 }, { QMetaType::QString, 36 },
        }}),
        // Method 'clear'
        QtMocHelpers::MethodData<void()>(37, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'selectedRows'
        QtMocHelpers::MethodData<QJSValue()>(38, 2, QMC::AccessPublic, 0x80000000 | 15),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxTableWidgetWrapper, qt_meta_tag_ZN20AxTableWidgetWrapperE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxTableWidgetWrapper::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN20AxTableWidgetWrapperE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN20AxTableWidgetWrapperE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN20AxTableWidgetWrapperE_t>.metaTypes,
    nullptr
} };

void AxTableWidgetWrapper::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxTableWidgetWrapper *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->cellChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 1: _t->cellClicked((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 2: _t->cellDoubleClicked((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 3: _t->setEnabled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 4: _t->setVisible((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 5: { bool _r = _t->getEnabled();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 6: { bool _r = _t->getVisible();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 7: _t->addColumn((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 8: _t->setColumns((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1]))); break;
        case 9: _t->addItem((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1]))); break;
        case 10: { int _r = _t->rowCount();
            if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
        case 11: { int _r = _t->columnCount();
            if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
        case 12: _t->setRowCount((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 13: _t->setColumnCount((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 14: { int _r = _t->currentRow();
            if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
        case 15: { int _r = _t->currentColumn();
            if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
        case 16: _t->setSortingEnabled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 17: _t->resizeToContent((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 18: { QString _r = _t->text((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 19: _t->setText((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3]))); break;
        case 20: _t->setReadOnly((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 21: _t->hideColumn((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 22: _t->setHeadersVisible((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 23: _t->setColumnAlign((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 24: _t->clear(); break;
        case 25: { QJSValue _r = _t->selectedRows();
            if (_a[0]) *reinterpret_cast<QJSValue*>(_a[0]) = std::move(_r); }  break;
        default: ;
        }
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 8:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 9:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (AxTableWidgetWrapper::*)(int , int )>(_a, &AxTableWidgetWrapper::cellChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (AxTableWidgetWrapper::*)(int , int )>(_a, &AxTableWidgetWrapper::cellClicked, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (AxTableWidgetWrapper::*)(int , int )>(_a, &AxTableWidgetWrapper::cellDoubleClicked, 2))
            return;
    }
}

const QMetaObject *AxTableWidgetWrapper::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxTableWidgetWrapper::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN20AxTableWidgetWrapperE_t>.strings))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "AbstractAxElement"))
        return static_cast< AbstractAxElement*>(this);
    if (!strcmp(_clname, "AbstractAxVisualElement"))
        return static_cast< AbstractAxVisualElement*>(this);
    return QObject::qt_metacast(_clname);
}

int AxTableWidgetWrapper::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 26)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 26;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 26)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 26;
    }
    return _id;
}

// SIGNAL 0
void AxTableWidgetWrapper::cellChanged(int _t1, int _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1, _t2);
}

// SIGNAL 1
void AxTableWidgetWrapper::cellClicked(int _t1, int _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1, _t2);
}

// SIGNAL 2
void AxTableWidgetWrapper::cellDoubleClicked(int _t1, int _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1, _t2);
}
namespace {
struct qt_meta_tag_ZN19AxListWidgetWrapperE_t {};
} // unnamed namespace

template <> constexpr inline auto AxListWidgetWrapper::qt_create_metaobjectdata<qt_meta_tag_ZN19AxListWidgetWrapperE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxListWidgetWrapper",
        "currentTextChanged",
        "",
        "currentText",
        "currentRowChanged",
        "currentRow",
        "itemClickedText",
        "text",
        "itemDoubleClickedText",
        "showContextMenu",
        "QPoint",
        "pos",
        "onAddClicked",
        "onRemoveClicked",
        "setEnabled",
        "enable",
        "setVisible",
        "getEnabled",
        "getVisible",
        "items",
        "QJSValue",
        "addItem",
        "addItems",
        "removeItem",
        "index",
        "itemText",
        "setItemText",
        "clear",
        "count",
        "setCurrentRow",
        "row",
        "selectedRows",
        "setReadOnly",
        "readonly",
        "setDragDropEnabled",
        "enabled",
        "setMenuEnabled",
        "setButtonsEnabled"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'currentTextChanged'
        QtMocHelpers::SignalData<void(const QString &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Signal 'currentRowChanged'
        QtMocHelpers::SignalData<void(int)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 5 },
        }}),
        // Signal 'itemClickedText'
        QtMocHelpers::SignalData<void(const QString &)>(6, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 7 },
        }}),
        // Signal 'itemDoubleClickedText'
        QtMocHelpers::SignalData<void(const QString &)>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 7 },
        }}),
        // Slot 'showContextMenu'
        QtMocHelpers::SlotData<void(const QPoint &)>(9, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 10, 11 },
        }}),
        // Slot 'onAddClicked'
        QtMocHelpers::SlotData<void()>(12, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onRemoveClicked'
        QtMocHelpers::SlotData<void()>(13, 2, QMC::AccessPrivate, QMetaType::Void),
        // Method 'setEnabled'
        QtMocHelpers::MethodData<void(const bool) const>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 15 },
        }}),
        // Method 'setVisible'
        QtMocHelpers::MethodData<void(const bool) const>(16, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 15 },
        }}),
        // Method 'getEnabled'
        QtMocHelpers::MethodData<bool() const>(17, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'getVisible'
        QtMocHelpers::MethodData<bool() const>(18, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'items'
        QtMocHelpers::MethodData<QJSValue()>(19, 2, QMC::AccessPublic, 0x80000000 | 20),
        // Method 'addItem'
        QtMocHelpers::MethodData<void(const QString &)>(21, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 7 },
        }}),
        // Method 'addItems'
        QtMocHelpers::MethodData<void(const QJSValue &)>(22, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 20, 19 },
        }}),
        // Method 'removeItem'
        QtMocHelpers::MethodData<void(int)>(23, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 24 },
        }}),
        // Method 'itemText'
        QtMocHelpers::MethodData<QString(int) const>(25, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::Int, 24 },
        }}),
        // Method 'setItemText'
        QtMocHelpers::MethodData<void(int, const QString &)>(26, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 24 }, { QMetaType::QString, 7 },
        }}),
        // Method 'clear'
        QtMocHelpers::MethodData<void()>(27, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'count'
        QtMocHelpers::MethodData<int() const>(28, 2, QMC::AccessPublic, QMetaType::Int),
        // Method 'currentRow'
        QtMocHelpers::MethodData<int() const>(5, 2, QMC::AccessPublic, QMetaType::Int),
        // Method 'setCurrentRow'
        QtMocHelpers::MethodData<void(int)>(29, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 30 },
        }}),
        // Method 'selectedRows'
        QtMocHelpers::MethodData<QJSValue() const>(31, 2, QMC::AccessPublic, 0x80000000 | 20),
        // Method 'setReadOnly'
        QtMocHelpers::MethodData<void(bool)>(32, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 33 },
        }}),
        // Method 'setDragDropEnabled'
        QtMocHelpers::MethodData<void(bool)>(34, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 35 },
        }}),
        // Method 'setMenuEnabled'
        QtMocHelpers::MethodData<void(bool)>(36, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 35 },
        }}),
        // Method 'setButtonsEnabled'
        QtMocHelpers::MethodData<void(bool)>(37, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 35 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxListWidgetWrapper, qt_meta_tag_ZN19AxListWidgetWrapperE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxListWidgetWrapper::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN19AxListWidgetWrapperE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN19AxListWidgetWrapperE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN19AxListWidgetWrapperE_t>.metaTypes,
    nullptr
} };

void AxListWidgetWrapper::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxListWidgetWrapper *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->currentTextChanged((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 1: _t->currentRowChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 2: _t->itemClickedText((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 3: _t->itemDoubleClickedText((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 4: _t->showContextMenu((*reinterpret_cast<std::add_pointer_t<QPoint>>(_a[1]))); break;
        case 5: _t->onAddClicked(); break;
        case 6: _t->onRemoveClicked(); break;
        case 7: _t->setEnabled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 8: _t->setVisible((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 9: { bool _r = _t->getEnabled();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 10: { bool _r = _t->getVisible();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 11: { QJSValue _r = _t->items();
            if (_a[0]) *reinterpret_cast<QJSValue*>(_a[0]) = std::move(_r); }  break;
        case 12: _t->addItem((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 13: _t->addItems((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1]))); break;
        case 14: _t->removeItem((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 15: { QString _r = _t->itemText((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 16: _t->setItemText((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 17: _t->clear(); break;
        case 18: { int _r = _t->count();
            if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
        case 19: { int _r = _t->currentRow();
            if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
        case 20: _t->setCurrentRow((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 21: { QJSValue _r = _t->selectedRows();
            if (_a[0]) *reinterpret_cast<QJSValue*>(_a[0]) = std::move(_r); }  break;
        case 22: _t->setReadOnly((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 23: _t->setDragDropEnabled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 24: _t->setMenuEnabled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 25: _t->setButtonsEnabled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 13:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (AxListWidgetWrapper::*)(const QString & )>(_a, &AxListWidgetWrapper::currentTextChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (AxListWidgetWrapper::*)(int )>(_a, &AxListWidgetWrapper::currentRowChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (AxListWidgetWrapper::*)(const QString & )>(_a, &AxListWidgetWrapper::itemClickedText, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (AxListWidgetWrapper::*)(const QString & )>(_a, &AxListWidgetWrapper::itemDoubleClickedText, 3))
            return;
    }
}

const QMetaObject *AxListWidgetWrapper::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxListWidgetWrapper::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN19AxListWidgetWrapperE_t>.strings))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "AbstractAxElement"))
        return static_cast< AbstractAxElement*>(this);
    if (!strcmp(_clname, "AbstractAxVisualElement"))
        return static_cast< AbstractAxVisualElement*>(this);
    return QObject::qt_metacast(_clname);
}

int AxListWidgetWrapper::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 26)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 26;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 26)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 26;
    }
    return _id;
}

// SIGNAL 0
void AxListWidgetWrapper::currentTextChanged(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void AxListWidgetWrapper::currentRowChanged(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void AxListWidgetWrapper::itemClickedText(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void AxListWidgetWrapper::itemDoubleClickedText(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}
namespace {
struct qt_meta_tag_ZN15AxButtonWrapperE_t {};
} // unnamed namespace

template <> constexpr inline auto AxButtonWrapper::qt_create_metaobjectdata<qt_meta_tag_ZN15AxButtonWrapperE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxButtonWrapper",
        "clicked",
        "",
        "setEnabled",
        "enable",
        "setVisible",
        "getEnabled",
        "getVisible"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'clicked'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'setEnabled'
        QtMocHelpers::MethodData<void(const bool) const>(3, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 4 },
        }}),
        // Method 'setVisible'
        QtMocHelpers::MethodData<void(const bool) const>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 4 },
        }}),
        // Method 'getEnabled'
        QtMocHelpers::MethodData<bool() const>(6, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'getVisible'
        QtMocHelpers::MethodData<bool() const>(7, 2, QMC::AccessPublic, QMetaType::Bool),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxButtonWrapper, qt_meta_tag_ZN15AxButtonWrapperE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxButtonWrapper::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15AxButtonWrapperE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15AxButtonWrapperE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN15AxButtonWrapperE_t>.metaTypes,
    nullptr
} };

void AxButtonWrapper::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxButtonWrapper *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->clicked(); break;
        case 1: _t->setEnabled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 2: _t->setVisible((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 3: { bool _r = _t->getEnabled();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 4: { bool _r = _t->getVisible();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (AxButtonWrapper::*)()>(_a, &AxButtonWrapper::clicked, 0))
            return;
    }
}

const QMetaObject *AxButtonWrapper::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxButtonWrapper::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15AxButtonWrapperE_t>.strings))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "AbstractAxVisualElement"))
        return static_cast< AbstractAxVisualElement*>(this);
    return QObject::qt_metacast(_clname);
}

int AxButtonWrapper::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 5)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 5)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 5;
    }
    return _id;
}

// SIGNAL 0
void AxButtonWrapper::clicked()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}
namespace {
struct qt_meta_tag_ZN17AxGroupBoxWrapperE_t {};
} // unnamed namespace

template <> constexpr inline auto AxGroupBoxWrapper::qt_create_metaobjectdata<qt_meta_tag_ZN17AxGroupBoxWrapperE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxGroupBoxWrapper",
        "clicked",
        "",
        "checked",
        "setEnabled",
        "enable",
        "setVisible",
        "getEnabled",
        "getVisible",
        "setTitle",
        "title",
        "isCheckable",
        "setCheckable",
        "checkable",
        "isChecked",
        "setChecked",
        "setPanel",
        "panel"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'clicked'
        QtMocHelpers::SignalData<void(bool)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 3 },
        }}),
        // Signal 'clicked'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void),
        // Method 'setEnabled'
        QtMocHelpers::MethodData<void(const bool) const>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 5 },
        }}),
        // Method 'setVisible'
        QtMocHelpers::MethodData<void(const bool) const>(6, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 5 },
        }}),
        // Method 'getEnabled'
        QtMocHelpers::MethodData<bool() const>(7, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'getVisible'
        QtMocHelpers::MethodData<bool() const>(8, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'setTitle'
        QtMocHelpers::MethodData<void(const QString &)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 10 },
        }}),
        // Method 'isCheckable'
        QtMocHelpers::MethodData<bool() const>(11, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'setCheckable'
        QtMocHelpers::MethodData<void(bool)>(12, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 13 },
        }}),
        // Method 'isChecked'
        QtMocHelpers::MethodData<bool() const>(14, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'setChecked'
        QtMocHelpers::MethodData<void(bool)>(15, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 3 },
        }}),
        // Method 'setPanel'
        QtMocHelpers::MethodData<void(QObject *) const>(16, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QObjectStar, 17 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxGroupBoxWrapper, qt_meta_tag_ZN17AxGroupBoxWrapperE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxGroupBoxWrapper::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AxGroupBoxWrapperE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AxGroupBoxWrapperE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN17AxGroupBoxWrapperE_t>.metaTypes,
    nullptr
} };

void AxGroupBoxWrapper::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxGroupBoxWrapper *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->clicked((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 1: _t->clicked(); break;
        case 2: _t->setEnabled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 3: _t->setVisible((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 4: { bool _r = _t->getEnabled();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 5: { bool _r = _t->getVisible();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 6: _t->setTitle((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 7: { bool _r = _t->isCheckable();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 8: _t->setCheckable((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 9: { bool _r = _t->isChecked();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 10: _t->setChecked((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 11: _t->setPanel((*reinterpret_cast<std::add_pointer_t<QObject*>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (AxGroupBoxWrapper::*)(bool )>(_a, &AxGroupBoxWrapper::clicked, 0))
            return;
    }
}

const QMetaObject *AxGroupBoxWrapper::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxGroupBoxWrapper::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AxGroupBoxWrapperE_t>.strings))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "AbstractAxElement"))
        return static_cast< AbstractAxElement*>(this);
    if (!strcmp(_clname, "AbstractAxVisualElement"))
        return static_cast< AbstractAxVisualElement*>(this);
    return QObject::qt_metacast(_clname);
}

int AxGroupBoxWrapper::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 12)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 12;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 12)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 12;
    }
    return _id;
}

// SIGNAL 0
void AxGroupBoxWrapper::clicked(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}
namespace {
struct qt_meta_tag_ZN19AxScrollAreaWrapperE_t {};
} // unnamed namespace

template <> constexpr inline auto AxScrollAreaWrapper::qt_create_metaobjectdata<qt_meta_tag_ZN19AxScrollAreaWrapperE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxScrollAreaWrapper",
        "setEnabled",
        "",
        "enable",
        "setVisible",
        "getEnabled",
        "getVisible",
        "setPanel",
        "panel",
        "setWidgetResizable",
        "resizable"
    };

    QtMocHelpers::UintData qt_methods {
        // Method 'setEnabled'
        QtMocHelpers::MethodData<void(const bool) const>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 3 },
        }}),
        // Method 'setVisible'
        QtMocHelpers::MethodData<void(const bool) const>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 3 },
        }}),
        // Method 'getEnabled'
        QtMocHelpers::MethodData<bool() const>(5, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'getVisible'
        QtMocHelpers::MethodData<bool() const>(6, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'setPanel'
        QtMocHelpers::MethodData<void(QObject *) const>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QObjectStar, 8 },
        }}),
        // Method 'setWidgetResizable'
        QtMocHelpers::MethodData<void(bool)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 10 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxScrollAreaWrapper, qt_meta_tag_ZN19AxScrollAreaWrapperE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxScrollAreaWrapper::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN19AxScrollAreaWrapperE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN19AxScrollAreaWrapperE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN19AxScrollAreaWrapperE_t>.metaTypes,
    nullptr
} };

void AxScrollAreaWrapper::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxScrollAreaWrapper *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->setEnabled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 1: _t->setVisible((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 2: { bool _r = _t->getEnabled();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 3: { bool _r = _t->getVisible();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 4: _t->setPanel((*reinterpret_cast<std::add_pointer_t<QObject*>>(_a[1]))); break;
        case 5: _t->setWidgetResizable((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        default: ;
        }
    }
}

const QMetaObject *AxScrollAreaWrapper::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxScrollAreaWrapper::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN19AxScrollAreaWrapperE_t>.strings))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "AbstractAxVisualElement"))
        return static_cast< AbstractAxVisualElement*>(this);
    return QObject::qt_metacast(_clname);
}

int AxScrollAreaWrapper::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 6)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 6;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 6)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 6;
    }
    return _id;
}
namespace {
struct qt_meta_tag_ZN17AxSplitterWrapperE_t {};
} // unnamed namespace

template <> constexpr inline auto AxSplitterWrapper::qt_create_metaobjectdata<qt_meta_tag_ZN17AxSplitterWrapperE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxSplitterWrapper",
        "splitterMoved",
        "",
        "pos",
        "index",
        "setEnabled",
        "enable",
        "setVisible",
        "getEnabled",
        "getVisible",
        "addPage",
        "w",
        "setSizes",
        "QVariantList",
        "sizes"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'splitterMoved'
        QtMocHelpers::SignalData<void(int, int)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 }, { QMetaType::Int, 4 },
        }}),
        // Method 'setEnabled'
        QtMocHelpers::MethodData<void(const bool) const>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 6 },
        }}),
        // Method 'setVisible'
        QtMocHelpers::MethodData<void(const bool) const>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 6 },
        }}),
        // Method 'getEnabled'
        QtMocHelpers::MethodData<bool() const>(8, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'getVisible'
        QtMocHelpers::MethodData<bool() const>(9, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'addPage'
        QtMocHelpers::MethodData<void(QObject *)>(10, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QObjectStar, 11 },
        }}),
        // Method 'setSizes'
        QtMocHelpers::MethodData<void(const QVariantList &)>(12, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 13, 14 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxSplitterWrapper, qt_meta_tag_ZN17AxSplitterWrapperE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxSplitterWrapper::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AxSplitterWrapperE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AxSplitterWrapperE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN17AxSplitterWrapperE_t>.metaTypes,
    nullptr
} };

void AxSplitterWrapper::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxSplitterWrapper *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->splitterMoved((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 1: _t->setEnabled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 2: _t->setVisible((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 3: { bool _r = _t->getEnabled();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 4: { bool _r = _t->getVisible();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 5: _t->addPage((*reinterpret_cast<std::add_pointer_t<QObject*>>(_a[1]))); break;
        case 6: _t->setSizes((*reinterpret_cast<std::add_pointer_t<QVariantList>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (AxSplitterWrapper::*)(int , int )>(_a, &AxSplitterWrapper::splitterMoved, 0))
            return;
    }
}

const QMetaObject *AxSplitterWrapper::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxSplitterWrapper::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AxSplitterWrapperE_t>.strings))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "AbstractAxVisualElement"))
        return static_cast< AbstractAxVisualElement*>(this);
    return QObject::qt_metacast(_clname);
}

int AxSplitterWrapper::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
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
    return _id;
}

// SIGNAL 0
void AxSplitterWrapper::splitterMoved(int _t1, int _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1, _t2);
}
namespace {
struct qt_meta_tag_ZN22AxStackedWidgetWrapperE_t {};
} // unnamed namespace

template <> constexpr inline auto AxStackedWidgetWrapper::qt_create_metaobjectdata<qt_meta_tag_ZN22AxStackedWidgetWrapperE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxStackedWidgetWrapper",
        "currentChanged",
        "",
        "index",
        "setEnabled",
        "enable",
        "setVisible",
        "getEnabled",
        "getVisible",
        "addPage",
        "page",
        "insertPage",
        "removePage",
        "setCurrentIndex",
        "currentIndex",
        "count"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'currentChanged'
        QtMocHelpers::SignalData<void(int)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 },
        }}),
        // Method 'setEnabled'
        QtMocHelpers::MethodData<void(const bool) const>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 5 },
        }}),
        // Method 'setVisible'
        QtMocHelpers::MethodData<void(const bool) const>(6, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 5 },
        }}),
        // Method 'getEnabled'
        QtMocHelpers::MethodData<bool() const>(7, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'getVisible'
        QtMocHelpers::MethodData<bool() const>(8, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'addPage'
        QtMocHelpers::MethodData<int(QObject *)>(9, 2, QMC::AccessPublic, QMetaType::Int, {{
            { QMetaType::QObjectStar, 10 },
        }}),
        // Method 'insertPage'
        QtMocHelpers::MethodData<int(int, QObject *)>(11, 2, QMC::AccessPublic, QMetaType::Int, {{
            { QMetaType::Int, 3 }, { QMetaType::QObjectStar, 10 },
        }}),
        // Method 'removePage'
        QtMocHelpers::MethodData<void(int)>(12, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 },
        }}),
        // Method 'setCurrentIndex'
        QtMocHelpers::MethodData<void(int)>(13, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 },
        }}),
        // Method 'currentIndex'
        QtMocHelpers::MethodData<int() const>(14, 2, QMC::AccessPublic, QMetaType::Int),
        // Method 'count'
        QtMocHelpers::MethodData<int() const>(15, 2, QMC::AccessPublic, QMetaType::Int),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxStackedWidgetWrapper, qt_meta_tag_ZN22AxStackedWidgetWrapperE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxStackedWidgetWrapper::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN22AxStackedWidgetWrapperE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN22AxStackedWidgetWrapperE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN22AxStackedWidgetWrapperE_t>.metaTypes,
    nullptr
} };

void AxStackedWidgetWrapper::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxStackedWidgetWrapper *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->currentChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 1: _t->setEnabled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 2: _t->setVisible((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 3: { bool _r = _t->getEnabled();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 4: { bool _r = _t->getVisible();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 5: { int _r = _t->addPage((*reinterpret_cast<std::add_pointer_t<QObject*>>(_a[1])));
            if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
        case 6: { int _r = _t->insertPage((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QObject*>>(_a[2])));
            if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
        case 7: _t->removePage((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 8: _t->setCurrentIndex((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 9: { int _r = _t->currentIndex();
            if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
        case 10: { int _r = _t->count();
            if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (AxStackedWidgetWrapper::*)(int )>(_a, &AxStackedWidgetWrapper::currentChanged, 0))
            return;
    }
}

const QMetaObject *AxStackedWidgetWrapper::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxStackedWidgetWrapper::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN22AxStackedWidgetWrapperE_t>.strings))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "AbstractAxVisualElement"))
        return static_cast< AbstractAxVisualElement*>(this);
    return QObject::qt_metacast(_clname);
}

int AxStackedWidgetWrapper::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
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

// SIGNAL 0
void AxStackedWidgetWrapper::currentChanged(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}
namespace {
struct qt_meta_tag_ZN14AxPanelWrapperE_t {};
} // unnamed namespace

template <> constexpr inline auto AxPanelWrapper::qt_create_metaobjectdata<qt_meta_tag_ZN14AxPanelWrapperE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxPanelWrapper",
        "setEnabled",
        "",
        "enable",
        "setVisible",
        "getEnabled",
        "getVisible",
        "setLayout",
        "layoutWrapper"
    };

    QtMocHelpers::UintData qt_methods {
        // Method 'setEnabled'
        QtMocHelpers::MethodData<void(const bool) const>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 3 },
        }}),
        // Method 'setVisible'
        QtMocHelpers::MethodData<void(const bool) const>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 3 },
        }}),
        // Method 'getEnabled'
        QtMocHelpers::MethodData<bool() const>(5, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'getVisible'
        QtMocHelpers::MethodData<bool() const>(6, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'setLayout'
        QtMocHelpers::MethodData<void(QObject *) const>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QObjectStar, 8 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxPanelWrapper, qt_meta_tag_ZN14AxPanelWrapperE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxPanelWrapper::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14AxPanelWrapperE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14AxPanelWrapperE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN14AxPanelWrapperE_t>.metaTypes,
    nullptr
} };

void AxPanelWrapper::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxPanelWrapper *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->setEnabled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 1: _t->setVisible((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 2: { bool _r = _t->getEnabled();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 3: { bool _r = _t->getVisible();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 4: _t->setLayout((*reinterpret_cast<std::add_pointer_t<QObject*>>(_a[1]))); break;
        default: ;
        }
    }
}

const QMetaObject *AxPanelWrapper::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxPanelWrapper::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14AxPanelWrapperE_t>.strings))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "AbstractAxVisualElement"))
        return static_cast< AbstractAxVisualElement*>(this);
    return QObject::qt_metacast(_clname);
}

int AxPanelWrapper::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 5)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 5)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 5;
    }
    return _id;
}
namespace {
struct qt_meta_tag_ZN18AxContainerWrapperE_t {};
} // unnamed namespace

template <> constexpr inline auto AxContainerWrapper::qt_create_metaobjectdata<qt_meta_tag_ZN18AxContainerWrapperE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxContainerWrapper",
        "put",
        "",
        "id",
        "wrapper",
        "get",
        "contains",
        "remove",
        "toJson",
        "fromJson",
        "jsonString",
        "toProperty",
        "QJSValue",
        "fromProperty",
        "obj"
    };

    QtMocHelpers::UintData qt_methods {
        // Method 'put'
        QtMocHelpers::MethodData<void(const QString &, QObject *)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 }, { QMetaType::QObjectStar, 4 },
        }}),
        // Method 'get'
        QtMocHelpers::MethodData<QObject *(const QString &)>(5, 2, QMC::AccessPublic, QMetaType::QObjectStar, {{
            { QMetaType::QString, 3 },
        }}),
        // Method 'contains'
        QtMocHelpers::MethodData<bool(const QString &) const>(6, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 3 },
        }}),
        // Method 'remove'
        QtMocHelpers::MethodData<void(const QString &)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Method 'toJson'
        QtMocHelpers::MethodData<QString()>(8, 2, QMC::AccessPublic, QMetaType::QString),
        // Method 'fromJson'
        QtMocHelpers::MethodData<void(const QString &)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 10 },
        }}),
        // Method 'toProperty'
        QtMocHelpers::MethodData<QJSValue()>(11, 2, QMC::AccessPublic, 0x80000000 | 12),
        // Method 'fromProperty'
        QtMocHelpers::MethodData<void(const QJSValue &)>(13, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 12, 14 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxContainerWrapper, qt_meta_tag_ZN18AxContainerWrapperE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxContainerWrapper::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18AxContainerWrapperE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18AxContainerWrapperE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN18AxContainerWrapperE_t>.metaTypes,
    nullptr
} };

void AxContainerWrapper::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxContainerWrapper *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->put((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QObject*>>(_a[2]))); break;
        case 1: { QObject* _r = _t->get((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 2: { bool _r = _t->contains((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 3: _t->remove((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 4: { QString _r = _t->toJson();
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 5: _t->fromJson((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 6: { QJSValue _r = _t->toProperty();
            if (_a[0]) *reinterpret_cast<QJSValue*>(_a[0]) = std::move(_r); }  break;
        case 7: _t->fromProperty((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 7:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        }
    }
}

const QMetaObject *AxContainerWrapper::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxContainerWrapper::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18AxContainerWrapperE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int AxContainerWrapper::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 8)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 8;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 8)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 8;
    }
    return _id;
}
namespace {
struct qt_meta_tag_ZN15AxDialogWrapperE_t {};
} // unnamed namespace

template <> constexpr inline auto AxDialogWrapper::qt_create_metaobjectdata<qt_meta_tag_ZN15AxDialogWrapperE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxDialogWrapper",
        "setLayout",
        "",
        "layoutWrapper",
        "setSize",
        "w",
        "h",
        "exec",
        "close",
        "setButtonsText",
        "ok_text",
        "cancel_text"
    };

    QtMocHelpers::UintData qt_methods {
        // Method 'setLayout'
        QtMocHelpers::MethodData<void(QObject *)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QObjectStar, 3 },
        }}),
        // Method 'setSize'
        QtMocHelpers::MethodData<void(int, int) const>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 5 }, { QMetaType::Int, 6 },
        }}),
        // Method 'exec'
        QtMocHelpers::MethodData<bool() const>(7, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'close'
        QtMocHelpers::MethodData<void() const>(8, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'setButtonsText'
        QtMocHelpers::MethodData<void(const QString &, const QString &) const>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 10 }, { QMetaType::QString, 11 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxDialogWrapper, qt_meta_tag_ZN15AxDialogWrapperE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxDialogWrapper::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15AxDialogWrapperE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15AxDialogWrapperE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN15AxDialogWrapperE_t>.metaTypes,
    nullptr
} };

void AxDialogWrapper::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxDialogWrapper *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->setLayout((*reinterpret_cast<std::add_pointer_t<QObject*>>(_a[1]))); break;
        case 1: _t->setSize((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 2: { bool _r = _t->exec();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 3: _t->close(); break;
        case 4: _t->setButtonsText((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        default: ;
        }
    }
}

const QMetaObject *AxDialogWrapper::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxDialogWrapper::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15AxDialogWrapperE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int AxDialogWrapper::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 5)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 5)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 5;
    }
    return _id;
}
namespace {
struct qt_meta_tag_ZN18AxExtDialogWrapperE_t {};
} // unnamed namespace

template <> constexpr inline auto AxExtDialogWrapper::qt_create_metaobjectdata<qt_meta_tag_ZN18AxExtDialogWrapperE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxExtDialogWrapper",
        "setLayout",
        "",
        "layoutWrapper",
        "setSize",
        "w",
        "h",
        "exec",
        "show",
        "close",
        "setButtonsText",
        "ok_text",
        "cancel_text"
    };

    QtMocHelpers::UintData qt_methods {
        // Method 'setLayout'
        QtMocHelpers::MethodData<void(QObject *)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QObjectStar, 3 },
        }}),
        // Method 'setSize'
        QtMocHelpers::MethodData<void(int, int) const>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 5 }, { QMetaType::Int, 6 },
        }}),
        // Method 'exec'
        QtMocHelpers::MethodData<bool() const>(7, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'show'
        QtMocHelpers::MethodData<void() const>(8, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'close'
        QtMocHelpers::MethodData<void() const>(9, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'setButtonsText'
        QtMocHelpers::MethodData<void(const QString &, const QString &) const>(10, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 11 }, { QMetaType::QString, 12 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxExtDialogWrapper, qt_meta_tag_ZN18AxExtDialogWrapperE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxExtDialogWrapper::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18AxExtDialogWrapperE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18AxExtDialogWrapperE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN18AxExtDialogWrapperE_t>.metaTypes,
    nullptr
} };

void AxExtDialogWrapper::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxExtDialogWrapper *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->setLayout((*reinterpret_cast<std::add_pointer_t<QObject*>>(_a[1]))); break;
        case 1: _t->setSize((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 2: { bool _r = _t->exec();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 3: _t->show(); break;
        case 4: _t->close(); break;
        case 5: _t->setButtonsText((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        default: ;
        }
    }
}

const QMetaObject *AxExtDialogWrapper::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxExtDialogWrapper::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18AxExtDialogWrapperE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int AxExtDialogWrapper::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 6)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 6;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 6)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 6;
    }
    return _id;
}
namespace {
struct qt_meta_tag_ZN14AxSelectorFileE_t {};
} // unnamed namespace

template <> constexpr inline auto AxSelectorFile::qt_create_metaobjectdata<qt_meta_tag_ZN14AxSelectorFileE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxSelectorFile",
        "onSelectFile",
        "",
        "setEnabled",
        "enable",
        "setVisible",
        "getEnabled",
        "getVisible",
        "setPlaceholder",
        "text"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'onSelectFile'
        QtMocHelpers::SlotData<void()>(1, 2, QMC::AccessPrivate, QMetaType::Void),
        // Method 'setEnabled'
        QtMocHelpers::MethodData<void(const bool) const>(3, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 4 },
        }}),
        // Method 'setVisible'
        QtMocHelpers::MethodData<void(const bool) const>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 4 },
        }}),
        // Method 'getEnabled'
        QtMocHelpers::MethodData<bool() const>(6, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'getVisible'
        QtMocHelpers::MethodData<bool() const>(7, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'setPlaceholder'
        QtMocHelpers::MethodData<void(const QString &) const>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 9 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxSelectorFile, qt_meta_tag_ZN14AxSelectorFileE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxSelectorFile::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14AxSelectorFileE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14AxSelectorFileE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN14AxSelectorFileE_t>.metaTypes,
    nullptr
} };

void AxSelectorFile::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxSelectorFile *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->onSelectFile(); break;
        case 1: _t->setEnabled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 2: _t->setVisible((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 3: { bool _r = _t->getEnabled();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 4: { bool _r = _t->getVisible();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 5: _t->setPlaceholder((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        default: ;
        }
    }
}

const QMetaObject *AxSelectorFile::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxSelectorFile::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14AxSelectorFileE_t>.strings))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "AbstractAxElement"))
        return static_cast< AbstractAxElement*>(this);
    if (!strcmp(_clname, "AbstractAxVisualElement"))
        return static_cast< AbstractAxVisualElement*>(this);
    return QObject::qt_metacast(_clname);
}

int AxSelectorFile::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 6)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 6;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 6)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 6;
    }
    return _id;
}
namespace {
struct qt_meta_tag_ZN17AxCredsTableModelE_t {};
} // unnamed namespace

template <> constexpr inline auto AxCredsTableModel::qt_create_metaobjectdata<qt_meta_tag_ZN17AxCredsTableModelE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxCredsTableModel"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxCredsTableModel, qt_meta_tag_ZN17AxCredsTableModelE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxCredsTableModel::staticMetaObject = { {
    QMetaObject::SuperData::link<QAbstractTableModel::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AxCredsTableModelE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AxCredsTableModelE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN17AxCredsTableModelE_t>.metaTypes,
    nullptr
} };

void AxCredsTableModel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxCredsTableModel *>(_o);
    (void)_t;
    (void)_c;
    (void)_id;
    (void)_a;
}

const QMetaObject *AxCredsTableModel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxCredsTableModel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AxCredsTableModelE_t>.strings))
        return static_cast<void*>(this);
    return QAbstractTableModel::qt_metacast(_clname);
}

int AxCredsTableModel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QAbstractTableModel::qt_metacall(_c, _id, _a);
    return _id;
}
namespace {
struct qt_meta_tag_ZN23AxCredsFilterProxyModelE_t {};
} // unnamed namespace

template <> constexpr inline auto AxCredsFilterProxyModel::qt_create_metaobjectdata<qt_meta_tag_ZN23AxCredsFilterProxyModelE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxCredsFilterProxyModel"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxCredsFilterProxyModel, qt_meta_tag_ZN23AxCredsFilterProxyModelE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxCredsFilterProxyModel::staticMetaObject = { {
    QMetaObject::SuperData::link<QSortFilterProxyModel::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN23AxCredsFilterProxyModelE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN23AxCredsFilterProxyModelE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN23AxCredsFilterProxyModelE_t>.metaTypes,
    nullptr
} };

void AxCredsFilterProxyModel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxCredsFilterProxyModel *>(_o);
    (void)_t;
    (void)_c;
    (void)_id;
    (void)_a;
}

const QMetaObject *AxCredsFilterProxyModel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxCredsFilterProxyModel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN23AxCredsFilterProxyModelE_t>.strings))
        return static_cast<void*>(this);
    return QSortFilterProxyModel::qt_metacast(_clname);
}

int AxCredsFilterProxyModel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QSortFilterProxyModel::qt_metacall(_c, _id, _a);
    return _id;
}
namespace {
struct qt_meta_tag_ZN13AxDialogCredsE_t {};
} // unnamed namespace

template <> constexpr inline auto AxDialogCreds::qt_create_metaobjectdata<qt_meta_tag_ZN13AxDialogCredsE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxDialogCreds",
        "onClicked",
        "",
        "handleSearch",
        "clearSearch"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'onClicked'
        QtMocHelpers::SlotData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'handleSearch'
        QtMocHelpers::SlotData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'clearSearch'
        QtMocHelpers::SlotData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxDialogCreds, qt_meta_tag_ZN13AxDialogCredsE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxDialogCreds::staticMetaObject = { {
    QMetaObject::SuperData::link<QDialog::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN13AxDialogCredsE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN13AxDialogCredsE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN13AxDialogCredsE_t>.metaTypes,
    nullptr
} };

void AxDialogCreds::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxDialogCreds *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->onClicked(); break;
        case 1: _t->handleSearch(); break;
        case 2: _t->clearSearch(); break;
        default: ;
        }
    }
    (void)_a;
}

const QMetaObject *AxDialogCreds::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxDialogCreds::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN13AxDialogCredsE_t>.strings))
        return static_cast<void*>(this);
    return QDialog::qt_metacast(_clname);
}

int AxDialogCreds::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QDialog::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 3)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 3)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 3;
    }
    return _id;
}
namespace {
struct qt_meta_tag_ZN15AxSelectorCredsE_t {};
} // unnamed namespace

template <> constexpr inline auto AxSelectorCreds::qt_create_metaobjectdata<qt_meta_tag_ZN15AxSelectorCredsE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxSelectorCreds",
        "setSize",
        "",
        "w",
        "h",
        "exec",
        "QJSValue",
        "close"
    };

    QtMocHelpers::UintData qt_methods {
        // Method 'setSize'
        QtMocHelpers::MethodData<void(int, int) const>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 }, { QMetaType::Int, 4 },
        }}),
        // Method 'exec'
        QtMocHelpers::MethodData<QJSValue() const>(5, 2, QMC::AccessPublic, 0x80000000 | 6),
        // Method 'close'
        QtMocHelpers::MethodData<void() const>(7, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxSelectorCreds, qt_meta_tag_ZN15AxSelectorCredsE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxSelectorCreds::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15AxSelectorCredsE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15AxSelectorCredsE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN15AxSelectorCredsE_t>.metaTypes,
    nullptr
} };

void AxSelectorCreds::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxSelectorCreds *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->setSize((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 1: { QJSValue _r = _t->exec();
            if (_a[0]) *reinterpret_cast<QJSValue*>(_a[0]) = std::move(_r); }  break;
        case 2: _t->close(); break;
        default: ;
        }
    }
}

const QMetaObject *AxSelectorCreds::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxSelectorCreds::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15AxSelectorCredsE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int AxSelectorCreds::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 3)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 3)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 3;
    }
    return _id;
}
namespace {
struct qt_meta_tag_ZN18AxAgentsTableModelE_t {};
} // unnamed namespace

template <> constexpr inline auto AxAgentsTableModel::qt_create_metaobjectdata<qt_meta_tag_ZN18AxAgentsTableModelE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxAgentsTableModel"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxAgentsTableModel, qt_meta_tag_ZN18AxAgentsTableModelE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxAgentsTableModel::staticMetaObject = { {
    QMetaObject::SuperData::link<QAbstractTableModel::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18AxAgentsTableModelE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18AxAgentsTableModelE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN18AxAgentsTableModelE_t>.metaTypes,
    nullptr
} };

void AxAgentsTableModel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxAgentsTableModel *>(_o);
    (void)_t;
    (void)_c;
    (void)_id;
    (void)_a;
}

const QMetaObject *AxAgentsTableModel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxAgentsTableModel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18AxAgentsTableModelE_t>.strings))
        return static_cast<void*>(this);
    return QAbstractTableModel::qt_metacast(_clname);
}

int AxAgentsTableModel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QAbstractTableModel::qt_metacall(_c, _id, _a);
    return _id;
}
namespace {
struct qt_meta_tag_ZN24AxAgentsFilterProxyModelE_t {};
} // unnamed namespace

template <> constexpr inline auto AxAgentsFilterProxyModel::qt_create_metaobjectdata<qt_meta_tag_ZN24AxAgentsFilterProxyModelE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxAgentsFilterProxyModel"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxAgentsFilterProxyModel, qt_meta_tag_ZN24AxAgentsFilterProxyModelE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxAgentsFilterProxyModel::staticMetaObject = { {
    QMetaObject::SuperData::link<QSortFilterProxyModel::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN24AxAgentsFilterProxyModelE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN24AxAgentsFilterProxyModelE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN24AxAgentsFilterProxyModelE_t>.metaTypes,
    nullptr
} };

void AxAgentsFilterProxyModel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxAgentsFilterProxyModel *>(_o);
    (void)_t;
    (void)_c;
    (void)_id;
    (void)_a;
}

const QMetaObject *AxAgentsFilterProxyModel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxAgentsFilterProxyModel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN24AxAgentsFilterProxyModelE_t>.strings))
        return static_cast<void*>(this);
    return QSortFilterProxyModel::qt_metacast(_clname);
}

int AxAgentsFilterProxyModel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QSortFilterProxyModel::qt_metacall(_c, _id, _a);
    return _id;
}
namespace {
struct qt_meta_tag_ZN14AxDialogAgentsE_t {};
} // unnamed namespace

template <> constexpr inline auto AxDialogAgents::qt_create_metaobjectdata<qt_meta_tag_ZN14AxDialogAgentsE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxDialogAgents",
        "onClicked",
        "",
        "handleSearch",
        "clearSearch"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'onClicked'
        QtMocHelpers::SlotData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'handleSearch'
        QtMocHelpers::SlotData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'clearSearch'
        QtMocHelpers::SlotData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxDialogAgents, qt_meta_tag_ZN14AxDialogAgentsE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxDialogAgents::staticMetaObject = { {
    QMetaObject::SuperData::link<QDialog::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14AxDialogAgentsE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14AxDialogAgentsE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN14AxDialogAgentsE_t>.metaTypes,
    nullptr
} };

void AxDialogAgents::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxDialogAgents *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->onClicked(); break;
        case 1: _t->handleSearch(); break;
        case 2: _t->clearSearch(); break;
        default: ;
        }
    }
    (void)_a;
}

const QMetaObject *AxDialogAgents::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxDialogAgents::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14AxDialogAgentsE_t>.strings))
        return static_cast<void*>(this);
    return QDialog::qt_metacast(_clname);
}

int AxDialogAgents::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QDialog::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 3)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 3)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 3;
    }
    return _id;
}
namespace {
struct qt_meta_tag_ZN16AxSelectorAgentsE_t {};
} // unnamed namespace

template <> constexpr inline auto AxSelectorAgents::qt_create_metaobjectdata<qt_meta_tag_ZN16AxSelectorAgentsE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxSelectorAgents",
        "setSize",
        "",
        "w",
        "h",
        "exec",
        "QJSValue",
        "close"
    };

    QtMocHelpers::UintData qt_methods {
        // Method 'setSize'
        QtMocHelpers::MethodData<void(int, int) const>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 }, { QMetaType::Int, 4 },
        }}),
        // Method 'exec'
        QtMocHelpers::MethodData<QJSValue() const>(5, 2, QMC::AccessPublic, 0x80000000 | 6),
        // Method 'close'
        QtMocHelpers::MethodData<void() const>(7, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxSelectorAgents, qt_meta_tag_ZN16AxSelectorAgentsE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxSelectorAgents::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN16AxSelectorAgentsE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN16AxSelectorAgentsE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN16AxSelectorAgentsE_t>.metaTypes,
    nullptr
} };

void AxSelectorAgents::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxSelectorAgents *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->setSize((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 1: { QJSValue _r = _t->exec();
            if (_a[0]) *reinterpret_cast<QJSValue*>(_a[0]) = std::move(_r); }  break;
        case 2: _t->close(); break;
        default: ;
        }
    }
}

const QMetaObject *AxSelectorAgents::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxSelectorAgents::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN16AxSelectorAgentsE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int AxSelectorAgents::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 3)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 3)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 3;
    }
    return _id;
}
namespace {
struct qt_meta_tag_ZN21AxListenersTableModelE_t {};
} // unnamed namespace

template <> constexpr inline auto AxListenersTableModel::qt_create_metaobjectdata<qt_meta_tag_ZN21AxListenersTableModelE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxListenersTableModel"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxListenersTableModel, qt_meta_tag_ZN21AxListenersTableModelE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxListenersTableModel::staticMetaObject = { {
    QMetaObject::SuperData::link<QAbstractTableModel::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN21AxListenersTableModelE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN21AxListenersTableModelE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN21AxListenersTableModelE_t>.metaTypes,
    nullptr
} };

void AxListenersTableModel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxListenersTableModel *>(_o);
    (void)_t;
    (void)_c;
    (void)_id;
    (void)_a;
}

const QMetaObject *AxListenersTableModel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxListenersTableModel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN21AxListenersTableModelE_t>.strings))
        return static_cast<void*>(this);
    return QAbstractTableModel::qt_metacast(_clname);
}

int AxListenersTableModel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QAbstractTableModel::qt_metacall(_c, _id, _a);
    return _id;
}
namespace {
struct qt_meta_tag_ZN27AxListenersFilterProxyModelE_t {};
} // unnamed namespace

template <> constexpr inline auto AxListenersFilterProxyModel::qt_create_metaobjectdata<qt_meta_tag_ZN27AxListenersFilterProxyModelE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxListenersFilterProxyModel"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxListenersFilterProxyModel, qt_meta_tag_ZN27AxListenersFilterProxyModelE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxListenersFilterProxyModel::staticMetaObject = { {
    QMetaObject::SuperData::link<QSortFilterProxyModel::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN27AxListenersFilterProxyModelE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN27AxListenersFilterProxyModelE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN27AxListenersFilterProxyModelE_t>.metaTypes,
    nullptr
} };

void AxListenersFilterProxyModel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxListenersFilterProxyModel *>(_o);
    (void)_t;
    (void)_c;
    (void)_id;
    (void)_a;
}

const QMetaObject *AxListenersFilterProxyModel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxListenersFilterProxyModel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN27AxListenersFilterProxyModelE_t>.strings))
        return static_cast<void*>(this);
    return QSortFilterProxyModel::qt_metacast(_clname);
}

int AxListenersFilterProxyModel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QSortFilterProxyModel::qt_metacall(_c, _id, _a);
    return _id;
}
namespace {
struct qt_meta_tag_ZN17AxDialogListenersE_t {};
} // unnamed namespace

template <> constexpr inline auto AxDialogListeners::qt_create_metaobjectdata<qt_meta_tag_ZN17AxDialogListenersE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxDialogListeners",
        "onClicked",
        "",
        "handleSearch",
        "clearSearch"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'onClicked'
        QtMocHelpers::SlotData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'handleSearch'
        QtMocHelpers::SlotData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'clearSearch'
        QtMocHelpers::SlotData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxDialogListeners, qt_meta_tag_ZN17AxDialogListenersE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxDialogListeners::staticMetaObject = { {
    QMetaObject::SuperData::link<QDialog::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AxDialogListenersE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AxDialogListenersE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN17AxDialogListenersE_t>.metaTypes,
    nullptr
} };

void AxDialogListeners::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxDialogListeners *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->onClicked(); break;
        case 1: _t->handleSearch(); break;
        case 2: _t->clearSearch(); break;
        default: ;
        }
    }
    (void)_a;
}

const QMetaObject *AxDialogListeners::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxDialogListeners::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AxDialogListenersE_t>.strings))
        return static_cast<void*>(this);
    return QDialog::qt_metacast(_clname);
}

int AxDialogListeners::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QDialog::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 3)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 3)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 3;
    }
    return _id;
}
namespace {
struct qt_meta_tag_ZN19AxSelectorListenersE_t {};
} // unnamed namespace

template <> constexpr inline auto AxSelectorListeners::qt_create_metaobjectdata<qt_meta_tag_ZN19AxSelectorListenersE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxSelectorListeners",
        "setSize",
        "",
        "w",
        "h",
        "exec",
        "QJSValue",
        "close"
    };

    QtMocHelpers::UintData qt_methods {
        // Method 'setSize'
        QtMocHelpers::MethodData<void(int, int) const>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 }, { QMetaType::Int, 4 },
        }}),
        // Method 'exec'
        QtMocHelpers::MethodData<QJSValue() const>(5, 2, QMC::AccessPublic, 0x80000000 | 6),
        // Method 'close'
        QtMocHelpers::MethodData<void() const>(7, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxSelectorListeners, qt_meta_tag_ZN19AxSelectorListenersE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxSelectorListeners::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN19AxSelectorListenersE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN19AxSelectorListenersE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN19AxSelectorListenersE_t>.metaTypes,
    nullptr
} };

void AxSelectorListeners::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxSelectorListeners *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->setSize((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 1: { QJSValue _r = _t->exec();
            if (_a[0]) *reinterpret_cast<QJSValue*>(_a[0]) = std::move(_r); }  break;
        case 2: _t->close(); break;
        default: ;
        }
    }
}

const QMetaObject *AxSelectorListeners::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxSelectorListeners::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN19AxSelectorListenersE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int AxSelectorListeners::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 3)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 3)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 3;
    }
    return _id;
}
namespace {
struct qt_meta_tag_ZN19AxTargetsTableModelE_t {};
} // unnamed namespace

template <> constexpr inline auto AxTargetsTableModel::qt_create_metaobjectdata<qt_meta_tag_ZN19AxTargetsTableModelE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxTargetsTableModel"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxTargetsTableModel, qt_meta_tag_ZN19AxTargetsTableModelE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxTargetsTableModel::staticMetaObject = { {
    QMetaObject::SuperData::link<QAbstractTableModel::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN19AxTargetsTableModelE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN19AxTargetsTableModelE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN19AxTargetsTableModelE_t>.metaTypes,
    nullptr
} };

void AxTargetsTableModel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxTargetsTableModel *>(_o);
    (void)_t;
    (void)_c;
    (void)_id;
    (void)_a;
}

const QMetaObject *AxTargetsTableModel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxTargetsTableModel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN19AxTargetsTableModelE_t>.strings))
        return static_cast<void*>(this);
    return QAbstractTableModel::qt_metacast(_clname);
}

int AxTargetsTableModel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QAbstractTableModel::qt_metacall(_c, _id, _a);
    return _id;
}
namespace {
struct qt_meta_tag_ZN25AxTargetsFilterProxyModelE_t {};
} // unnamed namespace

template <> constexpr inline auto AxTargetsFilterProxyModel::qt_create_metaobjectdata<qt_meta_tag_ZN25AxTargetsFilterProxyModelE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxTargetsFilterProxyModel"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxTargetsFilterProxyModel, qt_meta_tag_ZN25AxTargetsFilterProxyModelE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxTargetsFilterProxyModel::staticMetaObject = { {
    QMetaObject::SuperData::link<QSortFilterProxyModel::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN25AxTargetsFilterProxyModelE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN25AxTargetsFilterProxyModelE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN25AxTargetsFilterProxyModelE_t>.metaTypes,
    nullptr
} };

void AxTargetsFilterProxyModel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxTargetsFilterProxyModel *>(_o);
    (void)_t;
    (void)_c;
    (void)_id;
    (void)_a;
}

const QMetaObject *AxTargetsFilterProxyModel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxTargetsFilterProxyModel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN25AxTargetsFilterProxyModelE_t>.strings))
        return static_cast<void*>(this);
    return QSortFilterProxyModel::qt_metacast(_clname);
}

int AxTargetsFilterProxyModel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QSortFilterProxyModel::qt_metacall(_c, _id, _a);
    return _id;
}
namespace {
struct qt_meta_tag_ZN15AxDialogTargetsE_t {};
} // unnamed namespace

template <> constexpr inline auto AxDialogTargets::qt_create_metaobjectdata<qt_meta_tag_ZN15AxDialogTargetsE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxDialogTargets",
        "onClicked",
        "",
        "handleSearch",
        "clearSearch"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'onClicked'
        QtMocHelpers::SlotData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'handleSearch'
        QtMocHelpers::SlotData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'clearSearch'
        QtMocHelpers::SlotData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxDialogTargets, qt_meta_tag_ZN15AxDialogTargetsE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxDialogTargets::staticMetaObject = { {
    QMetaObject::SuperData::link<QDialog::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15AxDialogTargetsE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15AxDialogTargetsE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN15AxDialogTargetsE_t>.metaTypes,
    nullptr
} };

void AxDialogTargets::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxDialogTargets *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->onClicked(); break;
        case 1: _t->handleSearch(); break;
        case 2: _t->clearSearch(); break;
        default: ;
        }
    }
    (void)_a;
}

const QMetaObject *AxDialogTargets::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxDialogTargets::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15AxDialogTargetsE_t>.strings))
        return static_cast<void*>(this);
    return QDialog::qt_metacast(_clname);
}

int AxDialogTargets::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QDialog::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 3)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 3)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 3;
    }
    return _id;
}
namespace {
struct qt_meta_tag_ZN17AxSelectorTargetsE_t {};
} // unnamed namespace

template <> constexpr inline auto AxSelectorTargets::qt_create_metaobjectdata<qt_meta_tag_ZN17AxSelectorTargetsE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxSelectorTargets",
        "setSize",
        "",
        "w",
        "h",
        "exec",
        "QJSValue",
        "close"
    };

    QtMocHelpers::UintData qt_methods {
        // Method 'setSize'
        QtMocHelpers::MethodData<void(int, int) const>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 }, { QMetaType::Int, 4 },
        }}),
        // Method 'exec'
        QtMocHelpers::MethodData<QJSValue() const>(5, 2, QMC::AccessPublic, 0x80000000 | 6),
        // Method 'close'
        QtMocHelpers::MethodData<void() const>(7, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxSelectorTargets, qt_meta_tag_ZN17AxSelectorTargetsE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxSelectorTargets::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AxSelectorTargetsE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AxSelectorTargetsE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN17AxSelectorTargetsE_t>.metaTypes,
    nullptr
} };

void AxSelectorTargets::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxSelectorTargets *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->setSize((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 1: { QJSValue _r = _t->exec();
            if (_a[0]) *reinterpret_cast<QJSValue*>(_a[0]) = std::move(_r); }  break;
        case 2: _t->close(); break;
        default: ;
        }
    }
}

const QMetaObject *AxSelectorTargets::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxSelectorTargets::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AxSelectorTargetsE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int AxSelectorTargets::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 3)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 3)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 3;
    }
    return _id;
}
namespace {
struct qt_meta_tag_ZN21AxDownloadsTableModelE_t {};
} // unnamed namespace

template <> constexpr inline auto AxDownloadsTableModel::qt_create_metaobjectdata<qt_meta_tag_ZN21AxDownloadsTableModelE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxDownloadsTableModel"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxDownloadsTableModel, qt_meta_tag_ZN21AxDownloadsTableModelE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxDownloadsTableModel::staticMetaObject = { {
    QMetaObject::SuperData::link<QAbstractTableModel::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN21AxDownloadsTableModelE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN21AxDownloadsTableModelE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN21AxDownloadsTableModelE_t>.metaTypes,
    nullptr
} };

void AxDownloadsTableModel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxDownloadsTableModel *>(_o);
    (void)_t;
    (void)_c;
    (void)_id;
    (void)_a;
}

const QMetaObject *AxDownloadsTableModel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxDownloadsTableModel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN21AxDownloadsTableModelE_t>.strings))
        return static_cast<void*>(this);
    return QAbstractTableModel::qt_metacast(_clname);
}

int AxDownloadsTableModel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QAbstractTableModel::qt_metacall(_c, _id, _a);
    return _id;
}
namespace {
struct qt_meta_tag_ZN27AxDownloadsFilterProxyModelE_t {};
} // unnamed namespace

template <> constexpr inline auto AxDownloadsFilterProxyModel::qt_create_metaobjectdata<qt_meta_tag_ZN27AxDownloadsFilterProxyModelE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxDownloadsFilterProxyModel"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxDownloadsFilterProxyModel, qt_meta_tag_ZN27AxDownloadsFilterProxyModelE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxDownloadsFilterProxyModel::staticMetaObject = { {
    QMetaObject::SuperData::link<QSortFilterProxyModel::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN27AxDownloadsFilterProxyModelE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN27AxDownloadsFilterProxyModelE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN27AxDownloadsFilterProxyModelE_t>.metaTypes,
    nullptr
} };

void AxDownloadsFilterProxyModel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxDownloadsFilterProxyModel *>(_o);
    (void)_t;
    (void)_c;
    (void)_id;
    (void)_a;
}

const QMetaObject *AxDownloadsFilterProxyModel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxDownloadsFilterProxyModel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN27AxDownloadsFilterProxyModelE_t>.strings))
        return static_cast<void*>(this);
    return QSortFilterProxyModel::qt_metacast(_clname);
}

int AxDownloadsFilterProxyModel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QSortFilterProxyModel::qt_metacall(_c, _id, _a);
    return _id;
}
namespace {
struct qt_meta_tag_ZN17AxDialogDownloadsE_t {};
} // unnamed namespace

template <> constexpr inline auto AxDialogDownloads::qt_create_metaobjectdata<qt_meta_tag_ZN17AxDialogDownloadsE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxDialogDownloads",
        "onClicked",
        "",
        "handleSearch",
        "clearSearch"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'onClicked'
        QtMocHelpers::SlotData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'handleSearch'
        QtMocHelpers::SlotData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'clearSearch'
        QtMocHelpers::SlotData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxDialogDownloads, qt_meta_tag_ZN17AxDialogDownloadsE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxDialogDownloads::staticMetaObject = { {
    QMetaObject::SuperData::link<QDialog::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AxDialogDownloadsE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AxDialogDownloadsE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN17AxDialogDownloadsE_t>.metaTypes,
    nullptr
} };

void AxDialogDownloads::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxDialogDownloads *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->onClicked(); break;
        case 1: _t->handleSearch(); break;
        case 2: _t->clearSearch(); break;
        default: ;
        }
    }
    (void)_a;
}

const QMetaObject *AxDialogDownloads::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxDialogDownloads::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17AxDialogDownloadsE_t>.strings))
        return static_cast<void*>(this);
    return QDialog::qt_metacast(_clname);
}

int AxDialogDownloads::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QDialog::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 3)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 3)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 3;
    }
    return _id;
}
namespace {
struct qt_meta_tag_ZN19AxSelectorDownloadsE_t {};
} // unnamed namespace

template <> constexpr inline auto AxSelectorDownloads::qt_create_metaobjectdata<qt_meta_tag_ZN19AxSelectorDownloadsE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxSelectorDownloads",
        "setSize",
        "",
        "w",
        "h",
        "exec",
        "QJSValue",
        "close"
    };

    QtMocHelpers::UintData qt_methods {
        // Method 'setSize'
        QtMocHelpers::MethodData<void(int, int) const>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 }, { QMetaType::Int, 4 },
        }}),
        // Method 'exec'
        QtMocHelpers::MethodData<QJSValue() const>(5, 2, QMC::AccessPublic, 0x80000000 | 6),
        // Method 'close'
        QtMocHelpers::MethodData<void() const>(7, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxSelectorDownloads, qt_meta_tag_ZN19AxSelectorDownloadsE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxSelectorDownloads::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN19AxSelectorDownloadsE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN19AxSelectorDownloadsE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN19AxSelectorDownloadsE_t>.metaTypes,
    nullptr
} };

void AxSelectorDownloads::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxSelectorDownloads *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->setSize((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 1: { QJSValue _r = _t->exec();
            if (_a[0]) *reinterpret_cast<QJSValue*>(_a[0]) = std::move(_r); }  break;
        case 2: _t->close(); break;
        default: ;
        }
    }
}

const QMetaObject *AxSelectorDownloads::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxSelectorDownloads::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN19AxSelectorDownloadsE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int AxSelectorDownloads::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 3)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 3)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 3;
    }
    return _id;
}
namespace {
struct qt_meta_tag_ZN13AxDockWrapperE_t {};
} // unnamed namespace

template <> constexpr inline auto AxDockWrapper::qt_create_metaobjectdata<qt_meta_tag_ZN13AxDockWrapperE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AxDockWrapper",
        "closed",
        "",
        "shown",
        "hidden",
        "setLayout",
        "layoutWrapper",
        "setSize",
        "w",
        "h",
        "show",
        "hide",
        "close",
        "isVisible",
        "setTitle",
        "title"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'closed'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'shown'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'hidden'
        QtMocHelpers::SignalData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'setLayout'
        QtMocHelpers::MethodData<void(QObject *)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QObjectStar, 6 },
        }}),
        // Method 'setSize'
        QtMocHelpers::MethodData<void(int, int) const>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 8 }, { QMetaType::Int, 9 },
        }}),
        // Method 'show'
        QtMocHelpers::MethodData<void()>(10, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'hide'
        QtMocHelpers::MethodData<void()>(11, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'close'
        QtMocHelpers::MethodData<void()>(12, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'isVisible'
        QtMocHelpers::MethodData<bool() const>(13, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'setTitle'
        QtMocHelpers::MethodData<void(const QString &)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 15 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AxDockWrapper, qt_meta_tag_ZN13AxDockWrapperE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AxDockWrapper::staticMetaObject = { {
    QMetaObject::SuperData::link<DockTab::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN13AxDockWrapperE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN13AxDockWrapperE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN13AxDockWrapperE_t>.metaTypes,
    nullptr
} };

void AxDockWrapper::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AxDockWrapper *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->closed(); break;
        case 1: _t->shown(); break;
        case 2: _t->hidden(); break;
        case 3: _t->setLayout((*reinterpret_cast<std::add_pointer_t<QObject*>>(_a[1]))); break;
        case 4: _t->setSize((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 5: _t->show(); break;
        case 6: _t->hide(); break;
        case 7: _t->close(); break;
        case 8: { bool _r = _t->isVisible();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 9: _t->setTitle((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (AxDockWrapper::*)()>(_a, &AxDockWrapper::closed, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (AxDockWrapper::*)()>(_a, &AxDockWrapper::shown, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (AxDockWrapper::*)()>(_a, &AxDockWrapper::hidden, 2))
            return;
    }
}

const QMetaObject *AxDockWrapper::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AxDockWrapper::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN13AxDockWrapperE_t>.strings))
        return static_cast<void*>(this);
    return DockTab::qt_metacast(_clname);
}

int AxDockWrapper::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = DockTab::qt_metacall(_c, _id, _a);
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
void AxDockWrapper::closed()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void AxDockWrapper::shown()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void AxDockWrapper::hidden()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}
QT_WARNING_POP
