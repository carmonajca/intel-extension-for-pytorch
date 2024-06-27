#!/bin/bash
python -m pip install torch==2.3.0 --index-url https://download.pytorch.org/whl/test/cpu
python -m pip install https://intel-extension-for-pytorch.s3.amazonaws.com/ipex_dev/cpu/intel_extension_for_pytorch-2.3.0%2Bgit2b84b67-cp310-cp310-linux_x86_64.whl
python -m pip install https://intel-extension-for-pytorch.s3.amazonaws.com/ipex_dev/cpu/oneccl_bind_pt-2.3.0%2Bcpu-cp310-cp310-linux_x86_64.whl
python -m pip install cpuid accelerate datasets sentencepiece protobuf==3.20.3 transformers==4.38.1 neural-compressor==2.4.1 transformers_stream_generator tiktoken
python -m pip install deepspeed==0.14.0
