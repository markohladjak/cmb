#include "thread_ctl.hpp"  

namespace rutils {

thread_ctl::thread_ctl(thread_funct funct, bool run_on_create, char* name, void *args, int stack_size
					, bool wait_for_start, int priority)
{
	_funct = funct;

	_name = name;
	_args.args = args;
	_priority = priority;
	_stack_size = stack_size;

	_mux = xSemaphoreCreateMutex();

	_status = STATUS::READY;

	if (run_on_create)
		run();
}
	
thread_ctl::~thread_ctl()
{
	terminate(true);

	vSemaphoreDelete(_mux);
}

bool thread_ctl::run()
{
	if (xSemaphoreTake(_mux, 0) == pdFALSE)
		return false;

	if (_status != STATUS::READY) {
		xSemaphoreGive(_mux);
		return false;
	}

	_args._exit_signal = false;

	xTaskCreatePinnedToCore((TaskFunction_t)_task, _name, _stack_size, this, _priority, NULL, tskNO_AFFINITY);

	xSemaphoreGive(_mux);

	return true;
}

void thread_ctl::terminate(bool wait)
{
	_args._exit_signal = true;

	if (xSemaphoreTake(_mux, wait ? portMAX_DELAY : 0) == pdTRUE)
		xSemaphoreGive(_mux);
}

void thread_ctl::_task(thread_ctl *obj)
{
	xSemaphoreTake(obj->_mux, portMAX_DELAY);

	obj->_status = STATUS::STARTED;

	do
		obj->_funct(obj->_args);
	while (obj->_loop && !obj->_args._exit_signal);
	
	xSemaphoreGive(obj->_mux);

	obj->_status = STATUS::READY;

	vTaskDelete(NULL);
}

}