#pragma once
#include <switch.h>

namespace nexus {

    Result GetControlData(u64 tid, NsApplicationControlData *buf, u64 *actual);

}
