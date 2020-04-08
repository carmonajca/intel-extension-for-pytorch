#include "torch_ipex/csrc/cpu/DevOPs.h"

#include <ATen/Context.h>
#include <ATen/CPUGenerator.h>
#include <ATen/InferSize.h>
#include <c10/util/Exception.h>
#include <c10/util/Logging.h>

#include <limits>

#include "torch_ipex/csrc/aten_ipex_bridge.h"
#include "torch_ipex/csrc/ipex_tensor_impl.h"
#include "torch_ipex/csrc/utils.h"
#include "dbl/Common.h"
#include "dbl/Conv.h"
#include "dbl/Pool.h"
#include "ShadeDataContext.h"

#include "dil/dil.hpp"

namespace torch_ipex {
namespace cpu {

#define DBG
#if defined(DBG)
#define DEBUG(fmt) printf(fmt);
#else
#define DEBUG(fmt)
#endif

#define CHECK_DNNL_OP_PRE_COND(tensor)                                    \
  TORCH_INTERNAL_ASSERT(tensor.defined());                                \
  TORCH_INTERNAL_ASSERT(tensor.is_contiguous());                          \
  TORCH_INTERNAL_ASSERT(tensor.layout() == c10::kStrided)

at::Tensor AtenIpexCPUDev::dil_convolution(
    const at::Tensor & input,
    const at::Tensor & weight,
    const at::Tensor & bias,
    at::IntArrayRef stride,
    at::IntArrayRef padding,
    at::IntArrayRef dilation,
    int64_t groups) {
  DEBUG("AtenIpexCPUDev::dil_convolution\n");
  dil::tensor dil_input;
  dil::tensor dil_weight;
  c10::optional<dil::tensor> dil_bias{c10::nullopt};

  CHECK_DNNL_OP_PRE_COND(input);
  CHECK_DNNL_OP_PRE_COND(weight);
  dil_input = dbl::comm::try_gen_dil_tensor(input);
  dil_weight = dbl::comm::try_gen_dil_tensor(weight);
  if (bias.defined()) {
    CHECK_DNNL_OP_PRE_COND(bias);
    dil_bias = dbl::comm::try_gen_dil_tensor(bias);
  }

  dil::tensor dil_output = dbl::conv::conv2d_impl(
    dil_input,
    dil_weight,
    dil_bias,
    padding,
    stride,
    dilation,
    groups);

  return dbl::comm::gen_aten_tensor_by(dil_output);
}

at::Tensor AtenIpexCPUDev::convolution_overrideable(const at::Tensor & input, const at::Tensor & weight, const at::Tensor & bias, at::IntArrayRef stride, at::IntArrayRef padding, at::IntArrayRef dilation, bool transposed, at::IntArrayRef output_padding, int64_t groups) {
  DEBUG("AtenIpexCPUDev::convolution_overrideable\n");
  // NOTE: DO NOT always call contiguous. It may break lazy-reorder. Because contiguous will call reorder instantly.
  if (check_auto_dnnl()) {
    return dil_convolution(
      input.is_contiguous() ? input : input.contiguous(),
      weight.is_contiguous() ? weight : weight.contiguous(),
      bias.defined() ? (bias.is_contiguous() ? bias :bias.contiguous()) : bias,
      stride,
      padding,
      dilation,
      groups);
  } else {
    return mkldnn_convolution(input, weight, bias, padding, stride, dilation, groups);
  }
}

at::Tensor AtenIpexCPUDev::mkldnn_convolution(const at::Tensor & self, const at::Tensor & weight, const at::Tensor & bias, at::IntArrayRef padding, at::IntArrayRef stride, at::IntArrayRef dilation, int64_t groups) {
  DEBUG("AtenIpexCPUDev::mkldnn_convolution\n");
  TORCH_INTERNAL_ASSERT(self.defined());
  TORCH_INTERNAL_ASSERT(weight.defined());
  TORCH_INTERNAL_ASSERT(self.layout() == c10::kStrided);
  TORCH_INTERNAL_ASSERT(weight.layout() == c10::kStrided);
  TORCH_INTERNAL_ASSERT(!(bias.defined()) || (bias.defined() && bias.layout() == c10::kStrided));
  auto&& _ipex_self = bridge::shallowFallbackToCPUTensor(self);
  auto&& _ipex_weight = bridge::shallowFallbackToCPUTensor(weight);
  auto&& _ipex_bias = bridge::shallowFallbackToCPUTensor(bias);
  auto&& _ipex_result = at::mkldnn_convolution(_ipex_self.contiguous(), _ipex_weight.contiguous(), _ipex_bias.contiguous(), padding, stride, dilation, groups);
  static_cast<void>(_ipex_result); // Avoid warnings in case not used
  TORCH_INTERNAL_ASSERT(_ipex_result.is_contiguous());
  TORCH_INTERNAL_ASSERT(_ipex_result.layout() == c10::kStrided);
  return bridge::shallowUpgradeToDPCPPTensor(_ipex_result);
}

std::tuple<at::Tensor,at::Tensor,at::Tensor> AtenIpexCPUDev::convolution_backward_overrideable(const at::Tensor & grad_output, const at::Tensor & input, const at::Tensor & weight, at::IntArrayRef stride, at::IntArrayRef padding, at::IntArrayRef dilation, bool transposed, at::IntArrayRef output_padding, int64_t groups, std::array<bool,3> output_mask) {
  DEBUG("AtenIpexCPUDev::convolution_backward_overrideable\n");
  return mkldnn_convolution_backward(input, grad_output, weight, padding, stride, dilation, groups, output_mask);
}

std::tuple<at::Tensor,at::Tensor,at::Tensor> AtenIpexCPUDev::mkldnn_convolution_backward(const at::Tensor & self, const at::Tensor & grad_output, const at::Tensor & weight, at::IntArrayRef padding, at::IntArrayRef stride, at::IntArrayRef dilation, int64_t groups, std::array<bool,3> output_mask) {
  DEBUG("AtenIpexCPUDev::mkldnn_convolution_backward\n");
  TORCH_INTERNAL_ASSERT(self.defined());
  TORCH_INTERNAL_ASSERT(grad_output.defined());
  TORCH_INTERNAL_ASSERT(weight.defined());
  TORCH_INTERNAL_ASSERT(self.layout() == c10::kStrided);
  TORCH_INTERNAL_ASSERT(grad_output.layout() == c10::kStrided);
  TORCH_INTERNAL_ASSERT(weight.layout() == c10::kStrided);
  auto&& _ipex_self = bridge::shallowFallbackToCPUTensor(self);
  auto&& _ipex_grad_output = bridge::shallowFallbackToCPUTensor(grad_output);
  auto&& _ipex_weight = bridge::shallowFallbackToCPUTensor(weight);
  auto&& _ipex_result = at::mkldnn_convolution_backward(_ipex_self.contiguous(), _ipex_grad_output.contiguous(), _ipex_weight.contiguous(), padding, stride, dilation, groups, output_mask);
  static_cast<void>(_ipex_result); // Avoid warnings in case not used
  return std::tuple<at::Tensor,at::Tensor,at::Tensor>(bridge::shallowUpgradeToDPCPPTensor(std::get<0>(_ipex_result)), bridge::shallowUpgradeToDPCPPTensor(std::get<1>(_ipex_result)), bridge::shallowUpgradeToDPCPPTensor(std::get<2>(_ipex_result)));
}

at::Tensor& AtenIpexCPUDev::dil_add_out(
    at::Tensor& result,
    const at::Tensor& self,
    const at::Tensor& other,
    at::Scalar alpha) {
  DEBUG("AtenIpexCPUDev::dil_add_out\n");
  CHECK_DNNL_OP_PRE_COND(self);
  CHECK_DNNL_OP_PRE_COND(other);
  dil::tensor x = dbl::comm::try_gen_dil_tensor(self);
  dil::tensor y = dbl::comm::try_gen_dil_tensor(other);

  dil::tensor z = dbl::comm::try_gen_dil_tensor(result);
  const std::vector<float> scales{1.0, alpha.to<float>()};
  dil::sum::compute(scales, {x, y}, z);

  return result;
}

at::Tensor AtenIpexCPUDev::dil_add(const at::Tensor& self, const at::Tensor& other, at::Scalar alpha) {
  DEBUG("AtenIpexCPUDev::dil_add\n");
  CHECK_DNNL_OP_PRE_COND(self);
  CHECK_DNNL_OP_PRE_COND(other);
  dil::tensor x = dbl::comm::try_gen_dil_tensor(self);
  dil::tensor y = dbl::comm::try_gen_dil_tensor(other);

  dil::tensor z;
  const std::vector<float> scales{1.0, alpha.to<float>()};
  dil::sum::compute(scales, {x, y}, z);

  return dbl::comm::gen_aten_tensor_by(z);

}

at::Tensor & AtenIpexCPUDev::dil_add_(at::Tensor& self, const at::Tensor& other, at::Scalar alpha) {
  DEBUG("AtenIpexCPUDev::dil_add_\n");
  CHECK_DNNL_OP_PRE_COND(self);

  auto dil_self = dbl::comm::try_gen_dil_tensor(self);
  auto dil_other = dbl::comm::try_gen_dil_tensor(other);

  const std::vector<float> scales{1.0, alpha.to<float>()};
  dil::sum::compute(scales, {dil_self, dil_other}, dil_self);

  return self;
}

at::Tensor& AtenIpexCPUDev::dil_mul_out(at::Tensor& result, const at::Tensor& self, const at::Tensor& other) {
  DEBUG("AtenIpexCPUDev::dil_mul_out\n");
  CHECK_DNNL_OP_PRE_COND(result);
  CHECK_DNNL_OP_PRE_COND(self);
  CHECK_DNNL_OP_PRE_COND(other);

  auto dil_result = dbl::comm::try_gen_dil_tensor(result);
  auto dil_self = dbl::comm::try_gen_dil_tensor(self);
  auto dil_other = dbl::comm::try_gen_dil_tensor(other);

  dil::binary::compute(dil_self, dil_other, dil_result, dil::algorithm::binary_mul);

  return result;
}

at::Tensor AtenIpexCPUDev::dil_mul(const at::Tensor& self, const at::Tensor& other) {
  DEBUG("AtenIpexCPUDev::dil_mul\n");
  at::Tensor result = dbl::comm::empty_dil_tensor(self.sizes(), self.options());
  return dil_mul_out(result, self, other);
}

at::Tensor& AtenIpexCPUDev::dil_mul_(at::Tensor& self, const at::Tensor& other) {
  DEBUG("AtenIpexCPUDev::dil_mul_\n");
  CHECK_DNNL_OP_PRE_COND(self);
  CHECK_DNNL_OP_PRE_COND(other);
  return dil_mul_out(self, self, other);
}

at::Tensor AtenIpexCPUDev::dil_linear(
    const at::Tensor& self,
    const at::Tensor& weight,
    const at::Tensor& bias) {
  DEBUG("AtenIpexCPUDev::dil_linear\n");
  TORCH_CHECK(self.dim() >= 2,
      "dil_linear: input needs to has dim at least 2, input dim ", self.dim());
  TORCH_CHECK(self.is_mkldnn(),
      "dil_linear: input needs to be dil layout");

  // reshape first if input dim is greater than 2 and the reshape will cost a memory copy.
  auto self_reshaped = self.dim() > 2 ? self.reshape({-1, self.size(self.dim() - 1)}) : self;
  const dil::tensor x = dbl::comm::try_gen_dil_tensor(self_reshaped);
  const dil::tensor w = dbl::comm::try_gen_dil_tensor(weight);

  dil::tensor y;
  if (bias.defined()) {
    const dil::tensor b = dbl::comm::try_gen_dil_tensor(bias);
    dil::inner_product_forward::compute(x, w, b, y);
  } else {
    dil::inner_product_forward::compute(x, w, y);
  }

  auto input_size = self.sizes();
  std::vector<int64_t> output_size(input_size.begin(), input_size.end() - 1);
  output_size.push_back(weight.size(0));

  if (self.dim() > 2) {
    return dbl::comm::gen_aten_tensor_by(y).reshape(output_size);
  }
  return dbl::comm::gen_aten_tensor_by(y);
}

at::Tensor dil_linear_backward_input(
    at::IntArrayRef input_size, const at::Tensor& grad_output, const at::Tensor& weight){
  DEBUG("AtenIpexCPUDev::dil_linear_backward_input\n");
  auto grad_output_reshaped = grad_output.dim() > 2 ?
    grad_output.reshape({-1, grad_output.size(grad_output.dim() - 1)}) : grad_output;
  dil::tensor grady = dbl::comm::try_gen_dil_tensor(grad_output_reshaped);
  const dil::tensor w = dbl::comm::try_gen_dil_tensor(weight);

  std::vector<int64_t> input_reshaped_size;
  input_reshaped_size.push_back(grad_output_reshaped.size(0));
  input_reshaped_size.push_back(weight.size(1));

  dil::tensor gradx;
  dil::inner_product_backward_data::compute(
    grady, w, {input_reshaped_size.begin(), input_reshaped_size.end()}, gradx);

  if (input_size.size() > 2) {
    return dbl::comm::gen_aten_tensor_by(gradx).reshape(input_size);
  }
  return dbl::comm::gen_aten_tensor_by(gradx);
}

std::tuple<at::Tensor, at::Tensor> dil_linear_backward_weights(
    const at::Tensor& grad_output, const at::Tensor& input, const at::Tensor& weight, bool bias_defined) {
  DEBUG("AtenIpexCPUDev::dil_linear_backward_weights\n");
  auto grad_output_reshaped = grad_output.dim() > 2 ?
    grad_output.reshape({-1, grad_output.size(grad_output.dim() - 1)}) : grad_output;
  auto input_reshaped = input.dim() > 2 ? input.reshape({-1, input.size(input.dim() - 1)}) : input;

  dil::tensor grady = dbl::comm::try_gen_dil_tensor(grad_output_reshaped);
  dil::tensor x = dbl::comm::try_gen_dil_tensor(input_reshaped);
  dil::tensor gradw, gradb;
  if (bias_defined) {
    dil::inner_product_backward_weights::compute(x, grady, gradw, gradb);
  } else {
    dil::inner_product_backward_weights::compute(x, grady, gradw);
  }

  // Extract device info from weight and data type info from input.
  // since for current BF16 design, input is BF16 tensor while weight is FP32 tensor.  
  auto options = weight.options().dtype(input.scalar_type());
  if (weight.is_mkldnn()) {
    return std::tuple<at::Tensor, at::Tensor>{
      dbl::comm::gen_aten_tensor_by(gradw),
      dbl::comm::gen_aten_tensor_by(gradb)};
  } else {
    return std::tuple<at::Tensor, at::Tensor>{
      dbl::comm::dil_tensor_to_dense(dbl::comm::gen_aten_tensor_by(gradw)),
      dbl::comm::dil_tensor_to_dense(dbl::comm::gen_aten_tensor_by(gradb))};
  }
}

std::tuple<at::Tensor, at::Tensor, at::Tensor> AtenIpexCPUDev::dil_linear_backward(
    const at::Tensor& input, const at::Tensor& grad_output,
    const at::Tensor& weight, std::array<bool,3> output_mask) {
  DEBUG("AtenIpexCPUDev::dil_linear_backward\n");
  at::Tensor grad_input, grad_weight, grad_bias;
  if (output_mask[0]) {
    grad_input = dil_linear_backward_input(input.sizes(), grad_output, weight);
  }
  if (output_mask[1] || output_mask[2]) {
    std::tie(grad_weight, grad_bias) = dil_linear_backward_weights(grad_output, input, weight, output_mask[2]);
  }
  return std::tuple<at::Tensor, at::Tensor, at::Tensor>{grad_input, grad_weight, grad_bias};
}

std::tuple<at::Tensor, at::Tensor> _dil_dropout(
    const at::Tensor& self,
    double ratio) {
  TORCH_CHECK(
      ratio >= 0 && ratio < 1 && self.numel() != 0,
      "dropout probability has to be between 0 and 1, but got ",
      ratio);
  dil::tensor x = dbl::comm::try_gen_dil_tensor(self);
  dil::tensor mask;
  dil::tensor y;
  dil::dropout_forward::compute(x, ratio, y, mask);
  return std::tuple<at::Tensor, at::Tensor>{
      dbl::comm::gen_aten_tensor_by(y),
      dbl::comm::gen_aten_tensor_by(mask)};
}

at::Tensor AtenIpexCPUDev::dil_dropout(const at::Tensor& self, double ratio, bool train) {
  DEBUG("AtenIpexCPUDev::dil_dropout\n");
  return std::get<0>(_dil_dropout(self, ratio));
}

at::Tensor AtenIpexCPUDev::dil_dropout_backward(
    const at::Tensor& grady,
    const at::Tensor& mask,
    double ratio) {
  DEBUG("AtenIpexCPUDev::dil_dropout_backward\n");
  if (ratio == 0 || grady.numel() == 0) {
    return grady;
  }
  dil::tensor dY = dbl::comm::try_gen_dil_tensor(grady);
  dil::tensor mask_dil = dbl::comm::try_gen_dil_tensor(mask);


  dil::tensor dX;
  dil::dropout_backward::compute(mask_dil, dY, dX);
  return dbl::comm::gen_aten_tensor_by(dX);
}

std::tuple<at::Tensor, at::Tensor, at::Tensor> AtenIpexCPUDev::dil_native_batch_norm(
    const at::Tensor& input,
    const at::Tensor& weight,
    const at::Tensor& bias,
    const at::Tensor& running_mean,
    const at::Tensor& running_var,
    bool train,
    double momentum,
    double eps) {
  DEBUG("AtenIpexCPUDev::dil_native_batch_norm\n");
  TORCH_CHECK(input.dim() == 4 || input.dim() == 5,
             "mkldnn_batch_norm: currently mkldnn only support 2d and 3d batchnorm");
  TORCH_CHECK(weight.defined() && bias.defined(),
             "mkldnn_batch_norm: currently mkldnn only support affine model");
  dil::tensor x = dbl::comm::try_gen_dil_tensor(input);
  const dil::tensor w = dbl::comm::try_gen_dil_tensor(weight);
  const dil::tensor b = dbl::comm::try_gen_dil_tensor(bias);
  bool use_running_stat = (running_mean.defined() && running_var.defined());
  dil::tensor y;
  if (train) {
    dil::tensor saved_mean;
    dil::tensor saved_var;
    dil::batch_normalization_forward_training::compute(
        x, w, b, y, saved_mean, saved_var, momentum, eps);
    if (use_running_stat) {
      auto len = x.get_nelems() / w.get_nelems(); // n*h*w
      dil::tensor m = dbl::comm::try_gen_dil_tensor(running_mean);
      dil::tensor v = dbl::comm::try_gen_dil_tensor(running_var);
      const std::vector<float> scales_mean{1 - float(momentum), float(momentum)};
      const std::vector<float> scales_var{1 - float(momentum), float(momentum) * len / (len - 1)};
      dil::sum::compute(scales_mean, {m, saved_mean}, m);
      dil::sum::compute(scales_var, {v, saved_var}, v);
    }
    return std::make_tuple(
        dbl::comm::gen_aten_tensor_by(y),
        dbl::comm::gen_aten_tensor_by(saved_mean),
        dbl::comm::gen_aten_tensor_by(saved_var));
  } else {
    if (use_running_stat) {
      dil::tensor m = dbl::comm::try_gen_dil_tensor(running_mean);
      dil::tensor v = dbl::comm::try_gen_dil_tensor(running_var);
      dil::batch_normalization_forward_inference::compute(
          x, m, v, w, b, y, eps);
    } else {
      dil::batch_normalization_forward_inference::compute(
          x, w, b, y, eps);
    }
    return std::make_tuple(
        dbl::comm::gen_aten_tensor_by(y),
        dbl::comm::gen_aten_tensor_by(dil::tensor{}),
        dbl::comm::gen_aten_tensor_by(dil::tensor{}));
  }
}

std::tuple<at::Tensor, at::Tensor, at::Tensor> AtenIpexCPUDev::dil_native_batch_norm_backward(const at::Tensor& grad_output,
    const at::Tensor& input,
    const at::Tensor& weight,
    const at::Tensor& running_mean,
    const at::Tensor& running_var,
    const at::Tensor& save_mean,
    const at::Tensor& save_invstd,
    bool train,
    double eps,
    std::array<bool,3> grad_input_mask) {
  DEBUG("AtenIpexCPUDev::dil_native_batch_norm_backward\n");
  TORCH_CHECK(train, "mkldnn_batch_norm_backward: currently mkldnn only support train model");
  dil::tensor grady = dbl::comm::try_gen_dil_tensor(grad_output.is_contiguous() ? grad_output : grad_output.contiguous());
  dil::tensor x = dbl::comm::try_gen_dil_tensor(input);
  dil::tensor w = dbl::comm::try_gen_dil_tensor(weight);
  dil::tensor m = dbl::comm::try_gen_dil_tensor(save_mean);
  dil::tensor v = dbl::comm::try_gen_dil_tensor(save_invstd);

  dil::tensor gradx, gradw, gradb;
  dil::batch_normalization_backward::compute(
      x, m, v, grady, w, gradx, gradw, gradb, eps);

  if (weight.is_mkldnn()) {
    return std::make_tuple(
        dbl::comm::gen_aten_tensor_by(gradx),
        dbl::comm::gen_aten_tensor_by(gradw),
        dbl::comm::gen_aten_tensor_by(gradb));
  } else {
    return std::make_tuple(
        dbl::comm::gen_aten_tensor_by(gradx),
        dbl::comm::dil_tensor_to_dense(dbl::comm::gen_aten_tensor_by(gradw)),
        dbl::comm::dil_tensor_to_dense(dbl::comm::gen_aten_tensor_by(gradb)));
  }
}

at::Tensor AtenIpexCPUDev::dil_max_pooling(
    const at::Tensor& input,
    at::IntArrayRef kernel_size,
    at::IntArrayRef stride,
    at::IntArrayRef padding,
    at::IntArrayRef dilation,
    bool ceil_mode) {
  DEBUG("AtenIpexCPUDev::dil_max_pooling\n");
  return dbl::pool::_dil_pooling(
      input.is_contiguous() ? input : input.contiguous(),
      kernel_size,
      stride,
      padding,
      dilation,
      ceil_mode,
      dil::algorithm::pooling_max);
}

at::Tensor AtenIpexCPUDev::dil_avg_pool2d(
    const at::Tensor& input,
    at::IntArrayRef kernel_size,
    at::IntArrayRef stride,
    at::IntArrayRef padding,
    bool ceil_mode,
    bool count_include_pad,
    c10::optional<int64_t> divisor_override) {
  DEBUG("AtenIpexCPUDev::dil_avg_pool2d\n");
  TORCH_CHECK(!divisor_override.has_value(),
           "dil_avg_pooling operator does not support divisor");
  return dbl::pool::_dil_pooling(
      input.is_contiguous() ? input : input.contiguous(),
      kernel_size,
      stride,
      padding,
      /* dilation*/ std::vector<int64_t> {1, 1},
      ceil_mode,
      count_include_pad ? dil::algorithm::pooling_avg_include_padding
                        : dil::algorithm::pooling_avg_exclude_padding);
}

at::Tensor AtenIpexCPUDev::dil_avg_pool3d(
    const at::Tensor& input,
    at::IntArrayRef kernel_size,
    at::IntArrayRef stride,
    at::IntArrayRef padding,
    bool ceil_mode,
    bool count_include_pad,
    c10::optional<int64_t> divisor_override) {
  DEBUG("AtenIpexCPUDev::dil_avg_pool3d\n");
  TORCH_CHECK(!divisor_override.has_value(),
           "dil_avg_pooling operator does not support divisor");
  return dbl::pool::_dil_pooling(
      input.is_contiguous() ? input : input.contiguous(),
      kernel_size,
      stride,
      padding,
      /* dilation*/ std::vector<int64_t> {1, 1, 1},
      ceil_mode,
      count_include_pad ? dil::algorithm::pooling_avg_include_padding
                        : dil::algorithm::pooling_avg_exclude_padding);
}

at::Tensor AtenIpexCPUDev::dil_adaptive_avg_pooling(
    at::Tensor const& input,
    at::IntArrayRef output_size) {
  DEBUG("AtenIpexCPUDev::dil_adaptive_avg_pooling\n");
  auto output_size_vec =
      dbl::pool::expand_param_if_needed(output_size, "output_size", input.dim() - 2);
  std::vector<int64_t> kernel_size(input.dim() - 2);
  for (int64_t i = 2; i < input.dim(); ++i) {
    auto s1 = input.size(i);
    auto s2 = output_size_vec[i - 2];
    TORCH_CHECK(s2 != 0, "output size can not be zero");
    TORCH_CHECK(
        s1 % s2 == 0,
        "input size is not divisible by the output size is not supported yet");
    kernel_size[i - 2] = s1 / s2;
  }
  std::vector<int64_t> padding{0, 0};
  std::vector<int64_t> dilation{1, 1};
  
  if (input.dim() == 5) {
    padding.push_back(0);
    dilation.push_back(1);
  }

  return dbl::pool::_dil_pooling(
      input.is_contiguous() ? input : input.contiguous(),
      kernel_size,
      /*stride*/ kernel_size,
      /*padding*/ padding,
      /*dilation*/ dilation,
      /*ceil_mode*/ false,
      /*algo*/ dil::algorithm::pooling_avg);
}

at::Tensor AtenIpexCPUDev::dil_max_pooling_backward(
    const at::Tensor& grad_output,
    const at::Tensor& output,
    const at::Tensor& input,
    at::IntArrayRef kernel_size,
    at::IntArrayRef stride,
    at::IntArrayRef padding,
    at::IntArrayRef dilation,
    bool ceil_mode) {
  DEBUG("AtenIpexCPUDev::dil_max_pooling_backward\n");
  return dbl::pool::_dil_pooling_backward(
      grad_output.is_contiguous() ? grad_output : grad_output.contiguous(),
      output.is_contiguous() ? output : output.contiguous(),
      input.is_contiguous() ? input : input.contiguous(),
      kernel_size,
      stride,
      padding,
      dilation,
      ceil_mode,
      dil::algorithm::pooling_max);
}

at::Tensor AtenIpexCPUDev::dil_avg_pool2d_backward(
    const at::Tensor& grad_output,
    const at::Tensor& input,
    at::IntArrayRef kernel_size,
    at::IntArrayRef stride,
    at::IntArrayRef padding,
    bool ceil_mode,
    bool count_include_pad,
    c10::optional<int64_t> divisor_override) {
  DEBUG("AtenIpexCPUDev::dil_avg_pool2d_backward\n");
  
  return dbl::pool::_dil_pooling_backward(
      grad_output.is_contiguous() ? grad_output : grad_output.contiguous(),
      grad_output.is_contiguous() ? grad_output : grad_output.contiguous(),
      input.is_contiguous() ? input : input.contiguous(),
      kernel_size,
      stride,
      padding,
      /* dilation */ std::vector<int64_t>{1, 1},
      ceil_mode,
      count_include_pad ? dil::algorithm::pooling_avg_include_padding
                        : dil::algorithm::pooling_avg_exclude_padding);
}

at::Tensor AtenIpexCPUDev::dil_avg_pool3d_backward(
    const at::Tensor& grad_output,
    const at::Tensor& input,
    at::IntArrayRef kernel_size,
    at::IntArrayRef stride,
    at::IntArrayRef padding,
    bool ceil_mode,
    bool count_include_pad,
    c10::optional<int64_t> divisor_override) {
  DEBUG("AtenIpexCPUDev::dil_avg_pool3d_backward\n");
  std::vector<int64_t> dilation{1, 1};
  return dbl::pool::_dil_pooling_backward(
      grad_output.is_contiguous() ? grad_output : grad_output.contiguous(),
      grad_output.is_contiguous() ? grad_output : grad_output.contiguous(),
      input.is_contiguous() ? input : input.contiguous(),
      kernel_size,
      stride,
      padding,
      /* dilation */ std::vector<int64_t>{1, 1, 1},
      ceil_mode,
      count_include_pad ? dil::algorithm::pooling_avg_include_padding
                        : dil::algorithm::pooling_avg_exclude_padding);
}

at::Tensor AtenIpexCPUDev::dil_adaptive_avg_pooling_backward(
    const at::Tensor& grad_output,
    const at::Tensor& input) {
  DEBUG("AtenIpexCPUDev::dil_adaptive_avg_pooling_backward\n");
  auto output_size_vec = grad_output.sizes();
  std::vector<int64_t> kernel_size(input.dim() - 2);
  for (size_t i = 2; i < input.dim(); ++i) {
    auto s1 = input.size(i);
    auto s2 = output_size_vec[i];
    TORCH_CHECK(s2 != 0, "output size can not be zero");
    TORCH_CHECK(
        s1 % s2 == 0,
        "input size is not divisible by the output size is not supported yet");
    kernel_size[i - 2] = s1 / s2;
  }
  std::vector<int64_t> padding{0, 0};
  std::vector<int64_t> dilation{1, 1};
  
  if (input.dim() == 5) {
    padding.push_back(0);
    dilation.push_back(1);
  }

 
  return dbl::pool::_dil_pooling_backward(
      grad_output,
      grad_output,
      input.is_contiguous() ? input : input.contiguous(),
      kernel_size,
      /*stride*/ kernel_size,
      /*padding*/ padding,
      /*dilation*/ dilation,
      false,
      /*algo*/ dil::algorithm::pooling_avg);
}

at::Tensor AtenIpexCPUDev::dil_relu(const at::Tensor& input) {
  DEBUG("AtenIpexCPUDev::dil_relu\n");
  const dil::tensor& x = dbl::comm::try_gen_dil_tensor(input);
  dil::tensor y;
  dil::eltwise_forward::compute(
      x, y, dil::algorithm::eltwise_relu, dil::prop_kind::forward_training, /*alpha*/ 0.0);
  return dbl::comm::gen_aten_tensor_by(y);
}

at::Tensor& AtenIpexCPUDev::dil_relu_(at::Tensor& input) {
  DEBUG("AtenIpexCPUDev::dil_relu_\n");
  auto dil_self = dbl::comm::try_gen_dil_tensor(input);
  dil::eltwise_forward::compute(
    dil_self,
    dil_self,
    dil::algorithm::eltwise_relu,
    dil::prop_kind::forward_training,
    /*alpha*/ 0.0);
  return input;
}

at::Tensor AtenIpexCPUDev::dil_relu_backward(const at::Tensor& grad_output, const at::Tensor& input, at::Scalar threshold) {
  DEBUG("AtenIpexCPUDev::dil_relu_backward\n");
  // TODO: support bounded relu. `threshold` is ignored for now
  dil::tensor x = dbl::comm::try_gen_dil_tensor(input);
  dil::tensor grady = dbl::comm::try_gen_dil_tensor(grad_output);
  dil::tensor gradx;
  dil::eltwise_backward::compute(x, grady, gradx,
      dil::algorithm::eltwise_relu, /*alpha*/ 0.0);
  return dbl::comm::gen_aten_tensor_by(gradx);
}

at::Tensor AtenIpexCPUDev::dil__softmax(
    const at::Tensor& self,
    const int64_t dim,
    bool half_to_float) {
  DEBUG("AtenIpexCPUDev::dil_softmax\n");
  AT_ASSERTM(
      !half_to_float,
      "softmax with half to float conversion is not supported on Mkldnn");
  const int64_t wrapped_dim = at::maybe_wrap_dim(dim, self.dim());
  dil::tensor x = dbl::comm::try_gen_dil_tensor(self);
  dil::tensor y;
  dil::softmax_forward::compute(x, y, wrapped_dim);
  return dbl::comm::gen_aten_tensor_by(y);
}

at::Tensor AtenIpexCPUDev::dil__softmax_backward_data(
    const at::Tensor& grad_output,
    const at::Tensor& output,
    int64_t dim,
    const at::Tensor& self) {
  DEBUG("AtenIpexCPUDev::dil_softmax_backward\n");
  const int64_t wrapped_dim = at::maybe_wrap_dim(dim, self.dim());
  dil::tensor y = dbl::comm::try_gen_dil_tensor(output);
  dil::tensor grady = dbl::comm::try_gen_dil_tensor(grad_output.is_contiguous() ? grad_output : grad_output.contiguous());
  dil::tensor gradx;
  dil::softmax_backward::compute(y, grady, gradx, wrapped_dim);
  return dbl::comm::gen_aten_tensor_by(gradx);
}

at::Tensor AtenIpexCPUDev::dil_sigmoid(const at::Tensor& self) {
  DEBUG("AtenIpexCPUDev::dil_sigmoid\n");
  dil::tensor x = dbl::comm::try_gen_dil_tensor(self);
  dil::tensor y;
  dil::eltwise_forward::compute(
      x, y, dil::algorithm::eltwise_logistic, dil::prop_kind::forward);
  return dbl::comm::gen_aten_tensor_by(y);
}

at::Tensor& AtenIpexCPUDev::dil_sigmoid_(at::Tensor& self) {
  DEBUG("AtenIpexCPUDev::dil_sigmoid_\n");
  dil::tensor x = dbl::comm::try_gen_dil_tensor(self);
  dil::eltwise_forward::compute(
      x, x, dil::algorithm::eltwise_logistic, dil::prop_kind::forward);
  return self;
}

at::Tensor AtenIpexCPUDev::dil_sigmoid_backward(
    const at::Tensor& grad_output,
    const at::Tensor& output) {
  DEBUG("AtenIpexCPUDev::dil_sigmoid_backward\n");
  dil::tensor y = dbl::comm::try_gen_dil_tensor(output);
  dil::tensor gy = dbl::comm::try_gen_dil_tensor(grad_output.is_contiguous() ? grad_output : grad_output.contiguous());
  dil::tensor gx;
  dil::eltwise_backward::compute(y, gy, gx,
      dil::algorithm::eltwise_logistic);
  return dbl::comm::gen_aten_tensor_by(gx);
}

at::Tensor AtenIpexCPUDev::dil_reshape(const at::Tensor& self, at::IntArrayRef size) {
  DEBUG("AtenIpexCPUDev::dil_reshape\n");
  auto inferred_size = at::infer_size(size, self.numel());
  if (self.sizes() == inferred_size) {
    return self;
  }
  const dil::tensor x = dbl::comm::try_gen_dil_tensor(self);
  dil::tensor y{x};
  y.reshape(inferred_size);
  return dbl::comm::gen_aten_tensor_by(y);
}

at::Tensor AtenIpexCPUDev::dil_clone(const at::Tensor& self, c10::optional<c10::MemoryFormat> optional_memory_format) {
  DEBUG("AtenIpexCPUDev::dil_clone\n");
  TORCH_CHECK(
      !optional_memory_format.has_value(),
      "unsupported memory format option ",
      optional_memory_format.value());
  dil::tensor src = dbl::comm::try_gen_dil_tensor(self);
  dil::tensor dst;
  dil::direct_copy::compute(src, dst);
  return dbl::comm::gen_aten_tensor_by(dst);
}

at::Tensor AtenIpexCPUDev::dil_transpose(const at::Tensor & self, int64_t dim0, int64_t dim1) {
  DEBUG("AtenIpexCPUDev::dil_transpose\n");
  const dil::tensor& x = dbl::comm::try_gen_dil_tensor(self);
  dil::tensor y;
  std::vector<int> axes(x.ndims());
  std::iota(axes.begin(), axes.end(), 0);
  std::swap(axes[dim0], axes[dim1]);
  y.transpose_from(x, axes);
  return dbl::comm::gen_aten_tensor_by(y);
}

inline void check_cat_no_zero_dim(at::TensorList tensors) {
  for (size_t i = 0; i < tensors.size(); ++i) {
    auto& t = tensors[i];
    TORCH_CHECK(t.dim() > 0,
      "zero-dimensional tensor (at position ", i, ") cannot be concatenated");
  }
}

at::Tensor& AtenIpexCPUDev::dil_cat_out(at::Tensor& result, at::TensorList tensors, int64_t dim) {
  DEBUG("AtenIpexCPUDev::dil_cat_out\n");
  check_cat_no_zero_dim(tensors);
  dim = legacy_cat_wrap_dim(dim, tensors);
  std::vector<dil::tensor> x;
  for (auto i =0; i< tensors.size(); i++) {
    TORCH_CHECK(!(tensors[i].dim() == 1 && tensors[i].sizes()[0] == 0),
      "Currently Mkldnn cat operators do not support empty tensor.");
    x.push_back(dbl::comm::try_gen_dil_tensor(tensors[i]));
  }
  dil::tensor y = dbl::comm::try_gen_dil_tensor(result);
  dil::concat::compute(x, dim, y);
  return result;
}

at::Tensor AtenIpexCPUDev::dil_cat(at::TensorList tensors, int64_t dim) {
  DEBUG("AtenIpexCPUDev::dil_cat\n");
  check_cat_no_zero_dim(tensors);
  dim = legacy_cat_wrap_dim(dim, tensors);
  std::vector<dil::tensor> x;
  for (auto i = 0; i < tensors.size(); i++) {
    TORCH_CHECK(!(tensors[i].dim() == 1 && tensors[i].sizes()[0] == 0),
      "Currently Mkldnn cat operators do not support empty tensor.");
    x.push_back(dbl::comm::try_gen_dil_tensor(tensors[i].is_contiguous() ? tensors[i] : tensors[i].contiguous()));
  }
  dil::tensor y;
  dil::concat::compute(x, dim, y);
  return dbl::comm::gen_aten_tensor_by(y);
}

std::vector<at::Tensor> AtenIpexCPUDev::dil_split_with_sizes(const at::Tensor& self, at::IntArrayRef split_sizes, int64_t dim) {
  DEBUG("AtenIpexCPUDev::dil_split_with_sizes\n");
  dil::tensor x = dbl::comm::try_gen_dil_tensor(self);
  int64_t num_splits = split_sizes.size();
  std::vector<at::Tensor> splits(num_splits);
  std::vector<int32_t> sizes;
  for (auto i = 0; i < num_splits; i++) {
    auto length = split_sizes[i];
    TORCH_CHECK(length >= 0,
             "split_with_sizes expects split_sizes have only non-negative ",
             "entries, but got split_sizes=", split_sizes);
    sizes.push_back((int32_t)length);
  }
  auto y = dil::spliter::compute(x, sizes, dim, false);
  for (auto j = 0; j < num_splits; j++) {
    splits[j] = dbl::comm::gen_aten_tensor_by(y[j]);
  }
  return splits;
}

std::vector<at::Tensor> AtenIpexCPUDev::dil_split(const at::Tensor& self, int64_t split_size, int64_t dim) {
  DEBUG("AtenIpexCPUDev::dil_split\n");
  int64_t dim_size = self.size(dim);
  int64_t num_splits = 1;
  if (split_size != 0) {
    // ensuring num_splits is at least 1 makes consistent the case where split_size > dim_size
    // (returns a single split).  We might want to error here, but keep it for BC.
    num_splits = std::max<int64_t>((dim_size + split_size - 1) / split_size, 1);
  }
  std::vector<int64_t> split_sizes(num_splits, split_size);
  int64_t last_split_size = split_size - (split_size * num_splits - dim_size);
  split_sizes[num_splits-1] = last_split_size;
  return dil_split_with_sizes(self, split_sizes, dim);
}

}  // namespace cpu
}  // namespace torch_ipex
