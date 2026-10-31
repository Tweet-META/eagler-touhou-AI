#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

namespace eagler::ai {
// Fixed logical-frame delay for one AI seat's complete button state.
class InputDelay {
public:
    static constexpr int MaxDelay=60;
    void configure(int delayFrames){delay_=std::clamp(delayFrames,0,MaxDelay);reset();}
    void reset(){queue_.fill(0);next_=0;}
    void cancelButtons(std::uint16_t mask){
        for(auto& buttons:queue_)buttons&=std::uint16_t(~mask);
    }
    std::uint16_t tick(std::uint16_t wanted){
        queue_[next_]=wanted;
        const auto output=queue_[(next_+queue_.size()-std::size_t(delay_))%queue_.size()];
        next_=(next_+1)%queue_.size();
        return output;
    }
private:
    std::array<std::uint16_t,MaxDelay+1> queue_{};
    std::size_t next_=0;
    int delay_=0;
};
} // namespace eagler::ai
