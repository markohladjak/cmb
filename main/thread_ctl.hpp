#ifndef THREAD_H
#define THREAD_H
#pragma once
	
#include <atomic>
#include <functional>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace rutils {

class thread_ctl;

struct thread_funct_args {
	void *args;
	std::atomic<bool> _exit_signal;

	thread_funct_args() { _exit_signal = false; args = nullptr; }
};

typedef std::function<void (thread_funct_args&)> thread_funct;

class thread_ctl
{
public:
	enum STATUS {
		UNDEFINE,
		READY,
		STARTED,
		TERMINATING,
		STOPPED
	};

	thread_ctl(thread_funct funct, bool run_on_create = true, const char* name = nullptr, void *args = nullptr
				, int stack_size = 1024, bool wait_for_start = false, int priority = 1);
	~thread_ctl();

	bool run();
	void terminate(bool wait = true);

private:
	STATUS _status;
	const char* _name;

	thread_funct _funct;

	int _stack_size;
	thread_funct_args _args;
	int _priority;

	bool _loop = false;

	SemaphoreHandle_t _mux;

	static void _task(thread_ctl *obj);

};

}

#endif
