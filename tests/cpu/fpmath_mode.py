import argparse
import torch
import torch.nn as nn
import intel_extension_for_pytorch as ipex


class TestModel(torch.nn.Module):
    def __init__(self, ic, oc, bias):
        super(TestModel, self).__init__()
        self.conv = torch.nn.Conv2d(3, 64, kernel_size=(7, 7), stride=(2, 2), padding=(3, 3), bias=bias)
        self.linear = nn.Linear(ic, oc, bias=bias)

    def forward(self, x):
        y1 = self.conv(x)
        y2 = torch.matmul(y1, torch.transpose(y1, 0, 1))
        y3 = self.linear(y2)
        return y3

def run_model(args):
    mode = args.mode
    fpmath_mode = ipex.FP32MathMode.BF32 if args.fpmath == "BF32" else ipex.FP32MathMode.FP32
    if not args.env:
        ipex.set_fp32_math_mode(mode=fpmath_mode, device="cpu")
    inputs = torch.randn(1, 3, 224, 224).requires_grad_()
    if args.bias:
        model = TestModel(112, 10, True).eval()
    else:
        model = TestModel(112, 10, False).eval()
    model = ipex.optimize(model, dtype=torch.float32, level='O1', auto_kernel_selection=True)
    if mode == "jit":
        model = torch.jit.trace(model, inputs).eval()
        model = torch.jit.freeze(model)
    output = model(inputs)
    output.sum().backward()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", default="imperative", type=str)
    parser.add_argument("--fpmath", default="BF32", type=str)
    parser.add_argument("--env", action='store_true', default=False)
    parser.add_argument("--bias", default=False, type=bool)
    args = parser.parse_args()
    run_model(args)
