#pragma once
#include <algorithm>
#include <cmath>

namespace eagler::ai {
// Tick exactly once per eligible logical frame; never detects a deathbomb.
// The title owns resource/protection checks, cancellation and button mapping.
class BombDecision {
public:
    void configure(float threshold,int confirmationFrames){
        threshold_=std::isfinite(threshold)?std::clamp(threshold,0.f,1.f):.6f;
        confirmation_=std::clamp(confirmationFrames,0,60);
        reset();
    }
    void reset(){highFrames_=0;}
    bool tick(float pressure,bool reliable){
        if(!reliable||!std::isfinite(pressure)||pressure<threshold_){highFrames_=0;return false;}
        if(++highFrames_<std::max(1,confirmation_))return false;
        reset();return true;
    }
    int highFrames() const {return highFrames_;}
private:
    float threshold_=.6f;
    int confirmation_=6,highFrames_=0;
};
} // namespace eagler::ai
