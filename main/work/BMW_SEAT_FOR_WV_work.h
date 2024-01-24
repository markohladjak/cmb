#ifndef BMW_SEAT_FOR_WV_WORK_H
#define BMW_SEAT_FOR_WV_WORK_H
#pragma once

#include "work.h"

class BMW_SEAT_FOR_WV_work: public work
{
protected:
    void _run() override;

public:
    // BMW_SEAT_FOR_WV_work() {};
};

#endif