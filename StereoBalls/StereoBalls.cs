// Copyright 2025, rcelyte
// SPDX-License-Identifier: Apache-2.0

using StereoKit;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Threading.Tasks;
using System.Threading;
using System;
[assembly: DisableRuntimeMarshalling]
namespace StereoBalls;
using static ThreadSafe;


interface IScene : IDisposable {
	Task Run(CancellationToken cancellationToken);
}

static class StereoBalls {
	static Model? world = null;
	public static Vec2 windowSize {get; private set;}
	static Action? onWindow = null;
	static Pose windowPose = new(0, 0, -.5f, Quat.LookDir(-Vec3.Forward));
	static async Task<byte[]> GetResource(string name, CancellationToken cancellationToken) {
		using Stream? stream = Assembly.GetExecutingAssembly().GetManifestResourceStream(name);
		if(stream == null)
			throw new Exception("Resource not found");
		byte[] data = new byte[stream.Length];
		if(await stream.ReadAsync(data, 0, data.Length, cancellationToken) != data.Length)
			throw new Exception("Read failed");
		return data;
	}
	static async Task Main(string[] args) { // TODO: argument parsing and -h/--help
		SK.Initialize(new SKSettings {
			logFilter = LogLevel.Warning,
			overlayApp = true,
			standbyMode = StandbyMode.None,
		});
		using CancellationTokenSource cancellationSource = new();
		try {
			// TODO: "Failed to initialize VR" UI with relevant error message + retry button
			ThreadSafe.ClaimMainThread();
			using VideoContext videoContext = new("StereoBalls"u8);
			using VideoSource videoSource = videoContext.Open() ?? throw new Exception("videoContext.Open() failed");
			Task assetTask = GetResource("StereoBalls.assets", cancellationSource.Token).ContinueWith(resource => {
				Model? assets = ThreadSafe.Model_FromMemory("assets.glb", resource.Result);
				if(assets == null)
					return;
				foreach(ModelNode node in assets.Nodes) {
					(node.Name switch {
						"camera" => MainMenuScene.cameraIcon,
						_ => DemoScene.world,
					}).AddNode(node.Name, node.ModelTransform, node.Mesh, node.Material);
				}
			}, cancellationSource.Token);
			using IScene scene = (args.Length >= 1 && args[0] == "--quick") ? new CalibrationScene(stackalloc CalibrationScene.InputDesc[1] {new(videoSource)}, true) : new MainMenuScene(videoSource);
			Task sceneTask = scene.Run(cancellationSource.Token); // TODO: catch exceptions in task
			SK.ExecuteOnMain(() => GraphicsUtils.EnableDepthClamp());
			while(ThreadSafe.SK_Step()) {
				if(onWindow != null) {
					UI.WindowBegin("", ref windowPose, windowSize, UIWin.Body);
					onWindow();
					UI.WindowEnd();
				}

				world?.Draw(Matrix.Identity);
				if(sceneTask.IsCompleted)
					SK.Quit();
			}
			cancellationSource.Cancel();
			ThreadSafe.Shutdown();
			await assetTask;
			await sceneTask;
		} catch(Exception ex) {
			// TODO: graphical error reporting where possible
			Console.WriteLine("Unhandled exception. {0}", ex);
		}
		SK.Shutdown();
	}
	public static void SetScene(Model? world = null, float farPlane = 50, Vec2? windowSize = null, Action? onWindow = null) => SK.ExecuteOnMain(() => {
		StereoBalls.world = world;
		StereoBalls.windowSize = windowSize ?? new(.7f, .4f);
		StereoBalls.onWindow = onWindow;
		Renderer.SetClip(farPlane: farPlane);
	});
}
