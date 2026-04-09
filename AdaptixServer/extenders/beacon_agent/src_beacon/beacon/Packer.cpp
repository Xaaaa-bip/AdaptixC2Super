#include "Packer.h"

void* Packer_operator_new(size_t sz) 
{
    void* p = MemAllocLocal(sz);
    return p;
}

void Packer_operator_delete(void* p) noexcept 
{
    MemFreeLocal(&p, sizeof(Packer));
}

Packer* Packer_Create()
{       
    Packer* self = (Packer*)MemAllocLocal(sizeof(Packer));
    self->capacity = 4096;
    self->buffer = (BYTE*) MemAllocLocal(self->capacity);
    self->size   = 0;
    self->index  = 0;
    self->owns_buffer = TRUE;
    return self;
}

Packer* Packer_CreateWithBuffer(BYTE* buffer, ULONG size)
{
    Packer* self = (Packer*)MemAllocLocal(sizeof(Packer));
    self->buffer   = buffer;
    self->size     = size;
    self->capacity = size;
    self->index    = 0;
    self->owns_buffer = TRUE;
    return self;
}

Packer* Packer_CreateView(BYTE* buffer, ULONG size)
{
    Packer* self = (Packer*)MemAllocLocal(sizeof(Packer));
    self->buffer   = buffer;
    self->size     = size;
    self->capacity = size;
    self->index    = 0;
    self->owns_buffer = FALSE;
    return self;
}

void Packer_Destroy(Packer* self)
{
    if (self) {
        if (self->owns_buffer)
            Packer_Clear(self, FALSE);
        MemFreeLocal((LPVOID*)&self, sizeof(Packer));
    }
}

VOID Packer_Set32(Packer* self, ULONG index, ULONG value)
{
    PUCHAR place = self->buffer + index;
    place[0] = (value >> 24) & 0xFF;
    place[1] = (value >> 16) & 0xFF;
    place[2] = (value >> 8 ) & 0xFF;
    place[3] = (value      ) & 0xFF;
}

VOID Packer_EnsureCapacity(Packer* self, ULONG needed)
{
    if (self->index + needed > self->capacity) {
        ULONG new_cap = self->capacity ? (self->capacity * 2) : 4096;
        if (new_cap < self->index + needed)
            new_cap = self->index + needed + 1024;

        self->buffer = (BYTE*)MemReallocLocal(self->buffer, new_cap);
        self->capacity = new_cap;
    }
}

VOID Packer_Pack64(Packer* self, ULONG64 value ) 
{
    Packer_EnsureCapacity(self, sizeof(ULONG64));

    PUCHAR place = (PUCHAR) self->buffer + self->index;
    place[0] = (value >> 56) & 0xFF;
    place[1] = (value >> 48) & 0xFF;
    place[2] = (value >> 40) & 0xFF;
    place[3] = (value >> 32) & 0xFF;
    place[4] = (value >> 24) & 0xFF;
    place[5] = (value >> 16) & 0xFF;
    place[6] = (value >> 8 ) & 0xFF;
    place[7] = (value      ) & 0xFF;

    self->size  += sizeof(ULONG64);
    self->index += sizeof(ULONG64);
}

VOID Packer_Pack32(Packer* self, ULONG value)
{
    Packer_EnsureCapacity(self, sizeof(ULONG));

    PUCHAR place = self->buffer + self->index;
    place[0] = (value >> 24) & 0xFF;
    place[1] = (value >> 16) & 0xFF;
    place[2] = (value >> 8 ) & 0xFF;
    place[3] = (value      ) & 0xFF;

    self->size  += sizeof(ULONG);
    self->index += sizeof(ULONG);
}

VOID Packer_Pack16(Packer* self, WORD value)
{
    Packer_EnsureCapacity(self, sizeof(WORD));

    PUCHAR place = self->buffer + self->index;
    place[0] = (value >> 8) & 0xFF;
    place[1] = (value     ) & 0xFF;

    self->size  += sizeof(WORD);
    self->index += sizeof(WORD);
}

VOID Packer_Pack8(Packer* self, BYTE value)
{
    Packer_EnsureCapacity(self, sizeof(BYTE));

    (self->buffer + self->index)[0] = value;

    self->size  += 1;
    self->index += 1;
}

VOID Packer_PackBytes(Packer* self, PBYTE data, ULONG data_size)
{
    Packer_Pack32(self, data_size);

    if (data_size) {
        Packer_EnsureCapacity(self, data_size);
        memcpy(self->buffer + self->index, data, data_size);
        self->index += data_size;
        self->size = self->index;
    }
}

VOID Packer_PackStringA(Packer* self, LPSTR str)
{
    ULONG length = StrLenA(str); // +1;
    Packer_PackBytes(self, (BYTE*) str, length);
}

VOID Packer_PackFlatBytes(Packer* self, PBYTE data, ULONG data_size)
{
    if (data_size) {
        Packer_EnsureCapacity(self, data_size);
        memcpy(self->buffer + self->index, data, data_size);
        self->index += data_size;
        self->size = self->index;
    }
}


PBYTE Packer_data(Packer* self)
{
    return self->buffer;
}

ULONG Packer_datasize(Packer* self)
{
    return self->index;
}

VOID Packer_Clear(Packer* self, BOOL renew)
{
    if (!renew) {
        if (self->buffer && self->owns_buffer)
            MemFreeLocal((LPVOID*)&self->buffer, self->capacity);
        self->buffer   = NULL;
        self->capacity = 0;
        self->size     = 0;
        self->index    = 0;
        return;
    }
    else {
        if (self->buffer == NULL) {
            self->capacity = 4096;
            self->buffer = (BYTE*)MemAllocLocal(self->capacity);
        } 
        else if (self->capacity > 0x100000) { // 1MB
            MemFreeLocal((LPVOID*)&self->buffer, self->capacity);
            self->capacity = 4096;
            self->buffer = (BYTE*)MemAllocLocal(self->capacity);
        }
        else {
            memset(self->buffer, 0, self->capacity);
        }

        self->index = 0;
        self->size = 0;
    }
}

BYTE Packer_Unpack8(Packer* self)
{
    ULONG value = 0;
    if (self->size - self->index < 1)
        return 0;

    memcpy(&value, self->buffer + self->index, 1);

    self->index += 1;

    return value;
}

ULONG Packer_Unpack32(Packer* self)
{
    ULONG value = 0;
    if ( self->size - self->index < 4 )
        return 0;

    memcpy(&value, self->buffer + self->index, 4);

    self->index += 4;

    return value;
}

BYTE* Packer_UnpackBytes(Packer* self, ULONG* str_size)
{
    *str_size = Packer_Unpack32(self);

    if ( self->size - self->index < *str_size )
        return NULL;

    if (*str_size == 0)
        return NULL;

    BYTE* out = self->buffer + self->index;
    self->index += *str_size;

    return out;
}

BYTE* Packer_UnpackBytesCopy(Packer* self, ULONG* str_size)
{
    *str_size = Packer_Unpack32(self);

    if (self->size - self->index < *str_size)
        return NULL;

    if (*str_size == 0)
        return NULL;

    BYTE* out = (PBYTE) MemAllocLocal(*str_size);
    memcpy(out, self->buffer + self->index, *str_size);

    self->index += *str_size;

    return out;
}

BOOL Packer_UnpackFlatBytes(Packer* self, PBYTE dest, ULONG len)
{
    if (self->size - self->index < len)
        return FALSE;

    memcpy(dest, self->buffer + self->index, len);
    self->index += len;

    return TRUE;
}
