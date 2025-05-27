import torch
import timm
import torch.nn as nn

def convert_to_onnx(
	model_path="best_model.pth",
	onnx_path="model.onnx",
	input_size=(1, 1, 128, 128),
	opset_version=11
):
	"""
	Loads a PyTorch model from 'model_path' and exports it to an ONNX file
	at 'onnx_path'. Assumes single-channel 128x128 input by default.
	
	:param model_path: Path to the saved PyTorch model (.pth).
	:param onnx_path:  Output path for the ONNX model (.onnx).
	:param input_size: Dummy input shape (batch_size, channels, height, width).
	:param opset_version: ONNX opset version to use for export.
	"""
	# -------------------------
	# 1. Recreate your model architecture
	#    (Must match how you trained the model)
	# -------------------------
	model = timm.create_model(
		'tinynet_d.in1k',   # Same model architecture
		pretrained=False,   # We just need the skeleton; weights will be loaded
		in_chans=1,
		num_classes=3
	)
	
	# -------------------------
	# 2. Load the saved model weights
	# -------------------------
	state_dict = torch.load(model_path, map_location="cpu")
	model.load_state_dict(state_dict)
	model.eval()  # Set to eval mode for inference
	
	# -------------------------
	# 3. Create a dummy input tensor
	#    to match the model's input size
	# -------------------------
	print(input_size)
	dummy_input = torch.randn(*input_size, device="cpu")
	
	# -------------------------
	# 4. Export to ONNX
	# -------------------------
	torch.onnx.export(
		model,
		dummy_input,
		onnx_path,
		export_params=True,            # Store the trained parameter weights
		opset_version=opset_version,   # ONNX version (commonly 11 or 13)
		do_constant_folding=True,      # Optimize graph
		input_names=['input'],         # Name for the input node
		output_names=['output']        # Name for the output node
	)
	
	print(f"Model exported to ONNX at: {onnx_path}")


if __name__ == "__main__":
	convert_to_onnx(
		model_path="best_model.pth",   # Path to your trained PyTorch model
		onnx_path="./Inference/model_prob.onnx",       # Where to save the ONNX file
		input_size=(1, 1, 256, 256),  # (batch, channels, height, width)
		opset_version=11
	)
