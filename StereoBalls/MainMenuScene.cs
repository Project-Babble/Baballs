// Copyright 2025, rcelyte
// SPDX-License-Identifier: Apache-2.0

using StereoKit;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using System.Threading;
using System;
namespace StereoBalls;

/* CONCEPT:
   ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
╭──┨ ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓           ~o~o~ StereoBalls ~o~o~ ┃
│◁┃ ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓                                   ┃
│ C┃ ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓ [Start Calibration]        ╭────╮ ┃
│ A┃ ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓                            │ 🎮 │ ┃
│ M┃ ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓                            ╰────╯ ┃
│  ┃ ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓      ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓   controller-free navigation ╯    ┃
│ S┃ ▓▓▓▓▓▓▓▓▓▓▓▓▓▓              ▓▓▓▓▓▓▓▓▓▓▓▓▓                                   ┃
│ E┃ ▓▓▓▓▓▓▓▓▓▓▓▓▓▓      ()      ▓▓▓▓▓▓▓▓▓▓▓▓▓                                   ┃
│ T┃ ▓▓▓▓▓▓▓▓▓▓▓▓▓▓              ▓▓▓▓▓▓▓▓▓▓▓▓▓ ╭─select─profile────────────────╮ ┃
│ T┃ ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓ │ ❱ calibration.onnx            │ ┃
│ I┃ ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓Show▓Camera▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓ │   user 2.onnx                 │ ┃
│ N┃ ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓ │   old.onnx                    │ ┃
│ G┃ ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓ ╰───────────────────────────────╯ ┃
│ S┃ ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓                                   ┃
│ S┃ ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓  ╭─╮ ╭─╮          [Test Tracking] ┃
│◁┃ ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓  │0│ │0│                          ┃
╰──┨ ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓  ╰─╯ ╰─╯                   [Quit] ┃
   ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛

TODO: settings sidebar
TODO: language button
TODO: debug log

*/

class MainMenuScene : IScene {
	struct VideoImage : IDisposable {
		public VideoSource source;
		public VideoSource.Format format;
		public Tex texture;
		public byte[]? staging;
		public Sprite? sprite;
		public bool _show;
		public VideoImage(VideoSource source) {
			this.source = new(source);
			this.format = VideoSource.Format.None;
			this.texture = null!;
			this.staging = Array.Empty<byte>();
		}
		public void Dispose() =>
			this.source.Dispose();
	}

	public static readonly Model cameraIcon = new();
	VideoImage video;
	public MainMenuScene(VideoSource source) => this.video = new(source);

	public void Dispose() {
		this.showVideo = false;
		this.video.Dispose();
	}

	bool showVideo {get => video._show; set {
		if(value == video._show)
			return;
		video._show = value;
		if(value) {
			video.texture = new(TexType.Dynamic, TexFormat.R8);
			video.source.onMode += this.OnVideoMode;
			OnVideoMode(video.source.currentMode);
			video.source.onFrame += this.OnVideoFrame;
			video.sprite = Sprite.FromTex(video.texture, SpriteType.Single);
		} else {
			video.sprite = null;
			video.source.onFrame -= this.OnVideoFrame;
			video.source.onMode -= this.OnVideoMode;
			video.texture = null!;
			video.staging = Array.Empty<byte>();
		}
	}}

	void OnVideoMode(VideoSource.Mode mode) {
		ThreadSafe.SetSize(video.texture, (int)mode.size.width, (int)mode.size.height);
		video.format = mode.format;
		if(mode.format is (VideoSource.Format.None or VideoSource.Format.Bayer or VideoSource.Format.Gray8))
			video.staging = Array.Empty<byte>();
		else if(mode.size.width * mode.size.height >= 1024 * 512)
			video.staging = new byte[mode.size.width * mode.size.height];
		else
			video.staging = null; // stackalloc
		GraphicsUtils.FixupGrayscale(video.texture);
	}

	unsafe void OnVideoFrame(ReadOnlySpan<byte> data, ulong timestamp) {
		int width = video.texture.Width, height = video.texture.Height, msb = Convert.ToInt32(video.format == VideoSource.Format.Gray16LE);
		// long t_start = System.Diagnostics.Stopwatch.GetTimestamp();
		Span<byte> convert = video.staging ?? stackalloc byte[width * height];
		ReadOnlySpan<byte> grayscale = convert;
		switch(video.format) {
			case VideoSource.Format.None: return;
			case VideoSource.Format.Bayer: case VideoSource.Format.Gray8: {
				grayscale = data;
			} break;
			case VideoSource.Format.Gray16LE: case VideoSource.Format.Gray16BE: case VideoSource.Format.YUY2: {
				if(data.Length < convert.Length * 2)
					return;
				for(int i = 0; i < convert.Length; ++i)
					convert[i] = data[i * 2 + msb];
				grayscale = convert;
			} break;
			case VideoSource.Format.BGRx: { // https://en.wikipedia.org/wiki/Grayscale#Converting_color_to_grayscale
				float FromSRGB(byte value) => (value <= 10) ? (float)value / 3294.6f : MathF.Pow(((float)value + 14.025f) / 269.025f, 2.4f);
				byte ToSRGB(float value) => (byte)((value <= 0.0031308f) ? value * 3294.6f : MathF.Pow(value, 1 / 2.4f) * 269.025f - 14.025f);
				if(data.Length < convert.Length * 4)
					return;
				for(int i = 0; i < convert.Length; ++i)
					convert[i] = ToSRGB(0.2126f * FromSRGB(data[i * 4 + 2]) + 0.7152f * FromSRGB(data[i * 4 + 1]) + 0.0722f * FromSRGB(data[i * 4 + 0]));
				grayscale = convert;
			} break;
			case VideoSource.Format.NV12: return; // TODO
			case VideoSource.Format.MJPEG: return; // TODO
			case VideoSource.Format.H264: return; // TODO
		}
		// long t_end = System.Diagnostics.Stopwatch.GetTimestamp();
		if(grayscale.Length < width * height)
			return;
		fixed(byte* frame = grayscale) {
			// TODO: this throttles the framerate due to synchronization limitations within StereoKit; migrate to `video.source.gpuBlitTarget = video.texture.GetNativeSurface();`
			ThreadSafe.SetColors(video.texture, width, height, (IntPtr)frame);
		}
		// long t_upload = System.Diagnostics.Stopwatch.GetTimestamp();
		// Console.WriteLine("[convert={0} upload={1}]", (double)(t_end - t_start) / (double)System.Diagnostics.Stopwatch.Frequency, (double)(t_upload - t_end) / (double)System.Diagnostics.Stopwatch.Frequency);
	}

	async Task IScene.Run(CancellationToken cancellationToken) {
		while(!cancellationToken.IsCancellationRequested) {
			TaskCompletionSource<IScene> nextScene = new();
			// TODO: raycast gazes and draw balls at hit points
			StereoBalls.SetScene(world: SK.System.overlayApp ? null : DemoScene.world, onWindow: () => { // keep home screen minimal when potentially drawing over other apps
				UI.LayoutPushCut(UICut.Left, .4f);
				// TODO: wider video area to contain both eye views
				if(!this.showVideo) {
					// TODO: hover highlight matching StereoKit buttons
					UI.LayoutReserve(new(.4f, .4f));
					// TODO: general helper function for button w/ 3D mesh icon & caption text, also used by settings button and calibration buttons
					Bounds bounds = UI.LayoutLast;
					UI.ButtonBehavior(bounds.center - new Vec3 {XY = bounds.dimensions.XY / -2}, bounds.dimensions.XY,
						"Show Camera", out float fingerOffset, out BtnState buttonState, out BtnState focusState, out int hand);
					Default.MeshCube.Draw(Default.MaterialUI, Matrix.TS(bounds.center, new Vec3(.4f, .4f, fingerOffset)), new Color(1, 1, 1) * fingerOffset); // TODO: use StereoKit button model
					cameraIcon.Draw(Matrix.TS(bounds.center - new Vec3(0, 0, fingerOffset), .05f));
					UI.TextAt("Show Camera", TextAlign.TopCenter, TextFit.None, bounds.center - new Vec3(0, .05f, fingerOffset + .002f), new(Single.Epsilon, 0));
					this.showVideo = ((buttonState & BtnState.JustActive) != 0);
				} else if(this.video.sprite != null) {
					UI.Image(this.video.sprite, new(.4f, .4f)); // segfaults on null arg
					// TODO: 'hide' button, visible only on hover
				} else {
					UI.LayoutReserve(new(.4f, .4f));
				}
				UI.LayoutPop();
				UI.PanelBegin();
				UI.PushTextStyle(TextStyle.FromFont(StereoKit.Default.Font, UI.TextStyle.CharHeight * 1.2f, new(.957f, .761f, .761f)));
				UI.Text("~~ StereoBalls (working title) ~~", ref Unsafe.NullRef<Vec2>(), UIScroll.None, 0, TextAlign.Center, TextFit.Clip);
				UI.PopTextStyle();
				UI.PushTint(new(1.45f, 2.85f, 2.65f));
				UI.PanelEnd();
				UI.PopTint();
				UI.PushEnabled(CalibrationScene.CanRun); // TODO: "Start VR" button
				if(UI.Button("Start Calibration"))
					nextScene.TrySetResult(new CalibrationScene(stackalloc CalibrationScene.InputDesc[1] {new(video.source)}, true));
				UI.PopEnabled();
				if(UI.Button("🎮")) {
					// TODO: toggle head dwell interaction
				}
				UI.PanelBegin();
				UI.Label("Select Profile");
				for(uint i = 0; i < 3; ++i) // TODO: style darker; concave?
					UI.Radio($"file {i}.onnx", i == 0);
				UI.PanelEnd();
				// TODO: drawer along left edge with sideways text reading "△ Camera Settings △"
				// TODO: selector list for multiple saved models
				bool CornerButton(string text, ref float at) {
					Vec2 textSize = Text.Size(text, UI.TextStyle);
					Vec2 buttonSize = new(textSize.x + UI.Settings.padding * 2, textSize.y + UI.Settings.padding * 2);
					at += buttonSize.y + UI.Settings.padding;
					bool pressed = UI.ButtonAt(text, new(buttonSize.x + UI.Settings.padding - StereoBalls.windowSize.x / 2, at, 0), buttonSize);
					return pressed;
				}
				float y = -StereoBalls.windowSize.y;
				UI.PushTint(new(1.4f, 0, 0));
				if(CornerButton("Quit", ref y))
					nextScene.TrySetCanceled();
				UI.PopTint();
				UI.PushEnabled(false); // TODO: inferencing
				if(CornerButton("Test Tracking", ref y))
					nextScene.TrySetResult(new DemoScene());
				UI.PopEnabled();
				// TODO: xeyes displaying model output, including blink
			});
			try {
				using(cancellationToken.Register(() => nextScene.TrySetCanceled())) {
					using IScene scene = await nextScene.Task;
					this.showVideo = false;
					await scene.Run(cancellationToken);
				}
			} catch(Exception ex) {
				if(ex is not TaskCanceledException) {
					Console.WriteLine("Scene task failed: " + ex);
					// TODO: display error in text box below window
				}
			}
			if(!nextScene.Task.IsCompletedSuccessfully)
				break;
		}
	}
}

static partial class GraphicsUtils {
	static unsafe partial class GL {
		delegate IntPtr GetProcAddressDelegate(ReadOnlySpan<byte> name);
		[LibraryImport("GLX")] private static partial IntPtr glXGetProcAddress(ReadOnlySpan<byte> name);
		[LibraryImport("opengl32")] private static partial IntPtr wglGetProcAddress(ReadOnlySpan<byte> name);
		[LibraryImport("EGL")] private static partial IntPtr eglGetProcAddress(ReadOnlySpan<byte> name);

		static delegate*<int, void> glEnable;
		static delegate*<int, uint, void> glBindTexture;
		static delegate*<uint, int, int, int, in uint, void> glClearTexImage;
		static delegate*<int, out int, void> glGetIntegerv;
		static delegate*<int, int, in (int, int, int, int), void> glTexParameteriv;

		static GL() {
			GetProcAddressDelegate getProcAddress = Backend.Graphics switch {
				BackendGraphics.OpenGL_GLX => glXGetProcAddress,
				BackendGraphics.OpenGL_WGL => wglGetProcAddress,
				BackendGraphics.OpenGLES_EGL => eglGetProcAddress,
				_ => throw new Exception("Invalid backend"),
			};
			glEnable = (delegate*<int, void>)getProcAddress("glEnable"u8);
			glBindTexture = (delegate*<int, uint, void>)getProcAddress("glBindTexture"u8);
			glClearTexImage = (delegate*<uint, int, int, int, in uint, void>)getProcAddress("glClearTexImage"u8);
			glGetIntegerv = (delegate*<int, out int, void>)getProcAddress("glGetIntegerv"u8);
			glTexParameteriv = (delegate*<int, int, in (int, int, int, int), void>)getProcAddress("glTexParameteriv"u8);
		}

		public static void FixupGrayscale(Tex texture) => ThreadSafe.SyncCall(() => {
			uint handle = (uint)texture.GetNativeSurface();
			int oldBinding = 0;
			glGetIntegerv(0x8069/*GL_TEXTURE_BINDING_2D*/, out oldBinding);
			glBindTexture(0x0de1/*GL_TEXTURE_2D*/, handle);
			glTexParameteriv(0x0de1/*GL_TEXTURE_2D*/, 0x8e46/*GL_TEXTURE_SWIZZLE_RGBA*/, (0x1903/*GL_RED*/, 0x1903/*GL_RED*/, 0x1903/*GL_RED*/, 1/*GL_ONE*/));
			glClearTexImage(handle, 0, 0x1903/*GL_RED*/, 0x1401/*GL_UNSIGNED_BYTE*/, 0);
			glBindTexture(0x0de1/*GL_TEXTURE_2D*/, (uint)oldBinding);
		});
		public static void EnableDepthClamp() =>
			ThreadSafe.SyncCall(() => glEnable(0x864f/*GL_DEPTH_CLAMP*/));
	}
	public static void FixupGrayscale(Tex texture) {
		switch(Backend.Graphics) {
			case BackendGraphics.OpenGL_GLX: case BackendGraphics.OpenGL_WGL: case BackendGraphics.OpenGLES_EGL: GL.FixupGrayscale(texture); break;
			default: Console.WriteLine($"GraphicsUtils.FixupGrayscale() not implemented for backend {Backend.Graphics}"); break;
		}
	}
	public static void EnableDepthClamp() {
		switch(Backend.Graphics) {
			case BackendGraphics.OpenGL_GLX: case BackendGraphics.OpenGL_WGL: case BackendGraphics.OpenGLES_EGL: GL.EnableDepthClamp(); break;
			default: Console.WriteLine($"GraphicsUtils.EnableDepthClamp() not implemented for backend {Backend.Graphics}"); break;
		}
	}
}
