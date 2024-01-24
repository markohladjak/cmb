#include "utils.h"
#include "esp_timer.h"

utils::utils()
{

}

utils::~utils()
{

}

time_measure::time_measure()
{
    time_point = esp_timer_get_time();
}

time_measure::~time_measure()
{
    auto t = esp_timer_get_time() - time_point;
    printf(")))))))))))): %llu micro seconds\n", t);
}
