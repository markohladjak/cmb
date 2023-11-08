#ifndef WORK_H
#define WORK_H
#pragma once

class work
{
    static work *current_work;

protected:
    virtual void _run() { }

public:
    static void run() { current_work->_run(); }
};




#endif