from enum import Enum
from intel_extension_for_pytorch.transformers.models.cpu.fusions.linear_fusion import (
    _IPEXlinearAddCPU,
    _IPEXlinearAddAddCPU,
    _IPEXlinearNewGeluCPU,
    _IPEXlinearReluCPU,
    _IPEXlinearGeluCPU,
    _IPEXlinearMulCPU,
    _IPEXlinearSiluCPU,
    _IPEXlinearSiluMulCPU,
    _IPEXlinearSiluAndMulCPU,
)

from intel_extension_for_pytorch.transformers.models.cpu.fusions.mha_fusion import (
    _IPEXRopeCPU,
    _IPEXScaleDotProductCPU,
    _IPEXVarlenScaledDotProductCPU,
    _IPEXRMSNormCPU,
    _IPEXFastLayerNormCPU,
    _IPEXPagedAttentionCPU,
)

from intel_extension_for_pytorch.transformers.models.xpu.fusions.mha_fusion import (
    _IPEXFastLayerNormXPU,
    _IPEXRopeXPU,
    _IPEXRMSNormXPU,
    _IPEXPagedAttentionXPU,
    _IPEXVarlenScaledDotProductXPU,
)


class IPEXCustomOpType(Enum):
    LINEAR_SILU: int = 0
    LINEAR_SILU_MUL: int = 1
    LINEAR2_SILU_MUL: int = 2
    LINEAR_RELU: int = 3
    LINEAR_NEW_GELU: int = 4
    LINEAR_GELU: int = 5
    LINEAR_ADD: int = 6
    LINEAR_ADD_ADD: int = 7
    LINEAR_MUL: int = 8
    ROPE: int = 9
    RMS_NORM: int = 10
    PAGED_ATTENTION: int = 11
    FAST_LAYERNORM: int = 12
    VARLEN_ATTENTION: int = 13
    INDIRECTACCESS_KVCACHE: int = 14


CPU_fusion_modules = {
    IPEXCustomOpType.ROPE: _IPEXRopeCPU,
    IPEXCustomOpType.RMS_NORM: _IPEXRMSNormCPU,
    IPEXCustomOpType.PAGED_ATTENTION: _IPEXPagedAttentionCPU,
    IPEXCustomOpType.FAST_LAYERNORM: _IPEXFastLayerNormCPU,
    IPEXCustomOpType.VARLEN_ATTENTION: _IPEXVarlenScaledDotProductCPU,
    IPEXCustomOpType.INDIRECTACCESS_KVCACHE: _IPEXScaleDotProductCPU,
    IPEXCustomOpType.LINEAR_SILU: _IPEXlinearSiluCPU,
    IPEXCustomOpType.LINEAR_SILU_MUL: _IPEXlinearSiluAndMulCPU,
    IPEXCustomOpType.LINEAR2_SILU_MUL: _IPEXlinearSiluMulCPU,
    IPEXCustomOpType.LINEAR_RELU: _IPEXlinearReluCPU,
    IPEXCustomOpType.LINEAR_NEW_GELU: _IPEXlinearNewGeluCPU,
    IPEXCustomOpType.LINEAR_GELU: _IPEXlinearGeluCPU,
    IPEXCustomOpType.LINEAR_ADD: _IPEXlinearAddCPU,
    IPEXCustomOpType.LINEAR_ADD_ADD: _IPEXlinearAddAddCPU,
    IPEXCustomOpType.LINEAR_MUL: _IPEXlinearMulCPU,
}


XPU_fusion_modules = {
    IPEXCustomOpType.ROPE: _IPEXRopeXPU,
    IPEXCustomOpType.RMS_NORM: _IPEXRMSNormXPU,
    IPEXCustomOpType.PAGED_ATTENTION: _IPEXPagedAttentionXPU,
    IPEXCustomOpType.FAST_LAYERNORM: _IPEXFastLayerNormXPU,
    IPEXCustomOpType.VARLEN_ATTENTION: _IPEXVarlenScaledDotProductXPU,
}


class IPEXRuntimeCustomOps:
    def __init__(self):
        super().__init__()
        self.device_type = None
        self.runtime_module = None
        self.fusion_modules = {
            "cpu": CPU_fusion_modules,
            "xpu": XPU_fusion_modules,
        }

    def get_module_from_device(
        self,
        device_type: str,
        ops: IPEXCustomOpType,
        is_instance: bool,
        *args,
        **kwargs,
    ):
        if device_type is not self.device_type:
            assert device_type in [
                "cpu",
                "xpu",
            ], f"""The input parameter's device is not supported in ipex, we only support XPU and CPU device,
                "but what we get is {device_type}."""
            self.device_type = device_type
            if not is_instance:
                self.runtime_module = self.fusion_modules[device_type][ops]
                return self.runtime_module
            else:
                self.runtime_module = self.fusion_modules[device_type][ops](
                    *args, **kwargs
                )
                return self.runtime_module

        return self.runtime_module
