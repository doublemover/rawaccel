// Portable tests for the same math and validation headers used by the driver.
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>
#ifndef _MSC_VER
#define __forceinline inline
#define _copysign copysign
#endif
#include "../../common/rawaccel-validate.hpp"
#include "reference_smoothers.hpp"
namespace ra = rawaccel;

static int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::cerr << __FILE__ << ':' << __LINE__ << ": " #condition "\n"; std::exit(1); } } while (false)
static bool close(double a, double b, double tol = 1e-11) {
    return !ra::infnan(a) && !ra::infnan(b) && std::fabs(a - b) <= tol * (1 + std::fabs(b));
}

static void validation_tests() {
    CHECK(bool(ra::valid(ra::profile{})));
    CHECK(bool(ra::valid(ra::device_config{})));
    CHECK(bool(ra::valid(ra::device_settings{})));
    constexpr double bad[] = {std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    const auto accel_fields = {&ra::accel_args::input_offset, &ra::accel_args::output_offset,
        &ra::accel_args::acceleration, &ra::accel_args::decay_rate, &ra::accel_args::gamma,
        &ra::accel_args::motivity, &ra::accel_args::exponent_classic, &ra::accel_args::scale,
        &ra::accel_args::exponent_power, &ra::accel_args::limit, &ra::accel_args::sync_speed,
        &ra::accel_args::smooth};
    const auto profile_fields = {&ra::profile::output_dpi, &ra::profile::yx_output_dpi_ratio,
        &ra::profile::lr_output_dpi_ratio, &ra::profile::ud_output_dpi_ratio,
        &ra::profile::degrees_rotation, &ra::profile::degrees_snap,
        &ra::profile::speed_min, &ra::profile::speed_max};
    const auto speed_fields = {&ra::speed_args::lp_norm, &ra::speed_args::input_speed_smooth_halflife,
        &ra::speed_args::scale_smooth_halflife, &ra::speed_args::output_speed_smooth_halflife};
    for (double value : bad) {
        for (auto field : accel_fields) {
            ra::profile p; p.accel_x.*field = value; CHECK(!ra::valid(p));
            p = {}; p.accel_y.*field = value; CHECK(!ra::valid(p));
        }
        for (auto field : profile_fields) { ra::profile p; p.*field = value; CHECK(!ra::valid(p)); }
        for (auto field : speed_fields) { ra::profile p; p.speed_processor_args.*field = value; CHECK(!ra::valid(p)); }
        for (int i = 0; i != 6; ++i) {
            ra::profile p;
            double* fields[] = {&p.domain_weights.x, &p.domain_weights.y, &p.range_weights.x,
                &p.range_weights.y, &p.accel_x.cap.x, &p.accel_x.cap.y};
            *fields[i] = value; CHECK(!ra::valid(p));
        }
        ra::device_config d; d.clamp.min = value; CHECK(!ra::valid(d));
        d = {}; d.clamp.max = value; CHECK(!ra::valid(d));
    }
    for (int mode : {-1, 7, 999}) { ra::profile p; p.accel_x.mode = static_cast<ra::accel_mode>(mode); CHECK(!ra::valid(p)); }
    for (int mode : {-1, 3, 999}) { ra::profile p; p.accel_x.cap_mode = static_cast<ra::cap_mode>(mode); CHECK(!ra::valid(p)); }
    ra::profile p;
    for (auto& c : p.name) { c = L'x'; }
    CHECK(!ra::valid(p));
    p.name[ra::MAX_NAME_LEN - 1] = 0; CHECK(bool(ra::valid(p)));
    p = {}; p.speed_min = -1; CHECK(!ra::valid(p));
    for (auto field : speed_fields) { p = {}; p.speed_processor_args.*field = -1; CHECK(!ra::valid(p)); }
    for (size_t i = 0; i < ra::MOUSE_BUTTON_COUNT; ++i) {
        ra::device_config d; for (auto& c : d.button_profiles[i]) c = L'x'; CHECK(!ra::valid(d));
        d.button_profiles[i][ra::MAX_NAME_LEN - 1] = 0; CHECK(bool(ra::valid(d)));
    }
    ra::device_settings d; for (auto& c : d.id) c = L'x'; CHECK(!ra::valid(d));
    d = {}; for (auto& c : d.profile) c = L'x'; CHECK(!ra::valid(d));
    d = {}; for (auto& c : d.name) c = L'x'; CHECK(!ra::valid(d));
    p = {}; p.accel_x.mode = ra::accel_mode::classic; p.accel_x.cap_mode = ra::cap_mode::io;
    p.accel_x.input_offset = p.accel_x.cap.x = 5; CHECK(!ra::valid(p));
    p.accel_x.cap.x = 5.1; CHECK(bool(ra::valid(p)));
}

static void lookup_tests() {
    ra::profile p; auto& a = p.accel_x; a.mode = ra::accel_mode::lookup;
    a.length = 4; a.data[0] = 1; a.data[1] = 2; a.data[2] = 3; a.data[3] = 6;
    CHECK(bool(ra::valid(p)));
    ra::lookup curve(a);
    for (double x : {.01, 1., 2., 3., 100.}) CHECK(close(curve(x, a), 2));
    a.gain = false; curve = ra::lookup(a);
    CHECK(close(curve(.5, a), 2)); CHECK(close(curve(2, a), 4)); CHECK(close(curve(4, a), 8));
    for (int n : {-1, 0, 2, 3, 5, 515, 1000000000}) { a.length = n; CHECK(!ra::valid(p)); }
    a.length = 4; a.data[2] = 1; CHECK(!ra::valid(p));
    a.data[2] = .5; CHECK(!ra::valid(p));
    a.data[2] = 3; a.data[0] = 0; CHECK(!ra::valid(p));
    a.data[0] = 1;
    for (int i = 0; i < 4; ++i) {
        float old = a.data[i]; a.data[i] = std::numeric_limits<float>::quiet_NaN(); CHECK(!ra::valid(p)); a.data[i] = old;
    }
    a.length = ra::LUT_RAW_DATA_CAPACITY;
    for (int i = 0; i < a.length; i += 2) { a.data[i] = 1 + i / 2; a.data[i + 1] = 2 + i; }
    CHECK(bool(ra::valid(p))); curve = ra::lookup(a);
    CHECK(close(curve(257, a), 514)); CHECK(close(curve(1000, a), 2000));
}

static double old_jump(double x, double step, double height, double rate) {
    auto primitive = [&](double v) { return height * (v + std::log(1 + std::exp(rate * (step - v))) / rate); };
    return 1 + (primitive(x) - primitive(0)) / x;
}
static void curve_tests() {
    ra::accel_args a; a.mode = ra::accel_mode::jump; a.smooth = .001; a.cap = {1000, 2};
    ra::jump<true> j(a);
    CHECK(close(j(100, a), 1)); CHECK(close(j(2000, a), 1.5));
    CHECK(!ra::infnan(j(1000, a))); CHECK(j(1000, a) > 1);
    a.cap = {15, 1.5}; a.smooth = .5; j = ra::jump<true>(a);
    for (double x : {.125, 1., 5., 15., 25., 100.}) CHECK(close(j(x, a), old_jump(x, 15, .5, 2 * M_PI / 7.5)));
    a.smooth = 0; j = ra::jump<true>(a); CHECK(!ra::infnan(j.C)); CHECK(close(j(30, a), 1.25));
    a = {}; a.mode = ra::accel_mode::power; a.cap_mode = ra::cap_mode::in; a.output_offset = 2; a.cap.x = .00001;
    ra::power<true> power(a); CHECK(close(power(0, a), 2)); CHECK(close(power(1, a), 2));
    a = {}; a.mode = ra::accel_mode::synchronous;
    ra::activation_framework<true> sync(a);
    CHECK(close(sync(1e15, a), 1.5, 1e-6));
    CHECK(!ra::infnan(sync(0, a)));
    for (double x : {.001, .125, .25, 1., 5., 100., 512., 1024., 1e10}) {
        double y = sync(x, a); CHECK(!ra::infnan(y)); CHECK(y >= 1 / a.motivity - 1e-6); CHECK(y <= a.motivity + 1e-6);
    }
}

static void smoothing_cache_tests() {
    for (double half_life : {0., .1, 5., 50., 1e10}) {
        ra::simple_ema_smoother simple;
        reference::simple_ema_smoother simple_reference;
        ra::linear_ema_smoother linear;
        reference::linear_ema_smoother linear_reference;
        for (int reset = 0; reset < 2; ++reset) {
            simple.init(half_life + reset); simple_reference.init(half_life + reset);
            linear.init(half_life + reset, 1.25); linear_reference.init(half_life + reset, 1.25);
            for (int i = 0; i < 2000; ++i) {
                double speed = (i * 17) % 101;
                double time = i < 1000 ? .125 : .125 + (i % 7) * .001;
                CHECK(simple.smooth(speed, time) == simple_reference.smooth(speed, time));
                CHECK(linear.smooth(speed, time) == linear_reference.smooth(speed, time));
            }
        }
    }
}

static void modifier_tests() {
    ra::modifier_settings s; s.prof.speed_processor_args.whole = false;
    s.prof.speed_processor_args.output_speed_smooth_halflife = 5;
    ra::init_data(s); ra::modifier m(s); ra::speed_processor sp; sp.init(s.prof.speed_processor_args);
    vec2d v{-10, 0}; m.modify(v, sp, s, 1, 1); CHECK(v.x < 0); CHECK(v.y == 0);
    v = {0, 10}; m.modify(v, sp, s, 1, 1); CHECK(v.x == 0); CHECK(v.y > 0);
    v = {10, 0}; m.modify(v, sp, s, 1, 1); CHECK(v.x > 0); CHECK(v.y == 0);
    s = {}; s.prof.speed_max = 10; s.prof.speed_min = 1; ra::init_data(s); m = ra::modifier(s); sp.init(s.prof.speed_processor_args);
    v = {}; m.modify(v, sp, s, 1, 1); CHECK(v.x == 0 && v.y == 0);
    for (int mode = 0; mode <= 6; ++mode) for (bool gain : {false, true}) for (bool whole : {false, true}) {
        s = {}; s.prof.speed_processor_args.whole = whole;
        s.prof.accel_x.mode = static_cast<ra::accel_mode>(mode); s.prof.accel_x.gain = gain;
        if (mode == 5) { auto& a = s.prof.accel_x; a.length = 4; a.data[0] = 1; a.data[1] = 1; a.data[2] = 100; a.data[3] = 100; }
        s.prof.accel_y = s.prof.accel_x;
        CHECK(bool(ra::valid(s.prof))); ra::init_data(s); m = ra::modifier(s); sp.init(s.prof.speed_processor_args);
        for (double time : {.0625, .125, 1., 8., 100.}) for (double x : {0., 1., -1., 100., 2147483647.}) {
            v = {x, 1}; m.modify(v, sp, s, 1, time); CHECK(!ra::infnan(v.x)); CHECK(!ra::infnan(v.y));
        }
    }
}

int main() {
    static_assert(std::is_standard_layout<ra::device_config>::value, "wire layout");
    static_assert(std::is_trivially_copyable<ra::modifier_settings>::value, "wire copy");
    validation_tests(); lookup_tests(); curve_tests(); modifier_tests(); smoothing_cache_tests();
    std::cout << checks << " math/validation checks passed\n";
    std::cout << "ABI sizeof(wchar_t)=" << sizeof(wchar_t) << " device_config=" << sizeof(ra::device_config)
        << " device_settings=" << sizeof(ra::device_settings) << " io_base=" << sizeof(ra::io_base)
        << " modifier_settings=" << sizeof(ra::modifier_settings) << '\n';
}
