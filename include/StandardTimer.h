/*
 * $FU-Copyright$
 */

#ifndef _STANDARD_TIMER_H_
#define _STANDARD_TIMER_H_

#include "ARAMacros.h"
#include "Timer.h"
#include "Environment.h"
#include "StandardClock.h"
#include "StandardTimerProxy.h"

#include <chrono>
#include <thread>
#include <functional>
#include <condition_variable>

ARA_NAMESPACE_BEGIN
class StandardTimerProxy;

class StandardTimer : public Timer {
    public:
        StandardTimer(TimerType type, void* contextObject=nullptr);
        virtual ~StandardTimer();

        virtual void run(unsigned long timeoutInMicroSeconds);
	void sleep(unsigned long timeout);
        virtual void interrupt();

        bool equals(const Timer* otherTimer) const;
        bool equals(const std::shared_ptr<Timer> otherTimer) const;

        size_t getHashValue() const;

	void setCallback(std::weak_ptr<StandardTimerProxy> callback);

    private:
	std::weak_ptr<StandardTimerProxy> callback;
	std::condition_variable conditionVariable;
	std::mutex conditionVariableMutex;
};

ARA_NAMESPACE_END

#endif
