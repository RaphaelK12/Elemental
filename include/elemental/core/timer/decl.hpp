/*
   Copyright (c) 2009-2014, Jack Poulson
   All rights reserved.

   This file is part of Elemental and is under the BSD 2-Clause License, 
   which can be found in the LICENSE file in the root directory, or at 
   http://opensource.org/licenses/BSD-2-Clause
*/
#pragma once
#ifndef ELEM_TIMER_DECL_HPP
#define ELEM_TIMER_DECL_HPP

#include <chrono>

namespace elem {

using std::chrono::duration;
using std::chrono::duration_cast;
using std::chrono::steady_clock;

class Timer
{
public:
    Timer( const std::string& name="[blank]" );

    const std::string& Name() const;

    void Start();
    double Stop();
    double Partial() const; // time since last start
    double Total() const; // total elapsed time

    void Reset( const std::string& name="[blank]" );
private:
    bool running_ = false;
    std::string name_ = "[blank]";
    double totalTime_=0, lastPartialTime_=0;
    steady_clock::time_point lastTime_;
};

} // namespace elem

#endif // ifndef ELEM_TIMER_DECL_HPP
