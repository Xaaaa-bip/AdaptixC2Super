/****************************************************************************
** Meta object code from reading C++ file 'BridgeApp.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../Headers/Client/AxScript/BridgeApp.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'BridgeApp.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN9BridgeAppE_t {};
} // unnamed namespace

template <> constexpr inline auto BridgeApp::qt_create_metaobjectdata<qt_meta_tag_ZN9BridgeAppE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "BridgeApp",
        "consoleMessage",
        "",
        "msg",
        "consoleError",
        "engineError",
        "agents",
        "QJSValue",
        "agent_info",
        "id",
        "property",
        "agent_hide",
        "agent_remove",
        "agent_set_color",
        "background",
        "foreground",
        "reset",
        "agent_set_impersonate",
        "impersonate",
        "elevated",
        "agent_set_mark",
        "mark",
        "agent_set_tag",
        "tag",
        "agent_update_data",
        "data",
        "arch",
        "bof_pack",
        "types",
        "args",
        "copy_to_clipboard",
        "text",
        "convert_to_code",
        "language",
        "base64Data",
        "varName",
        "console_message",
        "message",
        "type",
        "credentials",
        "credentials_add",
        "username",
        "password",
        "realm",
        "storage",
        "host",
        "credentials_add_list",
        "QVariantList",
        "array",
        "create_command",
        "name",
        "description",
        "example",
        "create_commands_group",
        "downloads",
        "decode_data",
        "algorithm",
        "key",
        "decode_file",
        "path",
        "encode_data",
        "encode_file",
        "execute_alias",
        "cmdline",
        "command",
        "hook",
        "handler",
        "execute_alias_hook",
        "execute_alias_handler",
        "execute_browser",
        "execute_command",
        "execute_command_hook",
        "execute_command_handler",
        "file_basename",
        "file_dirname",
        "file_extension",
        "file_exists",
        "file_read",
        "file_size",
        "file_write_text",
        "content",
        "append",
        "file_write_binary",
        "base64Content",
        "format_size",
        "size",
        "format_time",
        "format",
        "time",
        "get_commands",
        "get_project",
        "hash",
        "length",
        "input",
        "ids",
        "interfaces",
        "is64",
        "isactive",
        "isadmin",
        "log",
        "log_error",
        "open_agent_console",
        "open_access_tunnel",
        "socks4",
        "socks5",
        "lportfwd",
        "rportfwd",
        "open_browser_files",
        "open_browser_process",
        "open_remote_terminal",
        "open_remote_shell",
        "prompt_confirm",
        "title",
        "prompt_open_file",
        "caption",
        "filter",
        "prompt_open_dir",
        "prompt_save_file",
        "filename",
        "random_string",
        "setname",
        "random_int",
        "min",
        "max",
        "register_commands_group",
        "obj",
        "os",
        "listeners",
        "script_import",
        "script_load",
        "script_unload",
        "script_dir",
        "screenshots",
        "service_command",
        "service",
        "show_message",
        "targets",
        "targets_add",
        "computer",
        "domain",
        "address",
        "osDesc",
        "info",
        "alive",
        "targets_add_list",
        "ticks",
        "tunnels",
        "validate_command"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'consoleMessage'
        QtMocHelpers::SignalData<void(const QString &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Signal 'consoleError'
        QtMocHelpers::SignalData<void(const QString &)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Signal 'engineError'
        QtMocHelpers::SignalData<void(const QString &)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Slot 'agents'
        QtMocHelpers::SlotData<QJSValue() const>(6, 2, QMC::AccessPublic, 0x80000000 | 7),
        // Slot 'agent_info'
        QtMocHelpers::SlotData<QJSValue(const QString &, const QString &) const>(8, 2, QMC::AccessPublic, 0x80000000 | 7, {{
            { QMetaType::QString, 9 }, { QMetaType::QString, 10 },
        }}),
        // Slot 'agent_hide'
        QtMocHelpers::SlotData<void(const QJSValue &)>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 7, 6 },
        }}),
        // Slot 'agent_remove'
        QtMocHelpers::SlotData<void(const QJSValue &)>(12, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 7, 6 },
        }}),
        // Slot 'agent_set_color'
        QtMocHelpers::SlotData<void(const QJSValue &, const QString &, const QString &, const bool)>(13, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 7, 6 }, { QMetaType::QString, 14 }, { QMetaType::QString, 15 }, { QMetaType::Bool, 16 },
        }}),
        // Slot 'agent_set_impersonate'
        QtMocHelpers::SlotData<void(const QString &, const QString &, const bool)>(17, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 9 }, { QMetaType::QString, 18 }, { QMetaType::Bool, 19 },
        }}),
        // Slot 'agent_set_mark'
        QtMocHelpers::SlotData<void(const QJSValue &, const QString &)>(20, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 7, 6 }, { QMetaType::QString, 21 },
        }}),
        // Slot 'agent_set_tag'
        QtMocHelpers::SlotData<void(const QJSValue &, const QString &)>(22, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 7, 6 }, { QMetaType::QString, 23 },
        }}),
        // Slot 'agent_update_data'
        QtMocHelpers::SlotData<void(const QString &, const QJSValue &)>(24, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 9 }, { 0x80000000 | 7, 25 },
        }}),
        // Slot 'arch'
        QtMocHelpers::SlotData<QString(const QString &) const>(26, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 9 },
        }}),
        // Slot 'bof_pack'
        QtMocHelpers::SlotData<QString(const QString &, const QJSValue &)>(27, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 28 }, { 0x80000000 | 7, 29 },
        }}),
        // Slot 'copy_to_clipboard'
        QtMocHelpers::SlotData<void(const QString &)>(30, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 31 },
        }}),
        // Slot 'convert_to_code'
        QtMocHelpers::SlotData<QString(const QString &, const QString &, const QString &) const>(32, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 33 }, { QMetaType::QString, 34 }, { QMetaType::QString, 35 },
        }}),
        // Slot 'convert_to_code'
        QtMocHelpers::SlotData<QString(const QString &, const QString &) const>(32, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QString, {{
            { QMetaType::QString, 33 }, { QMetaType::QString, 34 },
        }}),
        // Slot 'console_message'
        QtMocHelpers::SlotData<void(const QString &, const QString &, const QString &, const QString &)>(36, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 9 }, { QMetaType::QString, 37 }, { QMetaType::QString, 38 }, { QMetaType::QString, 31 },
        }}),
        // Slot 'console_message'
        QtMocHelpers::SlotData<void(const QString &, const QString &, const QString &)>(36, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::QString, 9 }, { QMetaType::QString, 37 }, { QMetaType::QString, 38 },
        }}),
        // Slot 'console_message'
        QtMocHelpers::SlotData<void(const QString &, const QString &)>(36, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::QString, 9 }, { QMetaType::QString, 37 },
        }}),
        // Slot 'credentials'
        QtMocHelpers::SlotData<QJSValue() const>(39, 2, QMC::AccessPublic, 0x80000000 | 7),
        // Slot 'credentials_add'
        QtMocHelpers::SlotData<void(const QString &, const QString &, const QString &, const QString &, const QString &, const QString &, const QString &)>(40, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 41 }, { QMetaType::QString, 42 }, { QMetaType::QString, 43 }, { QMetaType::QString, 38 },
            { QMetaType::QString, 23 }, { QMetaType::QString, 44 }, { QMetaType::QString, 45 },
        }}),
        // Slot 'credentials_add'
        QtMocHelpers::SlotData<void(const QString &, const QString &, const QString &, const QString &, const QString &, const QString &)>(40, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::QString, 41 }, { QMetaType::QString, 42 }, { QMetaType::QString, 43 }, { QMetaType::QString, 38 },
            { QMetaType::QString, 23 }, { QMetaType::QString, 44 },
        }}),
        // Slot 'credentials_add'
        QtMocHelpers::SlotData<void(const QString &, const QString &, const QString &, const QString &, const QString &)>(40, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::QString, 41 }, { QMetaType::QString, 42 }, { QMetaType::QString, 43 }, { QMetaType::QString, 38 },
            { QMetaType::QString, 23 },
        }}),
        // Slot 'credentials_add'
        QtMocHelpers::SlotData<void(const QString &, const QString &, const QString &, const QString &)>(40, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::QString, 41 }, { QMetaType::QString, 42 }, { QMetaType::QString, 43 }, { QMetaType::QString, 38 },
        }}),
        // Slot 'credentials_add'
        QtMocHelpers::SlotData<void(const QString &, const QString &, const QString &)>(40, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::QString, 41 }, { QMetaType::QString, 42 }, { QMetaType::QString, 43 },
        }}),
        // Slot 'credentials_add'
        QtMocHelpers::SlotData<void(const QString &, const QString &)>(40, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::QString, 41 }, { QMetaType::QString, 42 },
        }}),
        // Slot 'credentials_add_list'
        QtMocHelpers::SlotData<void(const QVariantList &)>(46, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 47, 48 },
        }}),
        // Slot 'create_command'
        QtMocHelpers::SlotData<QObject *(const QString &, const QString &, const QString &, const QString &)>(49, 2, QMC::AccessPublic, QMetaType::QObjectStar, {{
            { QMetaType::QString, 50 }, { QMetaType::QString, 51 }, { QMetaType::QString, 52 }, { QMetaType::QString, 37 },
        }}),
        // Slot 'create_command'
        QtMocHelpers::SlotData<QObject *(const QString &, const QString &, const QString &)>(49, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QObjectStar, {{
            { QMetaType::QString, 50 }, { QMetaType::QString, 51 }, { QMetaType::QString, 52 },
        }}),
        // Slot 'create_command'
        QtMocHelpers::SlotData<QObject *(const QString &, const QString &)>(49, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QObjectStar, {{
            { QMetaType::QString, 50 }, { QMetaType::QString, 51 },
        }}),
        // Slot 'create_commands_group'
        QtMocHelpers::SlotData<QObject *(const QString &, const QJSValue &)>(53, 2, QMC::AccessPublic, QMetaType::QObjectStar, {{
            { QMetaType::QString, 50 }, { 0x80000000 | 7, 48 },
        }}),
        // Slot 'downloads'
        QtMocHelpers::SlotData<QJSValue() const>(54, 2, QMC::AccessPublic, 0x80000000 | 7),
        // Slot 'decode_data'
        QtMocHelpers::SlotData<QString(const QString &, const QString &, const QString &) const>(55, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 56 }, { QMetaType::QString, 25 }, { QMetaType::QString, 57 },
        }}),
        // Slot 'decode_data'
        QtMocHelpers::SlotData<QString(const QString &, const QString &) const>(55, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QString, {{
            { QMetaType::QString, 56 }, { QMetaType::QString, 25 },
        }}),
        // Slot 'decode_file'
        QtMocHelpers::SlotData<QString(const QString &, const QString &, const QString &) const>(58, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 56 }, { QMetaType::QString, 59 }, { QMetaType::QString, 57 },
        }}),
        // Slot 'decode_file'
        QtMocHelpers::SlotData<QString(const QString &, const QString &) const>(58, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QString, {{
            { QMetaType::QString, 56 }, { QMetaType::QString, 59 },
        }}),
        // Slot 'encode_data'
        QtMocHelpers::SlotData<QString(const QString &, const QString &, const QString &) const>(60, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 56 }, { QMetaType::QString, 25 }, { QMetaType::QString, 57 },
        }}),
        // Slot 'encode_data'
        QtMocHelpers::SlotData<QString(const QString &, const QString &) const>(60, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QString, {{
            { QMetaType::QString, 56 }, { QMetaType::QString, 25 },
        }}),
        // Slot 'encode_file'
        QtMocHelpers::SlotData<QString(const QString &, const QString &, const QString &) const>(61, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 56 }, { QMetaType::QString, 59 }, { QMetaType::QString, 57 },
        }}),
        // Slot 'encode_file'
        QtMocHelpers::SlotData<QString(const QString &, const QString &) const>(61, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QString, {{
            { QMetaType::QString, 56 }, { QMetaType::QString, 59 },
        }}),
        // Slot 'execute_alias'
        QtMocHelpers::SlotData<void(const QString &, const QString &, const QString &, const QString &, const QJSValue &, const QJSValue &) const>(62, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 9 }, { QMetaType::QString, 63 }, { QMetaType::QString, 64 }, { QMetaType::QString, 37 },
            { 0x80000000 | 7, 65 }, { 0x80000000 | 7, 66 },
        }}),
        // Slot 'execute_alias'
        QtMocHelpers::SlotData<void(const QString &, const QString &, const QString &, const QString &, const QJSValue &) const>(62, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::QString, 9 }, { QMetaType::QString, 63 }, { QMetaType::QString, 64 }, { QMetaType::QString, 37 },
            { 0x80000000 | 7, 65 },
        }}),
        // Slot 'execute_alias'
        QtMocHelpers::SlotData<void(const QString &, const QString &, const QString &, const QString &) const>(62, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::QString, 9 }, { QMetaType::QString, 63 }, { QMetaType::QString, 64 }, { QMetaType::QString, 37 },
        }}),
        // Slot 'execute_alias'
        QtMocHelpers::SlotData<void(const QString &, const QString &, const QString &) const>(62, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::QString, 9 }, { QMetaType::QString, 63 }, { QMetaType::QString, 64 },
        }}),
        // Slot 'execute_alias_hook'
        QtMocHelpers::SlotData<void(const QString &, const QString &, const QString &, const QString &, const QJSValue &) const>(67, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 9 }, { QMetaType::QString, 63 }, { QMetaType::QString, 64 }, { QMetaType::QString, 37 },
            { 0x80000000 | 7, 65 },
        }}),
        // Slot 'execute_alias_handler'
        QtMocHelpers::SlotData<void(const QString &, const QString &, const QString &, const QString &, const QJSValue &) const>(68, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 9 }, { QMetaType::QString, 63 }, { QMetaType::QString, 64 }, { QMetaType::QString, 37 },
            { 0x80000000 | 7, 66 },
        }}),
        // Slot 'execute_browser'
        QtMocHelpers::SlotData<void(const QString &, const QString &) const>(69, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 9 }, { QMetaType::QString, 64 },
        }}),
        // Slot 'execute_command'
        QtMocHelpers::SlotData<void(const QString &, const QString &, const QJSValue &, const QJSValue &) const>(70, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 9 }, { QMetaType::QString, 64 }, { 0x80000000 | 7, 65 }, { 0x80000000 | 7, 66 },
        }}),
        // Slot 'execute_command'
        QtMocHelpers::SlotData<void(const QString &, const QString &, const QJSValue &) const>(70, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::QString, 9 }, { QMetaType::QString, 64 }, { 0x80000000 | 7, 65 },
        }}),
        // Slot 'execute_command'
        QtMocHelpers::SlotData<void(const QString &, const QString &) const>(70, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::QString, 9 }, { QMetaType::QString, 64 },
        }}),
        // Slot 'execute_command_hook'
        QtMocHelpers::SlotData<void(const QString &, const QString &, const QJSValue &) const>(71, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 9 }, { QMetaType::QString, 64 }, { 0x80000000 | 7, 65 },
        }}),
        // Slot 'execute_command_handler'
        QtMocHelpers::SlotData<void(const QString &, const QString &, const QJSValue &) const>(72, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 9 }, { QMetaType::QString, 64 }, { 0x80000000 | 7, 66 },
        }}),
        // Slot 'file_basename'
        QtMocHelpers::SlotData<QString(const QString &) const>(73, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 59 },
        }}),
        // Slot 'file_dirname'
        QtMocHelpers::SlotData<QString(const QString &) const>(74, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 59 },
        }}),
        // Slot 'file_extension'
        QtMocHelpers::SlotData<QString(const QString &) const>(75, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 59 },
        }}),
        // Slot 'file_exists'
        QtMocHelpers::SlotData<bool(const QString &) const>(76, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 59 },
        }}),
        // Slot 'file_read'
        QtMocHelpers::SlotData<QString(QString) const>(77, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 59 },
        }}),
        // Slot 'file_size'
        QtMocHelpers::SlotData<qint64(const QString &) const>(78, 2, QMC::AccessPublic, QMetaType::LongLong, {{
            { QMetaType::QString, 59 },
        }}),
        // Slot 'file_write_text'
        QtMocHelpers::SlotData<bool(QString, const QString &, bool) const>(79, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 59 }, { QMetaType::QString, 80 }, { QMetaType::Bool, 81 },
        }}),
        // Slot 'file_write_text'
        QtMocHelpers::SlotData<bool(QString, const QString &) const>(79, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Bool, {{
            { QMetaType::QString, 59 }, { QMetaType::QString, 80 },
        }}),
        // Slot 'file_write_binary'
        QtMocHelpers::SlotData<bool(QString, const QString &) const>(82, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 59 }, { QMetaType::QString, 83 },
        }}),
        // Slot 'format_size'
        QtMocHelpers::SlotData<QString(const int &) const>(84, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::Int, 85 },
        }}),
        // Slot 'format_time'
        QtMocHelpers::SlotData<QString(const QString &, const int &) const>(86, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 87 }, { QMetaType::Int, 88 },
        }}),
        // Slot 'get_commands'
        QtMocHelpers::SlotData<QJSValue(const QString &) const>(89, 2, QMC::AccessPublic, 0x80000000 | 7, {{
            { QMetaType::QString, 9 },
        }}),
        // Slot 'get_project'
        QtMocHelpers::SlotData<QString() const>(90, 2, QMC::AccessPublic, QMetaType::QString),
        // Slot 'hash'
        QtMocHelpers::SlotData<QString(const QString &, int, const QString &)>(91, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 56 }, { QMetaType::Int, 92 }, { QMetaType::QString, 93 },
        }}),
        // Slot 'ids'
        QtMocHelpers::SlotData<QJSValue() const>(94, 2, QMC::AccessPublic, 0x80000000 | 7),
        // Slot 'interfaces'
        QtMocHelpers::SlotData<QJSValue() const>(95, 2, QMC::AccessPublic, 0x80000000 | 7),
        // Slot 'is64'
        QtMocHelpers::SlotData<bool(const QString &) const>(96, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 9 },
        }}),
        // Slot 'isactive'
        QtMocHelpers::SlotData<bool(const QString &) const>(97, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 9 },
        }}),
        // Slot 'isadmin'
        QtMocHelpers::SlotData<bool(const QString &) const>(98, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 9 },
        }}),
        // Slot 'log'
        QtMocHelpers::SlotData<void(const QString &)>(99, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 31 },
        }}),
        // Slot 'log_error'
        QtMocHelpers::SlotData<void(const QString &)>(100, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 31 },
        }}),
        // Slot 'open_agent_console'
        QtMocHelpers::SlotData<void(const QString &)>(101, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 9 },
        }}),
        // Slot 'open_access_tunnel'
        QtMocHelpers::SlotData<void(const QString &, bool, bool, bool, bool)>(102, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 9 }, { QMetaType::Bool, 103 }, { QMetaType::Bool, 104 }, { QMetaType::Bool, 105 },
            { QMetaType::Bool, 106 },
        }}),
        // Slot 'open_browser_files'
        QtMocHelpers::SlotData<void(const QString &)>(107, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 9 },
        }}),
        // Slot 'open_browser_process'
        QtMocHelpers::SlotData<void(const QString &)>(108, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 9 },
        }}),
        // Slot 'open_remote_terminal'
        QtMocHelpers::SlotData<void(const QString &)>(109, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 9 },
        }}),
        // Slot 'open_remote_shell'
        QtMocHelpers::SlotData<void(const QString &)>(110, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 9 },
        }}),
        // Slot 'prompt_confirm'
        QtMocHelpers::SlotData<bool(const QString &, const QString &)>(111, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 112 }, { QMetaType::QString, 31 },
        }}),
        // Slot 'prompt_open_file'
        QtMocHelpers::SlotData<QString(const QString &, const QString &)>(113, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 114 }, { QMetaType::QString, 115 },
        }}),
        // Slot 'prompt_open_file'
        QtMocHelpers::SlotData<QString(const QString &)>(113, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QString, {{
            { QMetaType::QString, 114 },
        }}),
        // Slot 'prompt_open_file'
        QtMocHelpers::SlotData<QString()>(113, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QString),
        // Slot 'prompt_open_dir'
        QtMocHelpers::SlotData<QString(const QString &)>(116, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 114 },
        }}),
        // Slot 'prompt_open_dir'
        QtMocHelpers::SlotData<QString()>(116, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QString),
        // Slot 'prompt_save_file'
        QtMocHelpers::SlotData<QString(const QString &, const QString &, const QString &)>(117, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 118 }, { QMetaType::QString, 114 }, { QMetaType::QString, 115 },
        }}),
        // Slot 'prompt_save_file'
        QtMocHelpers::SlotData<QString(const QString &, const QString &)>(117, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QString, {{
            { QMetaType::QString, 118 }, { QMetaType::QString, 114 },
        }}),
        // Slot 'prompt_save_file'
        QtMocHelpers::SlotData<QString(const QString &)>(117, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QString, {{
            { QMetaType::QString, 118 },
        }}),
        // Slot 'random_string'
        QtMocHelpers::SlotData<QString(const int, const QString &)>(119, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::Int, 92 }, { QMetaType::QString, 120 },
        }}),
        // Slot 'random_int'
        QtMocHelpers::SlotData<int(const int, const int)>(121, 2, QMC::AccessPublic, QMetaType::Int, {{
            { QMetaType::Int, 122 }, { QMetaType::Int, 123 },
        }}),
        // Slot 'register_commands_group'
        QtMocHelpers::SlotData<void(QObject *, const QJSValue &, const QJSValue &, const QJSValue &)>(124, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QObjectStar, 125 }, { 0x80000000 | 7, 6 }, { 0x80000000 | 7, 126 }, { 0x80000000 | 7, 127 },
        }}),
        // Slot 'script_import'
        QtMocHelpers::SlotData<void(const QString &)>(128, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 59 },
        }}),
        // Slot 'script_load'
        QtMocHelpers::SlotData<void(const QString &)>(129, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 59 },
        }}),
        // Slot 'script_unload'
        QtMocHelpers::SlotData<void(const QString &)>(130, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 59 },
        }}),
        // Slot 'script_dir'
        QtMocHelpers::SlotData<QString()>(131, 2, QMC::AccessPublic, QMetaType::QString),
        // Slot 'screenshots'
        QtMocHelpers::SlotData<QJSValue()>(132, 2, QMC::AccessPublic, 0x80000000 | 7),
        // Slot 'service_command'
        QtMocHelpers::SlotData<void(const QString &, const QString &, const QJSValue &)>(133, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 134 }, { QMetaType::QString, 64 }, { 0x80000000 | 7, 29 },
        }}),
        // Slot 'service_command'
        QtMocHelpers::SlotData<void(const QString &, const QString &)>(133, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::QString, 134 }, { QMetaType::QString, 64 },
        }}),
        // Slot 'show_message'
        QtMocHelpers::SlotData<void(const QString &, const QString &)>(135, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 112 }, { QMetaType::QString, 31 },
        }}),
        // Slot 'targets'
        QtMocHelpers::SlotData<QJSValue() const>(136, 2, QMC::AccessPublic, 0x80000000 | 7),
        // Slot 'targets_add'
        QtMocHelpers::SlotData<void(const QString &, const QString &, const QString &, const QString &, const QString &, const QString &, const QString &, bool)>(137, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 138 }, { QMetaType::QString, 139 }, { QMetaType::QString, 140 }, { QMetaType::QString, 126 },
            { QMetaType::QString, 141 }, { QMetaType::QString, 23 }, { QMetaType::QString, 142 }, { QMetaType::Bool, 143 },
        }}),
        // Slot 'targets_add'
        QtMocHelpers::SlotData<void(const QString &, const QString &, const QString &, const QString &, const QString &, const QString &, const QString &)>(137, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::QString, 138 }, { QMetaType::QString, 139 }, { QMetaType::QString, 140 }, { QMetaType::QString, 126 },
            { QMetaType::QString, 141 }, { QMetaType::QString, 23 }, { QMetaType::QString, 142 },
        }}),
        // Slot 'targets_add'
        QtMocHelpers::SlotData<void(const QString &, const QString &, const QString &, const QString &, const QString &, const QString &)>(137, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::QString, 138 }, { QMetaType::QString, 139 }, { QMetaType::QString, 140 }, { QMetaType::QString, 126 },
            { QMetaType::QString, 141 }, { QMetaType::QString, 23 },
        }}),
        // Slot 'targets_add'
        QtMocHelpers::SlotData<void(const QString &, const QString &, const QString &, const QString &, const QString &)>(137, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::QString, 138 }, { QMetaType::QString, 139 }, { QMetaType::QString, 140 }, { QMetaType::QString, 126 },
            { QMetaType::QString, 141 },
        }}),
        // Slot 'targets_add'
        QtMocHelpers::SlotData<void(const QString &, const QString &, const QString &, const QString &)>(137, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::QString, 138 }, { QMetaType::QString, 139 }, { QMetaType::QString, 140 }, { QMetaType::QString, 126 },
        }}),
        // Slot 'targets_add'
        QtMocHelpers::SlotData<void(const QString &, const QString &, const QString &)>(137, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::QString, 138 }, { QMetaType::QString, 139 }, { QMetaType::QString, 140 },
        }}),
        // Slot 'targets_add_list'
        QtMocHelpers::SlotData<void(const QVariantList &)>(144, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 47, 48 },
        }}),
        // Slot 'ticks'
        QtMocHelpers::SlotData<int()>(145, 2, QMC::AccessPublic, QMetaType::Int),
        // Slot 'tunnels'
        QtMocHelpers::SlotData<QJSValue()>(146, 2, QMC::AccessPublic, 0x80000000 | 7),
        // Slot 'validate_command'
        QtMocHelpers::SlotData<QJSValue(const QString &, const QString &) const>(147, 2, QMC::AccessPublic, 0x80000000 | 7, {{
            { QMetaType::QString, 9 }, { QMetaType::QString, 64 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<BridgeApp, qt_meta_tag_ZN9BridgeAppE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject BridgeApp::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN9BridgeAppE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN9BridgeAppE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN9BridgeAppE_t>.metaTypes,
    nullptr
} };

void BridgeApp::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<BridgeApp *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->consoleMessage((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 1: _t->consoleError((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 2: _t->engineError((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 3: { QJSValue _r = _t->agents();
            if (_a[0]) *reinterpret_cast<QJSValue*>(_a[0]) = std::move(_r); }  break;
        case 4: { QJSValue _r = _t->agent_info((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QJSValue*>(_a[0]) = std::move(_r); }  break;
        case 5: _t->agent_hide((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1]))); break;
        case 6: _t->agent_remove((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1]))); break;
        case 7: _t->agent_set_color((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[4]))); break;
        case 8: _t->agent_set_impersonate((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[3]))); break;
        case 9: _t->agent_set_mark((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 10: _t->agent_set_tag((*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 11: _t->agent_update_data((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2]))); break;
        case 12: { QString _r = _t->arch((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 13: { QString _r = _t->bof_pack((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 14: _t->copy_to_clipboard((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 15: { QString _r = _t->convert_to_code((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 16: { QString _r = _t->convert_to_code((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 17: _t->console_message((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4]))); break;
        case 18: _t->console_message((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3]))); break;
        case 19: _t->console_message((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 20: { QJSValue _r = _t->credentials();
            if (_a[0]) *reinterpret_cast<QJSValue*>(_a[0]) = std::move(_r); }  break;
        case 21: _t->credentials_add((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[5])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[6])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[7]))); break;
        case 22: _t->credentials_add((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[5])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[6]))); break;
        case 23: _t->credentials_add((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[5]))); break;
        case 24: _t->credentials_add((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4]))); break;
        case 25: _t->credentials_add((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3]))); break;
        case 26: _t->credentials_add((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 27: _t->credentials_add_list((*reinterpret_cast<std::add_pointer_t<QVariantList>>(_a[1]))); break;
        case 28: { QObject* _r = _t->create_command((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4])));
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 29: { QObject* _r = _t->create_command((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])));
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 30: { QObject* _r = _t->create_command((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 31: { QObject* _r = _t->create_commands_group((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QObject**>(_a[0]) = std::move(_r); }  break;
        case 32: { QJSValue _r = _t->downloads();
            if (_a[0]) *reinterpret_cast<QJSValue*>(_a[0]) = std::move(_r); }  break;
        case 33: { QString _r = _t->decode_data((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 34: { QString _r = _t->decode_data((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 35: { QString _r = _t->decode_file((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 36: { QString _r = _t->decode_file((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 37: { QString _r = _t->encode_data((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 38: { QString _r = _t->encode_data((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 39: { QString _r = _t->encode_file((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 40: { QString _r = _t->encode_file((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 41: _t->execute_alias((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[5])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[6]))); break;
        case 42: _t->execute_alias((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[5]))); break;
        case 43: _t->execute_alias((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4]))); break;
        case 44: _t->execute_alias((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3]))); break;
        case 45: _t->execute_alias_hook((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[5]))); break;
        case 46: _t->execute_alias_handler((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[5]))); break;
        case 47: _t->execute_browser((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 48: _t->execute_command((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[4]))); break;
        case 49: _t->execute_command((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3]))); break;
        case 50: _t->execute_command((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 51: _t->execute_command_hook((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3]))); break;
        case 52: _t->execute_command_handler((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3]))); break;
        case 53: { QString _r = _t->file_basename((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 54: { QString _r = _t->file_dirname((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 55: { QString _r = _t->file_extension((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 56: { bool _r = _t->file_exists((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 57: { QString _r = _t->file_read((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 58: { qint64 _r = _t->file_size((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<qint64*>(_a[0]) = std::move(_r); }  break;
        case 59: { bool _r = _t->file_write_text((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[3])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 60: { bool _r = _t->file_write_text((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 61: { bool _r = _t->file_write_binary((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 62: { QString _r = _t->format_size((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 63: { QString _r = _t->format_time((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 64: { QJSValue _r = _t->get_commands((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QJSValue*>(_a[0]) = std::move(_r); }  break;
        case 65: { QString _r = _t->get_project();
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 66: { QString _r = _t->hash((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 67: { QJSValue _r = _t->ids();
            if (_a[0]) *reinterpret_cast<QJSValue*>(_a[0]) = std::move(_r); }  break;
        case 68: { QJSValue _r = _t->interfaces();
            if (_a[0]) *reinterpret_cast<QJSValue*>(_a[0]) = std::move(_r); }  break;
        case 69: { bool _r = _t->is64((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 70: { bool _r = _t->isactive((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 71: { bool _r = _t->isadmin((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 72: _t->log((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 73: _t->log_error((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 74: _t->open_agent_console((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 75: _t->open_access_tunnel((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[5]))); break;
        case 76: _t->open_browser_files((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 77: _t->open_browser_process((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 78: _t->open_remote_terminal((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 79: _t->open_remote_shell((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 80: { bool _r = _t->prompt_confirm((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 81: { QString _r = _t->prompt_open_file((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 82: { QString _r = _t->prompt_open_file((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 83: { QString _r = _t->prompt_open_file();
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 84: { QString _r = _t->prompt_open_dir((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 85: { QString _r = _t->prompt_open_dir();
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 86: { QString _r = _t->prompt_save_file((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 87: { QString _r = _t->prompt_save_file((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 88: { QString _r = _t->prompt_save_file((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 89: { QString _r = _t->random_string((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 90: { int _r = _t->random_int((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])));
            if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
        case 91: _t->register_commands_group((*reinterpret_cast<std::add_pointer_t<QObject*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[4]))); break;
        case 92: _t->script_import((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 93: _t->script_load((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 94: _t->script_unload((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 95: { QString _r = _t->script_dir();
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 96: { QJSValue _r = _t->screenshots();
            if (_a[0]) *reinterpret_cast<QJSValue*>(_a[0]) = std::move(_r); }  break;
        case 97: _t->service_command((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QJSValue>>(_a[3]))); break;
        case 98: _t->service_command((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 99: _t->show_message((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 100: { QJSValue _r = _t->targets();
            if (_a[0]) *reinterpret_cast<QJSValue*>(_a[0]) = std::move(_r); }  break;
        case 101: _t->targets_add((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[5])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[6])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[7])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[8]))); break;
        case 102: _t->targets_add((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[5])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[6])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[7]))); break;
        case 103: _t->targets_add((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[5])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[6]))); break;
        case 104: _t->targets_add((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[5]))); break;
        case 105: _t->targets_add((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4]))); break;
        case 106: _t->targets_add((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3]))); break;
        case 107: _t->targets_add_list((*reinterpret_cast<std::add_pointer_t<QVariantList>>(_a[1]))); break;
        case 108: { int _r = _t->ticks();
            if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
        case 109: { QJSValue _r = _t->tunnels();
            if (_a[0]) *reinterpret_cast<QJSValue*>(_a[0]) = std::move(_r); }  break;
        case 110: { QJSValue _r = _t->validate_command((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QJSValue*>(_a[0]) = std::move(_r); }  break;
        default: ;
        }
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 5:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 6:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 7:
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
        case 10:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 11:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 13:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 31:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 41:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 5:
            case 4:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 42:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 4:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 45:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 4:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 46:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 4:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 48:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 3:
            case 2:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 49:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 2:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 51:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 2:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 52:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 2:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 91:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 3:
            case 2:
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        case 97:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 2:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QJSValue >(); break;
            }
            break;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (BridgeApp::*)(const QString & )>(_a, &BridgeApp::consoleMessage, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (BridgeApp::*)(const QString & )>(_a, &BridgeApp::consoleError, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (BridgeApp::*)(const QString & )>(_a, &BridgeApp::engineError, 2))
            return;
    }
}

const QMetaObject *BridgeApp::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *BridgeApp::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN9BridgeAppE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int BridgeApp::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 111)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 111;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 111)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 111;
    }
    return _id;
}

// SIGNAL 0
void BridgeApp::consoleMessage(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void BridgeApp::consoleError(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void BridgeApp::engineError(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}
QT_WARNING_POP
