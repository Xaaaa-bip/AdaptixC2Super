#pragma once

#include "std.cpp"
#include "Packer.h"

#define COMMAND_JOB 0x8437

#define JOB_TYPE_LOCAL    0x1
#define JOB_TYPE_REMOTE   0x2
#define JOB_TYPE_PROCESS  0x3
#define JOB_TYPE_SHELL    0x4
#define JOB_TYPE_ASYNCBOF 0x5

#define JOB_STATE_STARTING 0x0
#define JOB_STATE_RUNNING  0x1
#define JOB_STATE_FINISHED 0x2
#define JOB_STATE_KILLED   0x3

struct JobData {
    ULONG  jobId;
    WORD   jobType;
    WORD   jobState;
    HANDLE jobObject;
    WORD   pidObject;
    HANDLE pipeRead;
    HANDLE pipeWrite;
};

typedef struct _JobsController {
	Vector<JobData> jobs;
} JobsController;

JobsController* JobsController_Create();
void JobsController_Destroy(JobsController* self);
JobData JobsController_CreateJobData(JobsController* self, ULONG taskId, WORD Type, WORD State, HANDLE object, WORD pid, HANDLE input, HANDLE output);
void JobsController_ProcessJobs(JobsController* self, Packer* packer);
BOOL JobsController_KillJob(JobsController* self, ULONG jobId);