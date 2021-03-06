/**
 * Copyright (C) 2018 MongoDB Inc.
 *
 * This program is free software: you can redistribute it and/or  modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, the copyright holders give permission to link the
 * code of portions of this program with the OpenSSL library under certain
 * conditions as described in each individual source file and distribute
 * linked combinations including the program with the OpenSSL library. You
 * must comply with the GNU Affero General Public License in all respects
 * for all of the code used other than as permitted herein. If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so. If you do not
 * wish to do so, delete this exception statement from your version. If you
 * delete this exception statement from all source files in the program,
 * then also delete it in the license file.
 */

#pragma once

#include <memory>
#include <vector>

#include "mongo/db/service_context_fwd.h"
#include "mongo/stdx/mutex.h"
#include "mongo/util/clock_source.h"
#include "mongo/util/concurrency/with_lock.h"
#include "mongo/util/periodic_runner.h"

namespace mongo {

/**
 * An implementation of the PeriodicRunner which exposes a pump function to execute jobs on the
 * calling thread.
 */
class PeriodicRunnerEmbedded : public PeriodicRunner {
public:
    PeriodicRunnerEmbedded(ServiceContext* svc, ClockSource* clockSource);
    ~PeriodicRunnerEmbedded();

    std::unique_ptr<PeriodicRunner::PeriodicJobHandle> makeJob(PeriodicJob job) override;
    void scheduleJob(PeriodicJob job) override;

    void startup() override;

    void shutdown() override;

    // Safe to call from multiple threads but will only execute on one thread at a time.
    // Returns true if it attempted to run any jobs.
    bool tryPump();

private:
    class PeriodicJobImpl {
        MONGO_DISALLOW_COPYING(PeriodicJobImpl);

    public:
        friend class PeriodicRunnerEmbedded;
        PeriodicJobImpl(PeriodicJob job, ClockSource* source, ServiceContext* svc);

        void start();
        void pause();
        void resume();
        void stop();

        bool isAlive(WithLock lk);

        Date_t nextScheduledRun() const {
            return _lastRun + _job.interval;
        }

        enum class ExecutionStatus { kNotScheduled, kRunning, kPaused, kCanceled };

    private:
        PeriodicJob _job;
        ClockSource* _clockSource;
        ServiceContext* _serviceContext;
        Date_t _lastRun{};

        // The mutex is protecting _execStatus, the variable that can be accessed from other
        // threads.
        stdx::mutex _mutex;

        // The current execution status of the job.
        ExecutionStatus _execStatus{ExecutionStatus::kNotScheduled};
    };
    struct PeriodicJobSorter;

    std::shared_ptr<PeriodicRunnerEmbedded::PeriodicJobImpl> createAndAddJob(PeriodicJob job,
                                                                             bool shouldStart);

    class PeriodicJobHandleImpl : public PeriodicJobHandle {
    public:
        explicit PeriodicJobHandleImpl(std::weak_ptr<PeriodicJobImpl> jobImpl)
            : _jobWeak(jobImpl) {}
        void start() override;
        void pause() override;
        void resume() override;

    private:
        std::weak_ptr<PeriodicJobImpl> _jobWeak;
    };


    ServiceContext* _svc;
    ClockSource* _clockSource;

    // min-heap for running jobs, next job to run in front()
    std::vector<std::shared_ptr<PeriodicJobImpl>> _jobs;
    std::vector<std::shared_ptr<PeriodicJobImpl>> _Pausedjobs;

    stdx::mutex _mutex;
    bool _running = false;
};

}  // namespace mongo
