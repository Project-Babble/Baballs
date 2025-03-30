// Copyright 2025, rcelyte
// SPDX-License-Identifier: Apache-2.0

using StereoKit;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Numerics;
using System.Runtime;
using System.Threading.Tasks;
using System.Threading;
using System;
namespace StereoBalls;
using static System.Linq.Enumerable;
using static OpenXR;

#pragma warning disable CS0414
#pragma warning disable CS1998
class CalibrationScene : IScene {
	const ulong MaxPollRate = 1536, MaxLatencyMs = 4000;
	static readonly (long minLatency, long maxLatency, double minSampleVelocity) PoseLatencyProbe = (-20 * Stopwatch.Frequency / 1000, 250 * Stopwatch.Frequency / 1000, Math.PI * .5);
	static readonly (uint width, uint height) InputSize = (256, 256); // TODO: configurable by user at runtime

	struct GCInhibit : IDisposable {
		bool started;
		public GCInhibit(long large, long small) =>
			started = GC.TryStartNoGCRegion(small + large, large, false);
		void IDisposable.Dispose() {
			if(!this.started)
				return;
			if(GCSettings.LatencyMode == GCLatencyMode.NoGCRegion)
				GC.EndNoGCRegion();
			else
				Console.WriteLine("WARNING: NoGCRegion revoked prematurely");
		}
		public static implicit operator bool(GCInhibit inhibit) => inhibit.started;
	}

	struct MultiRingBuffer<T> {
		public readonly Lock mutex;
		public ulong head;
		uint length, stride;
		T[] ring;
		public MultiRingBuffer(byte bits, uint stride) {
			if(bits >= 20)
				throw new ArgumentOutOfRangeException(nameof(bits));
			this.mutex = new();
			this.length = 1u << bits;
			this.stride = stride;
			this.ring = new T[this.length * this.stride];
		}
		Span<T> Get(ulong i) => new(this.ring, (int)(i & (this.length  - 1)) * (int)this.stride, (int)this.stride);
		public void Push(ReadOnlySpan<T> value) {
			lock(this.mutex) {
				value.CopyTo(this.Get(head++));
			}
		}
		public ReadOnlySpan<T> this[ulong i] => this.Get(i);
		public static implicit operator Lock(MultiRingBuffer<T> buffer) => buffer.mutex;
	}

	public static bool CanRun => (Backend.XRType == BackendXRType.OpenXR && Backend.OpenXR.Session != 0);

	static Mesh ColoredCircle(Vec3 origin, float diameter, byte subdivs, Color color) {
		uint stride = 1u << subdivs;
		Vertex[] vertices = new Vertex[stride * 3];
		uint[] indices = new uint[(vertices.Length - 2) * 3];
		{
			float i = 0, count = vertices.Length, radius = diameter / 2;
			foreach(ref Vertex vertex in vertices.AsSpan()) {
				(float x, float y) = MathF.SinCos((i++ / count) * MathF.Tau);
				vertex = new Vertex(origin + new Vec3(-x * radius, y * radius, 0), new(0, 0, 1), default, color);
			}
		}
		{
			uint i = 0;
			foreach(ref uint index in indices.AsSpan()) {
				uint face = i / 3;
				uint vertex = i - face * 3;
				int rank = 32 - BitOperations.LeadingZeroCount((face + 2) / 3);
				uint rankStart = (rank == 0) ? 0 : (1u << (rank - 1)) * 3 - 2;
				uint value = ((face - rankStart) * 2 + vertex) * (stride >> rank);
				index = value * Convert.ToUInt32(value != vertices.Length);
				++i;
			}
		}
		Mesh mesh = new();
		ThreadSafe.SetData(mesh, vertices, indices, false);
		mesh.Bounds = new Bounds(origin, new(diameter, diameter, 0));
		return mesh;
	}

	// some mechanism for deducing the point in time a video frame was captured
	// factories may return this delegate asynchronously if they utilize a setup process
	delegate Task<ulong> FrameTimingDelegate(ReadOnlySpan<byte> frame, (uint width, uint height) frame_size, ulong lateEstimate);

	// arbitrary hardcoded latency
	FrameTimingDelegate FrameTimestampNaive(ulong latency) =>
		(ReadOnlySpan<byte> frame, (uint width, uint height) frame_size, ulong lateEstimate) =>
			Task.FromResult(lateEstimate - latency);

	// presents sharp black->white transitions and monitors image brightness (TODO: may need to override auto-exposure mode during this)
	// average latency (OpenXR present timing info) is passed to `FrameTimestampNaive()`, whose result is then quantized to configured video framerate (hides jitter but NOT drift)
	// TODO: unsynchronized cameras (multiple `VideoSource`s) require separate `FrameTimingDelegate`s, but run setup simultaneously; need a more consistent abstraction for this
	Task<FrameTimingDelegate[]> FrameTimestampRoundtrip(ReadOnlySpan<(VideoSource source, VideoSource.CropRect crop)> video) =>
		Task.FromException<FrameTimingDelegate[]>(new NotImplementedException("TODO: display roundtrip frame timing"));

	// optical flow cross-referenced against gaze samples to determine which pixels depict pupil velocity, which can then be correlated with head motion
	// gathers timing samples continuously, interpolating higher confidence samples to fill in gaps
	FrameTimingDelegate FrameTimestampOpticalFlow() =>
		throw new NotImplementedException("TODO: optical flow frame timing");

	public struct InputDesc {
		public VideoSource source;
		public VideoSource.CropRect clip;
		public Matrix3x2 transform;
		public InputDesc(VideoSource source, VideoSource.CropRect? clip = null, Matrix3x2? transform = null) {
			this.source = source;
			this.clip = clip ?? VideoSource.CropRect.Full(source.currentMode);
			this.transform = transform ?? Matrix3x2.Identity;
		}
	}

	// > 1x mono camera -> 1x gaze -> 1x single eye model
	// > 2x mono camera -> 2x gaze -> 1x mirrored model
	// > 2x mono camera -> 2x gaze -> 2x single eye model
	// > 1x stereo camera -> 2x gaze -> 1x mirrored model
	// > 1x stereo camera -> 2x gaze -> 2x single eye model
	readonly InputDesc[] inputs;
	readonly byte interpBits = 0;
	MultiRingBuffer<Vector3> gazeSamples;
	readonly ConcurrentQueue<byte[]> framePool = new();
	public CalibrationScene(ReadOnlySpan<InputDesc> inputs, bool sharedModel) {
		this.inputs = inputs.ToArray();
		do { // TODO: mathematical equivalent without loop
			++this.interpBits;
		} while((ulong)Stopwatch.Frequency >> this.interpBits >= MaxPollRate);
		gazeSamples = new((byte)(64 - BitOperations.LeadingZeroCount(((ulong)Stopwatch.Frequency >> this.interpBits) * MaxLatencyMs / 1000)), (uint)inputs.Length);
		// TODO: source crop to prevent spillover when using transformed stereo frames

		// cameraCount = inputs.Select(input => input.source).ToHashSet().Count;
		// gazeCount = inputs.Length;
		// modelCount = sharedModel ? 1 : gazeCount;
		foreach(InputDesc input in this.inputs)
			new VideoSource(input.source);
	}

	public void Dispose() {
		foreach(InputDesc input in this.inputs)
			input.source.Dispose();
	}

	public async Task Run(CancellationToken cancellationToken) {
		if(!XrTime.IsTimingAvailable)
			throw new Exception("Precise timing not available");
		new XrTime(Stopwatch.GetTimestamp()); // fail early if runtime isn't loaded or doesn't support time conversion
		ulong/*XrSession*/ session = Backend.OpenXR.Session;
		if(session == 0)
			throw new Exception("OpenXR session not available");
		ulong/*XrSpace*/ space = Backend.OpenXR.Space;

		using CancellationTokenSource cancellationSource = new();
		using var _ = cancellationToken.Register(() => cancellationSource.Cancel());
		cancellationToken = cancellationSource.Token;

		Task? poseThread = null;
		Action? dispose = null;
		try {
			// TODO: prev scene fade out animation
			// TODO: dot entry animation to draw attention to it
			// TODO: floating instructions text below dot (styled like Portal 2 captions)
			// TODO: heatmap of collected samples
			Vector3 focusPoint = new(0, 1.5f, -4096f);
			StereoBalls.SetScene(world: Model.FromMesh(ColoredCircle(focusPoint, 128f, 3, new(1, 0, 0, 1)), Material.Unlit), farPlane: 8192f,
					windowSize: new(.3f + UI.Settings.gutter * 4, (.1f + .05f) + UI.Settings.gutter * 3), onWindow: () => {
				// disregard head dwell interaction setting for this UI; dwell is *always* enabled for the large panel and *always* disabled for the small window

				// TODO: big square icon buttons; window stays mostly transparent unless hovered over by controllers
				// TODO: very large copy of this UI behind the user for head dwell interaction
				if(UI.Button("🡸\nPrevious Step", new(.1f, .1f))) { // TODO: model
					// TODO: go back one step
				}
				UI.SameLine();
				UI.LayoutReserve(new(.1f, .1f));
				UI.SameLine();
				if(UI.Button("⟳\nRestart", new(.1f, .1f))) { // TODO: model
					// TODO: reset progress
				}
				UI.LayoutReserve(new(.1f, .05f));
				UI.SameLine();
				if(UI.Button("✖\nCancel", new(.1f, .05f))) // TODO: model
					cancellationSource.Cancel();
			});

			ReadOnlySpan<byte> Decode(ReadOnlySpan<byte> data, VideoSource.Format codec) =>
				throw new NotImplementedException("TODO: decode image");
			bool Resample(ReadOnlySpan<byte> data, VideoSource.Format format, (uint width, uint height) data_size, VideoSource.CropRect clip, Matrix3x2 transform, Span<byte> frame_out, (uint width, uint height) frame_size) =>
				throw new NotImplementedException("TODO: resample image");
			void SubmitDataPair(byte[] frame, (uint width, uint height) frame_size, Vector3 gaze) =>
				throw new NotImplementedException("TODO: send data!");

			FrameTimingDelegate getTimestamp = FrameTimestampNaive(0);
			async void ProcessFrame(int gazeIndex, byte[] frame, (uint width, uint height) frame_size, ulong lateEstimate) {
				try {
					Console.WriteLine("frame {0}", lateEstimate);
					ulong timestamp = await getTimestamp(frame, frame_size, lateEstimate);
					Vector3 from, to;
					ulong sample = timestamp >> interpBits;
					lock(gazeSamples.mutex) {
						from = gazeSamples[sample][gazeIndex];
						to = gazeSamples[sample + 1][gazeIndex];
					}
					Vector3 gaze = Vector3.Lerp(from, to, (timestamp & ((1LU << interpBits) - 1)) / (float)(1 << interpBits));
					SubmitDataPair(frame, frame_size, gaze);
				} finally {
					framePool.Enqueue(frame);
				}
			}
			ulong startTime = ulong.MaxValue;
			foreach(IGrouping<VideoSource, (InputDesc desc, int gazeIndex)> group in inputs.Select((desc, gazeIndex) => (desc, gazeIndex)).GroupBy(input => input.desc.source)) {
				VideoSource source = new(group.Key);
				VideoSource.Mode currentMode;
				Vector3[] gazes = new Vector3[group.Count()];
				void OnMode(VideoSource.Mode mode) => currentMode = mode;
				void OnFrame(ReadOnlySpan<byte> data, ulong lateEstimate) {
					if(lateEstimate < startTime)
						return;
					VideoSource.Mode mode = currentMode;
					if(mode.format is (VideoSource.Format.MJPEG or VideoSource.Format.H264)) {
						data = Decode(data, mode.format);
						mode.format = VideoSource.Format.NV12;
					}
					foreach((InputDesc desc, int gazeIndex) in group) {
						if(!framePool.TryDequeue(out byte[]? frame))
							frame = new byte[InputSize.width * InputSize.height];
						if(Resample(data, mode.format, (mode.size.width, mode.size.height), desc.clip, desc.transform, frame, InputSize))
							ProcessFrame(gazeIndex, frame, InputSize, lateEstimate);
						else
							framePool.Enqueue(frame);
					}
				}
				source.onMode += OnMode;
				OnMode(source.currentMode);
				source.onFrame += OnFrame;
				dispose += () => {
					source.onFrame -= OnFrame;
					source.onMode -= OnMode;
					source.Dispose();
				};
			}

			TaskCompletionSource<ulong> poseInit = new();
			async void PoseTask() {
				XrViewState viewState = new() {type = XrStructureType.ViewState};
				XrViewLocateInfo locateInfo = new() {
					type = XrStructureType.ViewLocateInfo,
					viewConfigurationType = XrViewConfigurationType.PrimaryStereo,
					space = space,
				};
				XrView[] views = new XrView[2];
				Array.Fill<XrView>(views, new() {type = XrStructureType.View});
				uint Locate(long timestamp) {
					locateInfo.displayTime = new(timestamp);
					if(/*XR_FAILED(*/xrLocateViews(session, in locateInfo, ref viewState, (uint)views.Length, out uint views_len, views) < 0)
						throw new Exception("xrLocateViews() failed");
					if(views_len == 0)
						throw new Exception("xrLocateViews() returned zero views");
					return views_len;
				}
				long pollLatency = 0;
				#if false // TODO
				long prevTime = Stopwatch.GetTimestamp();
				Quaternion prevRotation = Quaternion.Identity;
				double GetDelta(long timestamp, long deltaTime = 1) {
					uint views_len = Locate(timestamp);
					Quaternion rotation = views[0].pose.orientation;
					if(views_len >= 2)
						rotation = Quaternion.Slerp(rotation, views[1].pose.orientation, .5f);
					float dot = Quaternion.Dot(prevRotation, rotation);
					prevRotation = rotation;
					return Math.Acos(Math.Clamp(2.0 * dot * dot - 1, -1, 1)) * Stopwatch.Frequency / deltaTime;
				}
				GetDelta(prevTime);
				for(double velocity = 0; velocity < PoseLatencyProbe.minSampleVelocity;) { // wait for high motion
					await Task.Delay(22, cancellationToken);
					long timestamp = Stopwatch.GetTimestamp();
					velocity = GetDelta(timestamp, timestamp - prevTime);
					prevTime = timestamp;
					Console.WriteLine("{0}", (int)(velocity * 180 / Math.PI));
				}
				prevTime = Stopwatch.GetTimestamp();
				long probeTime = prevTime - PoseLatencyProbe.minLatency;
				GetDelta(probeTime);
				List<(long, double)> sampleBuffer = new((int)((PoseLatencyProbe.maxLatency - PoseLatencyProbe.minLatency) * 1000 / Stopwatch.Frequency));
				for(long endTime = probeTime + (PoseLatencyProbe.maxLatency - PoseLatencyProbe.minLatency); prevTime < endTime;) { // probe for latency window with lowest prediction
					new System.Threading.ManualResetEvent(false).WaitOne(1);
					cancellationToken.ThrowIfCancellationRequested();
					long timestamp = Stopwatch.GetTimestamp();
					sampleBuffer.Add((timestamp - probeTime, GetDelta(probeTime, timestamp - prevTime)));
					prevTime = timestamp;
				}
				foreach((long latency, double movement) in sampleBuffer)
					Console.WriteLine("[{0}ms] {1}", latency * 1000 / Stopwatch.Frequency, movement);
				#endif
				gazeSamples.head = (ulong)((Stopwatch.GetTimestamp() - pollLatency) >> interpBits);
				poseInit.SetResult(gazeSamples.head << interpBits);
				Span<Vector3> gazes = stackalloc Vector3[inputs.Length];
				while(!cancellationToken.IsCancellationRequested) {
					if((ulong)((Stopwatch.GetTimestamp() - pollLatency) >> interpBits) < gazeSamples.head) {
						Thread.Yield();
						continue;
					}
					uint views_len = Locate((long)(gazeSamples.head << interpBits));
					for(int i = 0; i < gazes.Length; ++i)
						gazes[i] = Vector3.Transform(focusPoint - views[i].pose.position, Quaternion.Conjugate(views[i].pose.orientation));
					gazeSamples.Push(gazes);
				}
				// TODO: record IPD of stablized sample; adjust all future samples at both calibration AND inference time to match
			}
			GC.Collect(GC.MaxGeneration, GCCollectionMode.Forced);
			using GCInhibit gcGuard = new(1024 * 128, 1024 * 1024);
			if(!gcGuard) {
				#if DEBUG
				throw new Exception("Failed to inhibit garbage collector");
				#else
				Console.WriteLine("WARNING: could not inhibit garbage collector; data quality may suffer");
				#endif
			}
			poseThread = Task.Run(PoseTask, cancellationToken);
			startTime = await poseInit.Task;
			Console.WriteLine("startTime = {0}", startTime);
			TaskCompletionSource temp = new(); using(cancellationToken.Register(() => temp.TrySetResult())) await temp.Task; // TODO: guided calibration sequence
		} finally {
			if(!cancellationToken.IsCancellationRequested)
				cancellationSource.Cancel();
			dispose?.Invoke();
			if(poseThread != null) {
				try {
					await poseThread;
				} catch(TaskCanceledException) {}
			}
		}
	}
}
