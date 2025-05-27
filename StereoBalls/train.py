import torch;
from timm.models.efficientnet import tinynet_e;
model = tinynet_e(pretrained=True, in_chans=1, num_classes=2);

# train here

torch.onnx.export(model, torch.randn((1, 1, 106, 106)), "out.onnx", export_params=True, opset_version=11, do_constant_folding=True, input_names=['input'], output_names=['output']);
