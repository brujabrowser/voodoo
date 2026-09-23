// Bindings is the base:: rung. Chromium //base/task/post_job.h posts
// ThreadPool jobs; here the job rung is WASMv8Bindings
// cppgc::Platform::PostJob (calling-thread DefaultJobHandle — wasm32-wasip1
// has no worker pool). WASMJobHandler is a separate playbook/state-machine
// queue, not this C++ PostJob.
#ifndef BASE_TASK_POST_JOB_H_
#define BASE_TASK_POST_JOB_H_

#include "cppgc/default-platform.h"
#include "cppgc/platform.h"

#include <memory>
#include <utility>

namespace base {

using JobHandle = cppgc::JobHandle;
using JobDelegate = cppgc::JobDelegate;
using JobTask = cppgc::JobTask;
using TaskPriority = cppgc::TaskPriority;

inline cppgc::DefaultPlatform& JobPlatform() {
  static cppgc::DefaultPlatform platform;
  return platform;
}

inline std::unique_ptr<JobHandle> PostJob(
    TaskPriority priority, std::unique_ptr<JobTask> job_task) {
  return JobPlatform().PostJob(priority, std::move(job_task));
}

}  // namespace base

#endif  // BASE_TASK_POST_JOB_H_
