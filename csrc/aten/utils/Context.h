#pragma once

#include <utils/DPCPPUtils.h>

namespace xpu {
namespace dpcpp {

void clearDeviceContext();

DPCPP::context getDeviceContext(int device_index = 0);

} // namespace dpcpp
} // namespace xpu
