#pragma once
#include <ul/loader/loader_TargetTypes.hpp>

namespace ul::loader {

    NX_NORETURN void Target(const TargetInput &target_ipt, const u64 applet_heap_size, const u64 applet_heap_reservation_size);

}
