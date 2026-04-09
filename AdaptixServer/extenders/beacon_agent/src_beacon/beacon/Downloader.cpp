#include "Downloader.h"
#include "utils.h"
#include <new>

Downloader* Downloader_Create(ULONG chunk_size)
{
	Downloader* self = (Downloader*)MemAllocLocal(sizeof(Downloader));
	new (&self->downloads) Vector<DownloadData>();
	self->chunkSize = chunk_size;
	return self;
}

void Downloader_Destroy(Downloader* self)
{
	if (!self)
		return;
	self->downloads.destroy();
	self->downloads.~Vector<DownloadData>();
	MemFreeLocal((LPVOID*)&self, sizeof(Downloader));
}

BOOL Downloader_IsTasks(Downloader* self)
{
	if (self->downloads.size()) {
		for (int i = 0; i < self->downloads.size(); i++) {
			if (self->downloads[i].state == DOWNLOAD_STATE_RUNNING)
				return true;
		}
	}
	return false;
}

DownloadData Downloader_CreateDownloadData(Downloader* self, ULONG taskId, HANDLE hFile, ULONG64 size)
{
	DownloadData downloadData;
	downloadData.taskId   = taskId;
	downloadData.fileId   = GenerateRandom32();
	downloadData.hFile    = hFile;
	downloadData.fileSize = size;
	downloadData.index    = 0;
	downloadData.state    = DOWNLOAD_STATE_RUNNING;

	self->downloads.push_back(downloadData);

    return downloadData;
}

void Downloader_ProcessDownloader(Downloader* self, Packer* packer)
{
	if ( !self->downloads.size() )
		return;
	
	for (int i = 0; i < self->downloads.size(); i++) {
		BOOL close = false;
		if (self->downloads[i].state == DOWNLOAD_STATE_RUNNING) {
			LPVOID buffer = MemAllocLocal(self->chunkSize);
			ULONG readedBytes = 0;
			ApiWin->ReadFile(self->downloads[i].hFile, buffer, self->chunkSize, &readedBytes, NULL);
			if (readedBytes > 0) {
				self->downloads[i].index += readedBytes;

				Packer_Pack32(packer, self->downloads[i].taskId);
				Packer_Pack32(packer, COMMAND_DOWNLOAD);
				Packer_Pack32(packer, self->downloads[i].fileId);
				Packer_Pack8(packer, DOWNLOAD_CONTINUE);
				Packer_PackBytes(packer,  (BYTE*) buffer, readedBytes);

				if (self->downloads[i].fileSize == self->downloads[i].index)
					self->downloads[i].state = DOWNLOAD_STATE_FINISHED;
			}
			else {
				self->downloads[i].state = DOWNLOAD_STATE_CANCELED;

				Packer_Pack32(packer, self->downloads[i].taskId);
				Packer_Pack32(packer, COMMAND_DOWNLOAD_STATE);
				Packer_Pack32(packer, self->downloads[i].fileId);
				Packer_Pack8(packer, self->downloads[i].state);
			}
			if(buffer)
				MemFreeLocal(&buffer, self->chunkSize);
		}

		if ( self->downloads[i].state == DOWNLOAD_STATE_FINISHED ) {
			Packer_Pack32(packer, self->downloads[i].taskId);
			Packer_Pack32(packer, COMMAND_DOWNLOAD);
			Packer_Pack32(packer, self->downloads[i].fileId);
			Packer_Pack8(packer, DOWNLOAD_FINISH);
	    }

		if ( self->downloads[i].state == DOWNLOAD_STATE_CANCELED || self->downloads[i].state == DOWNLOAD_STATE_FINISHED ) {
			if (self->downloads[i].hFile) {
				ApiNt->NtClose(self->downloads[i].hFile);
				self->downloads[i].hFile = NULL;
			}

			self->downloads.remove(i);
			--i;
		}
	}
}