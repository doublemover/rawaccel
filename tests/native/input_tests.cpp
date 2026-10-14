#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>
#ifndef _MSC_VER
#define __forceinline inline
#define _copysign copysign
#endif
#include "../../driver/input-pipeline.hpp"
namespace ra = rawaccel;
static int checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { std::cerr << __LINE__ << ": " #c "\n"; std::exit(1); } } while (false)
struct Packet {
    std::uint16_t Flags = 0, ButtonFlags = 0, ButtonData = 0;
    std::int32_t LastX = 0, LastY = 0;
    std::uint32_t ExtraInformation = 0;
};
struct Device {
    ra::input_configuration config;
    ra::input_state state;
    std::uint64_t configuration_generation = 0, input_epoch = 0;
    bool callback_busy = false;
};
static ra::prepared_profile profile(double scale) {
    ra::modifier_settings settings;
    settings.prof.output_dpi = scale * ra::NORMALIZED_DPI;
    ra::prepared_profile result;
    result.init(settings);
    return result;
}
static Packet motion(int x, int y = 0, unsigned flags = 0) {
    Packet p; p.LastX = x; p.LastY = y; p.ButtonFlags = static_cast<std::uint16_t>(flags); return p;
}
static void button_tests() {
    auto base = profile(2), left = profile(3), right = profile(4), middle = profile(5), x1 = profile(6), x2 = profile(7);
    ra::input_configuration cfg; cfg.enabled = true; cfg.base_profile = &base; cfg.mouse1_only = true;
    ra::input_state state;
    auto p = motion(10); ra::transform_packet(p,cfg,state,1); CHECK(p.LastX == 10);
    p=motion(10,0,1); ra::transform_packet(p,cfg,state,1); CHECK(p.LastX == 20); CHECK(state.buttons == 1);
    p=motion(10,0,2); ra::transform_packet(p,cfg,state,1); CHECK(p.LastX == 10); CHECK(state.buttons == 0);
    const ra::prepared_profile* bindings[] = {&left,&right,&middle,&x1,&x2};
    for (int i=0; i<5; ++i) cfg.button_profiles[i]=bindings[i];
    for (int i=0; i<5; ++i) {
        p=motion(10,0,1u << (2*i)); ra::transform_packet(p,cfg,state,1); CHECK(p.LastX == (i+3)*10);
    }
    CHECK(state.buttons == 31);
    for (int i=4; i>=0; --i) {
        p=motion(10,0,2u << (2*i)); ra::transform_packet(p,cfg,state,1); CHECK(p.LastX == (i ? (i+2)*10 : 10));
    }
    p=motion(10,0,3); ra::transform_packet(p,cfg,state,1); CHECK(state.buttons == 0); CHECK(p.LastX==10);
    p=motion(10,0,4); p.Flags=1; ra::transform_packet(p,cfg,state,1); CHECK(p.LastX==10); CHECK(state.buttons==2);
    p=motion(10); ra::transform_packet(p,cfg,state,1); CHECK(p.LastX==40);
    p=motion(99,0,8); p.Flags=4; ra::transform_packet(p,cfg,state,1); CHECK(state.buttons==2); CHECK(p.LastX==99);
    cfg.enabled=false;
    p=motion(10,0,8); ra::transform_packet(p,cfg,state,1); CHECK(state.buttons==0); CHECK(p.LastX==10);
    cfg.enabled=true; cfg.mouse1_only=false; cfg.set_extra_info=true;
    p=motion(-2,3); ra::transform_packet(p,cfg,state,1); CHECK(p.LastX==-4); CHECK(p.ExtraInformation==0x0003fffe);
}
static void carry_tests() {
    auto base = profile(1.5), right = profile(2.5);
    ra::input_configuration cfg; cfg.enabled=true; cfg.base_profile=&base; cfg.button_profiles[1]=&right;
    ra::input_state state;
    auto p=motion(1); ra::transform_packet(p,cfg,state,1); CHECK(p.LastX==1); CHECK(state.carry.x==.5);
    p=motion(1,0,4); ra::transform_packet(p,cfg,state,1); CHECK(p.LastX==2); CHECK(state.carry.x==.5);
    p=motion(1,0,8); ra::transform_packet(p,cfg,state,1); CHECK(p.LastX==1);
    auto huge=profile(1e300); cfg.base_profile=&huge;
    p=motion(100); ra::transform_packet(p,cfg,state,1); CHECK(p.LastX==100); CHECK(state.carry.x==0);
    for (double value : {std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN(),2147483648.0,-2147483649.0}) CHECK(!ra::representable_motion(value));
    CHECK(ra::representable_motion(2147483647)); CHECK(ra::representable_motion(-2147483648.0));
}
static void delivery_tests() {
    auto base=profile(1.5);
    Device d; d.config.enabled=true; d.config.base_profile=&base; d.config.keep_time=false;
    bool locked=false; int clock_calls=0, sends=0;
    auto lock=[&] { CHECK(!locked); locked=true; };
    auto unlock=[&] { CHECK(locked); locked=false; };
    auto clock=[&] { ++clock_calls; return 1000ll; };
    std::vector<Packet> input(70,motion(1));
    unsigned consumed=0;
    std::vector<int> output;
    auto accept=[&](Packet* first,Packet* last,unsigned* count) {
        CHECK(!locked); ++sends; *count=static_cast<unsigned>(last-first);
        for (;first!=last;++first) output.push_back(first->LastX);
    };
    ra::deliver_packets(d,input.data(),input.data()+input.size(),&consumed,.001,lock,unlock,clock,accept);
    CHECK(consumed==70); CHECK(sends==3); CHECK(clock_calls==0); CHECK(!d.callback_busy); CHECK(!locked);
    for (size_t i=0;i<input.size();++i) { CHECK(input[i].LastX==1); CHECK(output[i]==(i%2 ? 2 : 1)); }
    d.state={}; output.clear(); sends=0;
    auto partial=[&](Packet* first,Packet*,unsigned* count) { CHECK(!locked); ++sends; *count=3; for(int i=0;i<3;++i) output.push_back(first[i].LastX); };
    ra::deliver_packets(d,input.data(),input.data()+input.size(),&consumed,.001,lock,unlock,clock,partial);
    CHECK(consumed==3); CHECK(sends==1); CHECK(d.state.carry.x==.5);
    ra::deliver_packets(d,input.data()+3,input.data()+input.size(),&consumed,.001,lock,unlock,clock,accept);
    CHECK(consumed==67); CHECK(output.size()==70);
    for(size_t i=0;i<output.size();++i) CHECK(output[i]==(i%2 ? 2 : 1));
    d.state={}; d.state.counter=10; d.config.keep_time=true;
    auto reject=[&](Packet*,Packet*,unsigned* count) { CHECK(!locked); *count=0; };
    ra::deliver_packets(d,input.data(),input.data()+1,&consumed,.001,lock,unlock,clock,reject);
    CHECK(consumed==0); CHECK(d.state.counter==10); CHECK(d.state.active_profile==nullptr); CHECK(d.state.carry.x==0);
    // Apply during an upstream callback cannot resurrect old profile pointers.
    auto replace=[&](Packet*,Packet*,unsigned* count) { CHECK(!locked); ++d.configuration_generation; d.config.base_profile=nullptr; d.state={}; *count=1; };
    ra::deliver_packets(d,input.data(),input.data()+70,&consumed,.001,lock,unlock,clock,replace);
    CHECK(consumed==1); CHECK(d.state.active_profile==nullptr); CHECK(!d.callback_busy);
    d.config.base_profile=&base; d.state={};
    Packet transitions[] = {motion(1,0,1),motion(1,0,2)};
    auto apply_partial=[&](Packet*,Packet*,unsigned* count) {
        CHECK(d.state.buttons==0); // Unaccepted transitions were never published.
        ++d.configuration_generation; ra::reset_motion(d.state); *count=1;
    };
    ra::deliver_packets(d,transitions,transitions+2,&consumed,.001,lock,unlock,clock,apply_partial);
    CHECK(consumed==1); CHECK(d.state.buttons==1); CHECK(d.state.active_profile==nullptr);
    auto suspend_partial=[&](Packet*,Packet*,unsigned* count) {
        ++d.configuration_generation; ++d.input_epoch; d.state={}; *count=1;
    };
    ra::deliver_packets(d,transitions,transitions+2,&consumed,.001,lock,unlock,clock,suspend_partial);
    CHECK(consumed==1); CHECK(d.state.buttons==0);
    d.config.base_profile=&base;
    auto reenter=[&](Packet* first,Packet* last,unsigned* count) {
        unsigned nested=99;
        ra::deliver_packets(d,input.data(),input.data()+1,&nested,.001,lock,unlock,clock,accept);
        CHECK(nested==0); *count=static_cast<unsigned>(last-first);
    };
    ra::deliver_packets(d,input.data(),input.data()+1,&consumed,.001,lock,unlock,clock,reenter);
    CHECK(consumed==1); CHECK(!d.callback_busy);
}
int main() { button_tests(); carry_tests(); delivery_tests(); std::cout<<checks<<" input assertions passed\n"; }
