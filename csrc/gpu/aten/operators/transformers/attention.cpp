#include <ATen/ATen.h>
#include <utils/DPCPP.h>

//#include <xetla/kernels/SDP/mha_forward.h>
#include <ATen/record_function.h>
#include <xetla/mha.h>
#include "../comm/ATDispatch.h"
#include "NaiveScaledDotProduct.h"
#include "sdp_utils.h"
#include "sdp_utils_cpp.h"
#include "utils/CustomOperatorRegistration.h"

namespace at {
namespace AtenIpexTypeXPU {

std::tuple<Tensor, Tensor> _scaled_dot_product_efficient_attention(
    const Tensor& query,
    const Tensor& key,
    const Tensor& value,
    bool compute_log_sumexp,
    bool is_causal) {
  RECORD_FUNCTION("xetla_fsdp_forward_no_mask_no_casual_no_stride", {});
  // auto result = naive_scaled_dot_product(query, key, value, is_causal);
  // std::cout << "this is _scaled_dot_product_efficient_attention!!!!!\n";
#if defined(USE_XETLA)
  // std::cout << "go fmha_forward ......\n";
  auto output = at::empty_like(query);
  auto output_lm = at::empty_like(query);
  auto dpcpp_queue = dpcppGetCurrentQueue();
  gpu::xetla::fmha_forward_op(
      dpcpp_queue,
      query.data_ptr(),
      key.data_ptr(),
      value.data_ptr(),
      output.data_ptr(),
      query.size(0),
      query.size(1),
      query.size(3),
      query.size(2),
      key.size(2));
  return std::forward_as_tuple(output, output_lm);
#else
  auto result = naive_scaled_dot_product(query, key, value, is_causal);
  return std::forward_as_tuple(std::get<0>(result), std::get<1>(result));
#endif
}

std::tuple<
    Tensor,
    Tensor,
    Tensor,
    Tensor,
    int64_t,
    int64_t,
    int64_t,
    int64_t,
    Tensor>
_scaled_dot_product_flash_attention(
    const Tensor& query,
    const Tensor& key,
    const Tensor& value,
    double dropout_p,
    bool is_causal,
    bool return_debug_mask) {
  TORCH_CHECK(
      false,
      "'_scaled_dot_product_flash_attention' hasn't been implemented, we should have falled back to the math path.");
  // TODO: Implement flash attention algorithm.
}

int64_t _fused_sdp_choice(
    const Tensor& query_,
    const Tensor& key,
    const Tensor& value,
    const c10::optional<Tensor>& attn_mask_,
    double dropout_p,
    bool is_causal) {
  sdp::sdp_params kernel_params{
      query_, key, value, attn_mask_.has_value(), dropout_p, is_causal};
  auto backend = select_sdp_backend(kernel_params);
  if (backend == sdp::SDPBackend::error) {
    TORCH_CHECK(
        false,
        "No viable backend for scaled_dot_product_attention was found. ",
        "This is likely due to turning off both the math kernel and the fused kernels.");
  }
  return static_cast<int64_t>(backend);
}

inline void validate_sdpa_input(
    const Tensor& query_,
    const Tensor& key,
    const Tensor& value,
    const c10::optional<Tensor>& attn_mask_,
    double dropout_p,
    bool is_causal) {
  TORCH_CHECK(
      query_.dtype() == key.dtype() && query_.dtype() == value.dtype(),
      "Expected query, key, and value to have the same dtype, but got query.dtype: ",
      query_.dtype(),
      " key.dtype: ",
      key.dtype(),
      " and value.dtype: ",
      value.dtype(),
      " instead.");
  TORCH_CHECK(
      query_.device() == key.device() && query_.device() == value.device(),
      "Expected query, key, and value to have the same device type, but got query.device: ",
      query_.device(),
      " key.device: ",
      key.device(),
      " and value.device: ",
      value.device(),
      " instead.");
  TORCH_CHECK(
      query_.dim() >= 2 && key.dim() >= 2 && value.dim() >= 2,
      "Expected query, key, and value to all be  at least 2 dimensional, but got query.dim: ",
      query_.dim(),
      " key.dim: ",
      key.dim(),
      " and value.dim: ",
      value.dim(),
      " instead.");
  if (attn_mask_.has_value()) {
    auto mask_dtype = attn_mask_->dtype();
    TORCH_CHECK(
        mask_dtype == at::kBool || mask_dtype == query_.dtype(),
        "Expected attn_mask dtype to be bool or to match query dtype, but got attn_mask.dtype: ",
        mask_dtype,
        " and  query.dtype: ",
        query_.dtype(),
        " instead.");
  }
  return;
}

Tensor scaled_dot_product_attention(
    const Tensor& query_,
    const Tensor& key,
    const Tensor& value,
    const c10::optional<Tensor>& attn_mask_,
    double dropout_p,
    bool is_causal) {
  validate_sdpa_input(query_, key, value, attn_mask_, dropout_p, is_causal);
  int64_t choice_int = static_cast<int64_t>(sdp::SDPBackend::math);
  choice_int = at::_fused_sdp_choice(
      query_, key, value, attn_mask_, dropout_p, is_causal);
  sdp::SDPBackend backend = static_cast<sdp::SDPBackend>(choice_int);
  switch (backend) {
    case sdp::SDPBackend::flash_attention: {
      TORCH_WARN(
          "flash_attention algorithm hasn't been implemented, we will fall back to the math path.")
      return std::get<0>(at::_scaled_dot_product_attention_math(
          query_, key, value, attn_mask_, dropout_p, is_causal));
    }
    case sdp::SDPBackend::efficient_attention: {
      if (query_.sizes()[2] == 1) {
        bool compute_logsumexp =
            (query_.requires_grad() || key.requires_grad() ||
             value.requires_grad());
        auto out_and_lse = at::_scaled_dot_product_efficient_attention(
            query_, key, value, compute_logsumexp, is_causal);
        return std::get<0>(out_and_lse);
      } else {
        // std::cout << "will go math!!!!!!\n";
        return std::get<0>(at::_scaled_dot_product_attention_math(
            query_, key, value, attn_mask_, dropout_p, is_causal));
      }
    }
    case sdp::SDPBackend::math:
      // std::cout << "will go math!!!!!!\n";
      return std::get<0>(at::_scaled_dot_product_attention_math(
          query_, key, value, attn_mask_, dropout_p, is_causal));
    default:
      TORCH_CHECK(
          false,
          "No viable backend for scaled_dot_product_attention was found.");
      return Tensor();
  }
}

} // namespace AtenIpexTypeXPU
} // namespace at
  //
// namespace {
// IPEX_LIBRARY_FRAGMENT() {
//  IPEX_OP_REGISTER_DISPATCH(
//      "mha_forward", at::AtenIpexTypeXPU::mha_forward, c10::DispatchKey::XPU);
//}
//} // namespace
