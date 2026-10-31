#pragma once
#include <algorithm>
#include <cmath>

namespace eagler::ai {
// Tick exactly once per eligible logical frame; never detects a deathbomb.
// The title owns resource/protection checks, cancellation and button mapping.
class BombDecision {
public:
    void configure(float threshold,int confirmationFrames,int delayFrames){
        threshold_=std::isfinite(threshold)?std::clamp(threshold,0.f,1.f):.6f;
        confirmation_=std::clamp(confirmationFrames,0,60);
        delay_=std::clamp(delayFrames,0,60);reset();
    }
    void reset(){highFrames_=remaining_=0;pending_=false;}
    bool tick(float pressure,bool reliable){
        if(pending_){
            if(remaining_>0)--remaining_;
            if(remaining_==0){reset();return true;}
            return false;
        }
        if(!reliable||!std::isfinite(pressure)||pressure<threshold_){highFrames_=0;return false;}
        if(++highFrames_<std::max(1,confirmation_))return false;
        highFrames_=0;remaining_=delay_;pending_=true;
        if(remaining_==0){reset();return true;}
        return false;
    }
    bool pending() const {return pending_;}
    int remainingFrames() const {return remaining_;}
    int highFrames() const {return highFrames_;}
private:
    float threshold_=.6f;
    int confirmation_=6,delay_=12,highFrames_=0,remaining_=0;
    bool pending_=false;
};
} // namespace eagler::ai
