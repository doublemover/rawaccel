// Local CPU microbenchmark, not a Windows kernel latency measurement.
#include <cmath>
#include <chrono>
#include <iostream>
#define __forceinline inline
#define _copysign copysign
#include "../../common/rawaccel.hpp"
#include "reference_smoothers.hpp"
template <typename T> __attribute__((noinline)) double step(T& s, double x, double t) { return s.smooth(x,t); }
template <typename T> void run(const char* name, bool jitter) {
 T s; s.init(5,1.25); double sum=0; auto start=std::chrono::steady_clock::now();
 for(int i=0;i<2000000;i++) sum+=step(s,1+(i%99), jitter ? .125+(i%7)*.001 : .125);
 auto end=std::chrono::steady_clock::now();
 std::cout<<name<<(jitter?" jitter ":" fixed ")<<std::chrono::duration<double,std::nano>(end-start).count()/2000000<<" ns "<<sum<<'\n';
}
int main(){for(int i=0;i<5;i++){run<reference::linear_ema_smoother>("before",false);run<rawaccel::linear_ema_smoother>("cached",false);run<reference::linear_ema_smoother>("before",true);run<rawaccel::linear_ema_smoother>("cached",true);}}
