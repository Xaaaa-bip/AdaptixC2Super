#pragma once
#include "functions.c"

BOOL char2unicode(char* source, UNICODE_STRING* result) {
    STRING ansiPassword;

    RtlInitAnsiString(&ansiPassword, source);
    return RtlAnsiStringToUnicodeString(result, &ansiPassword, 1) != 0;
}

BOOL get_key_rc4(char* password, byte** hash, int* size) {
    BOOL		 status = FALSE;
    PKERB_ECRYPT pCSystem;

    if (!NT_SUCCESS(CDLocateCSystem(rc4_hmac, &pCSystem))) {
        PRINT_OUT("[x] Failed to call CDLocateCSystem");
        return TRUE;
    }

    STRING ansiPassword;
    UNICODE_STRING unicodePassword;
    UNICODE_STRING Salt;
    *size = pCSystem->KeySize;

    char2unicode(password, &unicodePassword);
    RtlInitUnicodeString(&Salt, L"");

    *hash = MemAlloc(pCSystem->KeySize);
    if (*hash) {
        pCSystem->HashPassword_NT6(&unicodePassword, &Salt, 4096, *hash);
        return FALSE;
    }
    else {
        PRINT_OUT("[x] Failed alloc memory");
        return TRUE;
    }
}

BOOL get_key_aes128(char* domain, char* username, char* password, byte** hash, int* size) {
    BOOL		 status = FALSE;
    PKERB_ECRYPT pCSystem;

    if (!NT_SUCCESS(CDLocateCSystem(aes128_cts_hmac_sha1, &pCSystem))) {
        PRINT_OUT("[x] Failed to call CDLocateCSystem");
        return TRUE;
    }
    *size = pCSystem->KeySize;

    int domain_size = my_strlen(domain);
    int username_size = my_strlen(username);

    char* salt;
    if (username[username_size - 1] == '$') {
        salt = MemAlloc(domain_size * 2 + username_size + 5);
        if (!salt) {
            PRINT_OUT("[x] Failed alloc memory");
            return TRUE;
        }
        int i, j;
        for (i = 0; i < domain_size; i++)
            salt[i] = my_toupper(domain[i]);
        salt[i++] = 'h';
        salt[i++] = 'o';
        salt[i++] = 's';
        salt[i++] = 't';
        for (j = 0; j < username_size - 1; j++)
            salt[i + j] = my_tolower(username[j]);
        i = i + j;
        salt[i++] = '.';
        for (j = 0; j < domain_size; j++)
            salt[i + j] = my_tolower(domain[j]);
        salt[domain_size * 2 + username_size + 4] = 0;
    }
    else {
        salt = MemAlloc(domain_size + username_size + 1);
        if (!salt) {
            PRINT_OUT("[x] Failed alloc memory");
            return TRUE;
        }
        int i, j;
        for (i = 0; i < domain_size; i++)
            salt[i] = my_toupper(domain[i]);
        for (j = 0; j < username_size; j++)
            salt[i + j] = my_tolower(username[j]);
        salt[domain_size + username_size] = 0;
    }

    UNICODE_STRING unicodePassword;
    UNICODE_STRING unicodeSalt;

    char2unicode(password, &unicodePassword);
    char2unicode(salt, &unicodeSalt);

    *hash = MemAlloc(pCSystem->KeySize);
    if (*hash) {
        pCSystem->HashPassword_NT6(&unicodePassword, &unicodeSalt, 4096, *hash);
        return FALSE;
    }
    else {
        PRINT_OUT("[x] Failed alloc memory");
        return TRUE;
    }
}

BOOL get_key_aes128_with_salt(char* domain, char* username, char* password, char* customSalt, byte** hash, int* size) {
    BOOL		 status = FALSE;
    PKERB_ECRYPT pCSystem;

    if (!NT_SUCCESS(CDLocateCSystem(aes128_cts_hmac_sha1, &pCSystem))) {
        PRINT_OUT("[x] Failed to call CDLocateCSystem");
        return TRUE;
    }
    *size = pCSystem->KeySize;

    char* salt;
    if (customSalt && my_strlen(customSalt) > 0) {
        int salt_len = my_strlen(customSalt);
        salt = MemAlloc(salt_len + 1);
        if (!salt) {
            PRINT_OUT("[x] Failed alloc memory");
            return TRUE;
        }
        MemCpy(salt, customSalt, salt_len + 1);
    }
    else {
        int domain_size = my_strlen(domain);
        int username_size = my_strlen(username);

        if (username[username_size - 1] == '$') {
            salt = MemAlloc(domain_size * 2 + username_size + 5);
            if (!salt) {
                PRINT_OUT("[x] Failed alloc memory");
                return TRUE;
            }
            int i, j;
            for (i = 0; i < domain_size; i++)
                salt[i] = my_toupper(domain[i]);
            salt[i++] = 'h';
            salt[i++] = 'o';
            salt[i++] = 's';
            salt[i++] = 't';
            for (j = 0; j < username_size - 1; j++)
                salt[i + j] = my_tolower(username[j]);
            i = i + j;
            salt[i++] = '.';
            for (j = 0; j < domain_size; j++)
                salt[i + j] = my_tolower(domain[j]);
            salt[domain_size * 2 + username_size + 4] = 0;
        }
        else {
            salt = MemAlloc(domain_size + username_size + 1);
            if (!salt) {
                PRINT_OUT("[x] Failed alloc memory");
                return TRUE;
            }
            int i, j;
            for (i = 0; i < domain_size; i++)
                salt[i] = my_toupper(domain[i]);
            for (j = 0; j < username_size; j++)
                salt[i + j] = my_tolower(username[j]);
            salt[domain_size + username_size] = 0;
        }
    }

    UNICODE_STRING unicodePassword;
    UNICODE_STRING unicodeSalt;

    char2unicode(password, &unicodePassword);
    char2unicode(salt, &unicodeSalt);

    *hash = MemAlloc(pCSystem->KeySize);
    if (*hash) {
        pCSystem->HashPassword_NT6(&unicodePassword, &unicodeSalt, 4096, *hash);
        return FALSE;
    }
    else {
        PRINT_OUT("[x] Failed alloc memory");
        return TRUE;
    }
}

BOOL get_key_aes256_with_salt(char* domain, char* username, char* password, char* customSalt, byte** hash, int* size) {
    BOOL		 status = FALSE;
    PKERB_ECRYPT pCSystem;

    if (!NT_SUCCESS(CDLocateCSystem(aes256_cts_hmac_sha1, &pCSystem))) {
        PRINT_OUT("[x] Failed to call CDLocateCSystem");
        return TRUE;
    }
    *size = pCSystem->KeySize;

    char* salt;
    if (customSalt && my_strlen(customSalt) > 0) {
        // Use custom salt from KDC (for Protected Users)
        int salt_len = my_strlen(customSalt);
        salt = MemAlloc(salt_len + 1);
        if (!salt) {
            PRINT_OUT("[x] Failed alloc memory");
            return TRUE;
        }
        MemCpy(salt, customSalt, salt_len + 1);
    }
    else {
        // Generate default salt
        int domain_size = my_strlen(domain);
        int username_size = my_strlen(username);

        if (username[username_size - 1] == '$') {
            salt = MemAlloc(domain_size * 2 + username_size + 5);
            if (!salt) {
                PRINT_OUT("[x] Failed alloc memory");
                return TRUE;
            }
            int i, j;
            for (i = 0; i < domain_size; i++)
                salt[i] = my_toupper(domain[i]);
            salt[i++] = 'h';
            salt[i++] = 'o';
            salt[i++] = 's';
            salt[i++] = 't';
            for (j = 0; j < username_size - 1; j++)
                salt[i + j] = my_tolower(username[j]);
            i = i + j;
            salt[i++] = '.';
            for (j = 0; j < domain_size; j++)
                salt[i + j] = my_tolower(domain[j]);
            salt[domain_size * 2 + username_size + 4] = 0;
        }
        else {
            salt = MemAlloc(domain_size + username_size + 1);
            if (!salt) {
                PRINT_OUT("[x] Failed alloc memory");
                return TRUE;
            }
            int i, j;
            for (i = 0; i < domain_size; i++)
                salt[i] = my_toupper(domain[i]);
            for (j = 0; j < username_size; j++)
                salt[i + j] = username[j];
            salt[domain_size + username_size] = 0;
        }
    }

    UNICODE_STRING unicodePassword;
    UNICODE_STRING unicodeSalt;

    char2unicode(password, &unicodePassword);
    char2unicode(salt, &unicodeSalt);

    *hash = MemAlloc(pCSystem->KeySize);
    if (*hash) {
        pCSystem->HashPassword_NT6(&unicodePassword, &unicodeSalt, 4096, *hash);
        return FALSE;
    }
    else {
        PRINT_OUT("[x] Failed alloc memory");
        return TRUE;
    }
}

BOOL get_key_aes256(char* domain, char* username, char* password, byte** hash, int* size) {
    return get_key_aes256_with_salt(domain, username, password, NULL, hash, size);
}