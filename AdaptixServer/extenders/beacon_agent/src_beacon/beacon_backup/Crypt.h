#pragma once

void RC4Init(unsigned char* key, unsigned char* S, int keyLength);

void RC4EncryptDecrypt(unsigned char* data, int dataLength, unsigned char* S);

void EncryptRC4(unsigned char* data, int dataLength, unsigned char* key, int keyLength);

void DecryptRC4(unsigned char* data, int dataLength, unsigned char* key, int keyLength);

/* Double-RC4 session transport: RC4(sk) then RC4(derive(sk,salt)). In-place, 16-byte keys. */
void DeriveTransportSubkey(const unsigned char* sessionKey16, const unsigned char* salt16, unsigned char* subkeyOut16);
void EncryptSessionTransport(unsigned char* data, int dataLength, const unsigned char* sessionKey16, const unsigned char* salt16);
void DecryptSessionTransport(unsigned char* data, int dataLength, const unsigned char* sessionKey16, const unsigned char* salt16);