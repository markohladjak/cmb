#ifndef TWAI_FILTER_H
#define TWAI_FILTER_H

#include "driver/twai.h"

#pragma once


class twai_filter  
{
	private:

	public:

		twai_filter();
		~twai_filter();

	bool operator ()(twai_message_t &msg);

};
#endif