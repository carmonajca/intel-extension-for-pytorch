import torch
from torch.testing._internal.common_utils import TestCase

import intel_extension_for_pytorch  # noqa

cpu_device = torch.device("cpu")
dpcpp_device = torch.device("xpu")

class TestTorchMethod(TestCase):
    def test_mul_1(self, dtype=torch.float):
        batch1 = torch.randn((1, 1, 1, 512), device=cpu_device)
        batch1_dpcpp = batch1.to(dpcpp_device)
        self.assertEqual(
            torch.mul(batch1, 2),
            torch.mul(batch1_dpcpp, 2).to(cpu_device),
        )

    def test_mul_2(self, dtype=torch.float):
        batch1 = torch.randn((2), device=cpu_device)
        batch1_dpcpp = batch1.to(dpcpp_device)
        self.assertEqual(
            torch.mul(batch1, 2),
            torch.mul(batch1_dpcpp, 2).to(cpu_device),
        )

    def test_mul_3(self, dtype=torch.float):
        batch1 = torch.randn((1024), device=cpu_device)
        batch1_dpcpp = batch1.to(dpcpp_device)
        self.assertEqual(
            torch.mul(batch1, 2),
            torch.mul(batch1_dpcpp, 2).to(cpu_device),
        )

    def test_mul_4(self, dtype=torch.float):
        batch1 = torch.randn((4096), device=cpu_device)
        batch1_dpcpp = batch1.to(dpcpp_device)
        self.assertEqual(
            torch.mul(batch1, 2),
            torch.mul(batch1_dpcpp, 2).to(cpu_device),
        )

    def test_mul_5(self, dtype=torch.float):
        batch1 = torch.randn((30522), device=cpu_device)
        batch1_dpcpp = batch1.to(dpcpp_device)
        self.assertEqual(
            torch.mul(batch1, 2),
            torch.mul(batch1_dpcpp, 2).to(cpu_device),
        )

    def test_mul_6(self, dtype=torch.float):
        batch1 = torch.randn((2, 1024), device=cpu_device)
        batch1_dpcpp = batch1.to(dpcpp_device)
        self.assertEqual(
            torch.mul(batch1, 2),
            torch.mul(batch1_dpcpp, 2).to(cpu_device),
        )

    def test_mul_7(self, dtype=torch.float):
        batch1 = torch.randn((512, 1024), device=cpu_device)
        batch1_dpcpp = batch1.to(dpcpp_device)
        self.assertEqual(
            torch.mul(batch1, 2),
            torch.mul(batch1_dpcpp, 2).to(cpu_device),
        )

    def test_mul_8(self, dtype=torch.float):
        batch1 = torch.randn((1024, 1024), device=cpu_device)
        batch1_dpcpp = batch1.to(dpcpp_device)
        self.assertEqual(
            torch.mul(batch1, 2),
            torch.mul(batch1_dpcpp, 2).to(cpu_device),
        )

    def test_mul_9(self, dtype=torch.float):
        batch1 = torch.randn((1024, 4096), device=cpu_device)
        batch1_dpcpp = batch1.to(dpcpp_device)
        self.assertEqual(
            torch.mul(batch1, 2),
            torch.mul(batch1_dpcpp, 2).to(cpu_device),
        )

    def test_mul_10(self, dtype=torch.float):
        batch1 = torch.randn((4096, 1024), device=cpu_device)
        batch1_dpcpp = batch1.to(dpcpp_device)
        self.assertEqual(
            torch.mul(batch1, 2),
            torch.mul(batch1_dpcpp, 2).to(cpu_device),
        )

    def test_mul_11(self, dtype=torch.float):
        batch1 = torch.randn((30522, 1024), device=cpu_device)
        batch1_dpcpp = batch1.to(dpcpp_device)
        self.assertEqual(
            torch.mul(batch1, 2),
            torch.mul(batch1_dpcpp, 2).to(cpu_device),
        )
