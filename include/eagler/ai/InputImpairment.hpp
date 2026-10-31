#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace eagler::ai {
// Apply to one AI seat, once per logical tick, after deciding its buttons.
// Failed presses are retried after noticing the failure, while still wanted.
class InputImpairment {
public:
    static constexpr int MaxDelay=60;
    static constexpr int RetryFrames=6; // 0.1 s reaction after expected delivery.
    void configure(int delayFrames,float failureRate){
        delay_=std::clamp(delayFrames,0,MaxDelay);
        failure_=std::isfinite(failureRate)?std::clamp(failureRate,0.f,1.f):0;
        reset();failed_=0;
    }
    void reset(){queue_.fill(0);retryAfter_.fill(0);next_=0;previous_=blocked_=pendingPulses_=0;}
    void startRun(){reset();random_=0x9e3779b9u;failed_=0;}
    void cancelButtons(std::uint16_t mask){
        for(auto& buttons:queue_)buttons&=std::uint16_t(~mask);
        blocked_&=std::uint16_t(~mask);pendingPulses_&=std::uint16_t(~mask);
        for(unsigned bit=0;bit<16;++bit)if(mask&(1u<<bit))retryAfter_[bit]=0;
    }
    std::uint16_t tick(std::uint16_t wanted,std::uint16_t retryPulseMask=0){
        const auto pressed=std::uint16_t(wanted&~previous_);
        pendingPulses_&=retryPulseMask;
        const auto requested=std::uint16_t(wanted|pendingPulses_);
        blocked_&=requested;
        std::uint16_t pulses=0;
        for(unsigned bit=0;bit<16;++bit){
            const auto key=std::uint16_t(1u<<bit);
            if(!(requested&key)){retryAfter_[bit]=0;continue;}
            if(blocked_&key){
                if(retryAfter_[bit]>0)--retryAfter_[bit];
                if(retryAfter_[bit]>0)continue;
            }else if(!(pressed&key))continue;
            if(fails()){
                blocked_|=key;++failed_;retryAfter_[bit]=RetryFrames+delay_;
                if(retryPulseMask&key)pendingPulses_|=key;
            }else{
                blocked_&=std::uint16_t(~key);retryAfter_[bit]=0;
                if(pendingPulses_&key){pulses|=key;pendingPulses_&=std::uint16_t(~key);}
            }
        }
        previous_=wanted;
        queue_[next_]=std::uint16_t((wanted&~blocked_)|pulses);
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
    std::array<int,16> retryAfter_{};
    std::size_t next_=0;
    int delay_=0;
    float failure_=0;
    std::uint16_t previous_=0,blocked_=0,pendingPulses_=0;
    std::uint32_t random_=0x9e3779b9u,failed_=0;
};
} // namespace eagler::ai
