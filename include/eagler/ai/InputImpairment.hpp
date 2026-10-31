#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace eagler::ai {
// Apply to one AI seat, once per logical tick, after deciding its buttons.
// A failed press stays absent until that requested key is released.
class InputImpairment {
public:
    static constexpr int MaxDelay=60;
    void configure(int delayFrames,float failureRate){
        delay_=std::clamp(delayFrames,0,MaxDelay);
        failure_=std::isfinite(failureRate)?std::clamp(failureRate,0.f,1.f):0;
        reset();failed_=0;
    }
    void reset(){queue_.fill(0);next_=0;previous_=blocked_=0;}
    void startRun(){reset();random_=0x9e3779b9u;failed_=0;}
    void cancelButtons(std::uint16_t mask){
        for(auto& buttons:queue_)buttons&=std::uint16_t(~mask);
    }
    std::uint16_t tick(std::uint16_t wanted){
        const auto pressed=std::uint16_t(wanted&~previous_);
        blocked_&=wanted;
        for(unsigned bit=0;bit<16;++bit){
            const auto key=std::uint16_t(1u<<bit);
            if((pressed&key)&&fails()){blocked_|=key;++failed_;}
        }
        previous_=wanted;
        queue_[next_]=std::uint16_t(wanted&~blocked_);
        const auto output=queue_[(next_+queue_.size()-std::size_t(delay_))%queue_.size()];
        next_=(next_+1)%queue_.size();
        return output;
    }
    std::uint32_t failedPresses() const {return failed_;}
private:
    bool fails(){
        if(failure_<=0)return false;
        if(failure_>=1)return true;
        random_^=random_<<13;random_^=random_>>17;random_^=random_<<5;
        return float(random_>>8)*(1.f/16777216.f)<failure_;
    }
    std::array<std::uint16_t,MaxDelay+1> queue_{};
    std::size_t next_=0;
    int delay_=0;
    float failure_=0;
    std::uint16_t previous_=0,blocked_=0;
    std::uint32_t random_=0x9e3779b9u,failed_=0;
};
} // namespace eagler::ai
