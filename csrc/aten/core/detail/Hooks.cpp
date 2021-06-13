#include <ATen/Config.h>
#include <ATen/Context.h>
#include <c10/util/Exception.h>

#include <runtime/DPCPPUtils.h>
#include <runtime/Exception.h>
#include <core/Device.h>
#include <core/Generator.h>
#include <core/detail/Hooks.h>
#include <core/CachingHostAllocator.h>

#include <cstddef>
#include <functional>
#include <memory>


namespace xpu {
namespace dpcpp {
namespace detail {

void XPUHooks::initXPU() const {
  // TODO:
}

bool XPUHooks::hasXPU() const {
  return true;
}

bool XPUHooks::hasOneMKL() const {
#ifdef USE_ONEMKL
  return true;
#else
  return false;
#endif
}

bool XPUHooks::hasOneDNN() const {
  return true;
}

std::string XPUHooks::showConfig() const {
  return "DPCPP backend version: 1.0";
}

int64_t XPUHooks::getCurrentDevice() const {
  c10::DeviceIndex device_index;
  AT_DPCPP_CHECK(dpcppGetDevice(&device_index));
  return device_index;
}

int XPUHooks::getDeviceCount() const {
  int count;
  AT_DPCPP_CHECK(dpcppGetDeviceCount(&count));
  return count;
}

at::Device XPUHooks::getDeviceFromPtr(void* data) const {
  c10::DeviceIndex device;
  AT_DPCPP_CHECK(dpcppGetDeviceIdFromPtr(&device, data));
  return {DeviceType::XPU, static_cast<int16_t>(device)};
}

bool XPUHooks::isPinnedPtr(void* data) const {
  return dpcpp_isAllocatedByCachingHostAllocator(data);
}

at::Allocator* XPUHooks::getPinnedMemoryAllocator() const {
  return dpcpp_getCachingHostAllocator();
}

const Generator&
XPUHooks::getDefaultXPUGenerator(DeviceIndex device_index) const {
  return xpu::dpcpp::detail::getDefaultDPCPPGenerator(device_index);
}

REGISTER_XPU_HOOKS(XPUHooks);

} // detail
} // dpcpp
} // namespace
