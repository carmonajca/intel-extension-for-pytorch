import contextlib
import sympy

from torch._dynamo.utils import dynamo_timed

from torch._inductor import config
from torch._inductor.virtualized import V
from torch._inductor.codegen.common import IndentedBuffer, PythonPrinter
from torch._inductor.codegen.wrapper import (
    WrapperCodeGen,
    WrapperLine,
    EnterDeviceContextManagerLine,
    ExitDeviceContextManagerLine,
)
from torch._inductor.utils import cache_on_self, get_benchmark_name
from .. import codecache

pexpr = PythonPrinter().doprint


class XPUTritonWrapperCodeGen(WrapperCodeGen):
    """
    Generate outer wrapper in Python that calls the kernels.
    """

    def __init__(self):
        super().__init__()

    def write_header(self):
        self.header.splice(
            f"""
                from ctypes import c_void_p, c_long
                import torch
                import intel_extension_for_pytorch
                import math
                import random
                import os
                import tempfile
                from math import inf, nan
                from torch._inductor.hooks import run_intermediate_hooks
                from torch._inductor.utils import maybe_profile
                from torch._inductor.codegen.memory_planning import _align as align

                from torch import empty_strided, device
                from {codecache.__name__} import XPUAsyncCompile
                from torch._inductor.select_algorithm import extern_kernels
                from torch._inductor.codegen.multi_kernel import MultiKernelCall

                aten = torch.ops.aten
                inductor_ops = torch.ops.inductor
                assert_size_stride = torch._C._dynamo.guards.assert_size_stride
                empty_strided_cpu = torch._C._dynamo.guards._empty_strided_cpu
                assert_size_stride = torch._C._dynamo.guards.assert_size_stride
                alloc_from_pool = torch.ops.inductor._alloc_from_pool
                reinterpret_tensor = torch.ops.inductor._reinterpret_tensor
                async_compile = XPUAsyncCompile()

            """
        )

    @cache_on_self
    def write_triton_header_once(self):
        self.header.splice(
            """
            import triton
            import triton.language as tl
            from torch._inductor.triton_heuristics import grid, split_scan_grid, start_graph, end_graph
            from torch._C import _xpu_getCurrentRawStream as get_raw_stream
            """
        )

    def write_prefix(self):
        self.prefix.splice(
            """

            async_compile.wait(globals())
            del async_compile

            def call(args):
            """
        )
        with self.prefix.indent():
            if config.triton.debug_sync_graph:
                self.prefix.writeline("torch.xpu.synchronize()")
            inp_len = len(V.graph.graph_inputs.keys())
            if inp_len != 0:
                lhs = f"{', '.join(V.graph.graph_inputs.keys())}{'' if inp_len != 1 else ','}"
                self.prefix.writeline(f"{lhs} = args")
                self.prefix.writeline("args.clear()")

            self.codegen_inputs(self.prefix, V.graph.graph_inputs)
            if config.size_asserts:
                self.codegen_input_size_asserts()

    def write_get_raw_stream(self, device_idx: int, graph=None) -> str:
        self.write_triton_header_once()
        name = f"stream{device_idx}"
        self.writeline(f"{name} = get_raw_stream({device_idx})")
        return name

    def codegen_device_guard_enter(self, device_idx):
        self.writeline(
            EnterDeviceContextManagerLine(device_idx, self.last_seen_device_guard_index)
        )
        self.last_seen_device_guard_index = False

    def codegen_device_guard_exit(self):
        self.lines.append(ExitDeviceContextManagerLine())

    @dynamo_timed
    def generate(self, is_inference):
        if config.profile_bandwidth:
            self.write_triton_header_once()
        result = IndentedBuffer()
        result.splice(self.header)

        with contextlib.ExitStack() as stack:
            stack.enter_context(self.wrapper_call.indent())
            if config.profiler_mark_wrapper_call:
                self.generate_profiler_mark_wrapper_call(stack)
            if config.profile_bandwidth:
                self.generate_start_graph()

            # We disable planning during training because it presently increases peak memory consumption.
            if is_inference and config.memory_planning:
                self.memory_plan()
                # TODO: integrate memory planning & stack allocation?
                self.allow_stack_allocation = False
            else:
                self.memory_plan_reuse()

            if config.triton.store_cubin:
                self.generate_reset_kernel_saved_flags()

            for line in self.lines:
                if isinstance(line, WrapperLine):
                    line.codegen(self.wrapper_call)
                else:
                    self.wrapper_call.writeline(line)

            output_refs = self.get_output_refs()
            self.mark_output_type()
            if config.triton.debug_sync_graph:
                self.wrapper_call.writeline(V.graph.device_ops.synchronize())

            if config.profile_bandwidth:
                self.generate_end_graph()

            if config.triton.store_cubin:
                self.generate_save_uncompiled_kernels()

            self.generate_return(output_refs)

        self.finalize_prefix()
        result.splice(self.prefix)

        with result.indent():
            result.splice(self.wrapper_call)

        self.generate_before_suffix(result)
        result.splice(self.suffix)

        self.generate_end(result)

        self.add_benchmark_harness(result)

        return result.getvaluewithlinemap()

    def benchmark_compiled_module(self, output):
        def add_fake_input(name, shape, stride, device, dtype):
            output.writeline(
                f"{name} = rand_strided("
                f"{self.codegen_python_shape_tuple(shape)}, "
                f"{self.codegen_python_shape_tuple(stride)}, "
                f"device='{device}', dtype={dtype})"
            )

        def add_expr_input(name, val):
            output.writeline(f"{name} = {val}")

        output.writelines(
            ["", "", "def benchmark_compiled_module(times=10, repeat=10):"]
        )
        with output.indent():
            output.splice(
                """
                from torch._dynamo.testing import rand_strided
                from torch._inductor.utils import print_performance
                """,
                strip=True,
            )

            for name, value in V.graph.constants.items():
                # all the constants are global variables, that's why we need
                # these 'global var_name' lines
                output.writeline(f"global {name}")
                add_fake_input(
                    name, value.size(), value.stride(), value.device, value.dtype
                )

            for name, value in V.graph.graph_inputs.items():
                if isinstance(value, sympy.Expr):  # Don't need to add symbolic
                    add_expr_input(name, V.graph.sizevars.size_hint(value))
                else:
                    shape = [V.graph.sizevars.size_hint(x) for x in value.get_size()]
                    stride = [V.graph.sizevars.size_hint(x) for x in value.get_stride()]
                    add_fake_input(
                        name, shape, stride, value.get_device(), value.get_dtype()
                    )

            call_str = f"call([{', '.join(V.graph.graph_inputs.keys())}])"
            output.writeline(
                f"return print_performance(lambda: {call_str}, times=times, repeat=repeat, device='xpu')"
            )

    def add_benchmark_harness(self, output):
        """
        Append a benchmark harness to generated code for debugging
        """
        if not config.benchmark_harness:
            return

        self.benchmark_compiled_module(output)

        output.writelines(["", "", 'if __name__ == "__main__":'])
        with output.indent():
            output.writelines(
                [
                    "from intel_extension_for_pytorch._inductor.xpu.wrapper_benchmark import compiled_module_main",
                    f"compiled_module_main('{get_benchmark_name()}', benchmark_compiled_module)",
                ]
            )
