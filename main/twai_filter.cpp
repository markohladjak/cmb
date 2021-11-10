#include "twai_filter.hpp"  
	
twai_filter::twai_filter()
{
	
}
	
twai_filter::~twai_filter()
{
	
}

bool twai_filter::operator ()(twai_message_t &msg)
{
    return true;
}
