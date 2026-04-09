#include "Crypt.h"

void RC4Init(unsigned char* key, unsigned char* S, int keyLength) {
    int i, j = 0;
    unsigned char temp;

    for (i = 0; i < 256; i++) {
        S[i] = (unsigned char)i;
    }

    for (i = 0; i < 256; i++) {
        j = (j + S[i] + key[i % keyLength]) % 256;
        temp = S[i];
        S[i] = S[j];
        S[j] = temp;
    }
}

void RC4EncryptDecrypt(unsigned char* data, int dataLength, unsigned char* S) {
    int i = 0, j = 0, k;
    unsigned char temp;

    for (k = 0; k < dataLength; k++) {
        i = (i + 1) % 256;
        j = (j + S[i]) % 256;

        temp = S[i];
        S[i] = S[j];
        S[j] = temp;

        data[k] ^= S[(S[i] + S[j]) % 256];
    }
}

void EncryptRC4(unsigned char* data, int dataLength, unsigned char* key, int keyLength) {
    unsigned char S[256];
    RC4Init(key, S, keyLength);
    RC4EncryptDecrypt(data, dataLength, S);
}

void DecryptRC4(unsigned char* data, int dataLength, unsigned char* key, int keyLength) {
    EncryptRC4(data, dataLength, key, keyLength);
}

void DeriveTransportSubkey(const unsigned char* sessionKey16, const unsigned char* salt16, unsigned char* subkeyOut16) {
	for (int i = 0; i < 16; i++)
		subkeyOut16[i] = sessionKey16[i] ^ sessionKey16[(i + 1) % 16] ^ salt16[i];
}

void EncryptSessionTransport(unsigned char* data, int dataLength, const unsigned char* sessionKey16, const unsigned char* salt16) {
	unsigned char sub[16];
	DeriveTransportSubkey(sessionKey16, salt16, sub);
	EncryptRC4(data, dataLength, (unsigned char*)sessionKey16, 16);
	EncryptRC4(data, dataLength, sub, 16);
}

void DecryptSessionTransport(unsigned char* data, int dataLength, const unsigned char* sessionKey16, const unsigned char* salt16) {
	unsigned char sub[16];
	DeriveTransportSubkey(sessionKey16, salt16, sub);
	EncryptRC4(data, dataLength, sub, 16);
	EncryptRC4(data, dataLength, (unsigned char*)sessionKey16, 16);
}