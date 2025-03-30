// Copyright 2025, rcelyte
// SPDX-License-Identifier: Apache-2.0

using StereoKit;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using System.Threading;
using System;
namespace StereoBalls;

// TODO: configure `Saturation = 0` for possibly improved streaming
partial struct VideoSource : IDisposable {
	public enum Control : short {
		Mode, // uint

		Brightness, // unorm
		Contrast, // unorm
		Saturation, // unorm
		Hue, // snorm
		Gamma, // unorm
		Exposure, // Variant<unorm, ExposureMode>
		Gain, // Nullable<uint>
		Sharpness, // unorm

		WhiteBalance, // Nullable<uint>
		HFlip, // bool
		VFlip, // bool
		PowerLine, // Frequency
	}
	public enum Format : uint {
		None,
		Bayer,
		Gray8,
		Gray16LE,
		Gray16BE,
		BGRx,
		YUY2,
		NV12,
		MJPEG,
		H264,
		// non-exhaustive; may add more formats here if necessary
	}
	public enum ExposureMode : short {
		Auto = -1,
		Shutter = -2,
		Aperture = -3,
	}
	public enum Frequency : short {
		_50Hz,
		_60Hz,
	}
	[StructLayout(LayoutKind.Sequential)] public struct WidthHeightTuple {public uint width, height;}
	[StructLayout(LayoutKind.Sequential)] public struct NumDenomTuple {public uint num, denom;}
	[StructLayout(LayoutKind.Sequential)]
	public struct Mode {
		public Format format;
		// public (uint width, uint height) size; // TODO: [StructLayout(LayoutKind.Sequential)]
		public WidthHeightTuple size;
		// public (uint num, uint denom) framerate; // TODO: [StructLayout(LayoutKind.Sequential)]
		public NumDenomTuple framerate;
	}
	public struct CropRect { // not currently used by native API
		public WidthHeightTuple size;
		// public (uint x, uint y) offset;
		public static CropRect Full(Mode mode) => new() {size = mode.size};
	}
	[StructLayout(LayoutKind.Sequential)]
	public struct ControlValue {
		public Control name;
		public short value;

		public static ControlValue Mode(short mode) => new() {name = Control.Mode, value = mode};

		// image settings
		public static ControlValue Brightness(float brightness) => new() {name = Control.Brightness, value = (short)(brightness * Int16.MaxValue)};
		public static ControlValue Contrast(float contrast) => new() {name = Control.Contrast, value = (short)(contrast * Int16.MaxValue)};
		public static ControlValue Saturation(float saturation) => new() {name = Control.Saturation, value = (short)(saturation * Int16.MaxValue)};
		public static ControlValue Hue(float hue) => new() {name = Control.Hue, value = (short)MathF.Round(hue * Int16.MaxValue)};
		public static ControlValue Gamma(float gamma) => new() {name = Control.Contrast, value = (short)(gamma * Int16.MaxValue)};
		public static ControlValue Exposure(float exposure) => new() {name = Control.Exposure, value = (short)(exposure * Int16.MaxValue)};
		public static ControlValue Gain(ushort? gain) => new() {name = Control.Gain, value = (short?)gain ?? -1};
		public static ControlValue Sharpness(float sharpness) => new() {name = Control.Contrast, value = (short)(sharpness * Int16.MaxValue)};

		// camera configuration
		public static ControlValue Exposure(ExposureMode mode) => new() {name = Control.Exposure, value = (short)mode};
		public static ControlValue WhiteBalance(ushort? temperature) => new() {name = Control.WhiteBalance, value = (short?)temperature ?? -1};
		public static ControlValue HFlip(bool flip) => new() {name = Control.HFlip, value = Convert.ToInt16(flip)};
		public static ControlValue VFlip(bool flip) => new() {name = Control.VFlip, value = Convert.ToInt16(flip)};
		public static ControlValue PowerLine(Frequency? frequency) => new() {name = Control.PowerLine, value = (short?)frequency ?? -1};
	}

	[LibraryImport("runtime")] private static unsafe partial Mode VideoSource_currentMode(IntPtr handle);
	[LibraryImport("runtime")] private static unsafe partial void VideoSource_onMode(IntPtr handle, delegate* unmanaged<IntPtr, Mode, void> callback, IntPtr userptr);
	[LibraryImport("runtime")] private static unsafe partial void VideoSource_onFrame(IntPtr handle, delegate* unmanaged<IntPtr, byte*, uint, ulong, void> callback, IntPtr userptr);
	[LibraryImport("runtime")] private static partial void VideoSource_play(IntPtr handle, byte/*bool*/ play);
	[LibraryImport("runtime")] private static partial void VideoSource_ref(IntPtr handle);
	[LibraryImport("runtime")] private static partial byte/*bool*/ VideoSource_unref(IntPtr handle, out IntPtr onMode, out IntPtr onFrame);
	[LibraryImport("runtime")] private static unsafe partial void **VideoSource_onMode_modifyUserptrLocked(IntPtr handle);
	[LibraryImport("runtime")] private static unsafe partial void **VideoSource_onFrame_modifyUserptrLocked(IntPtr handle);
	[LibraryImport("runtime")] private static partial void VideoSource_unlock(IntPtr handle);

	internal IntPtr handle;
	public ReadOnlySpan<byte> path => default; // TODO
	public ReadOnlySpan<byte> name => default; // TODO
	public Mode currentMode => VideoSource_currentMode(this.handle);

	public VideoSource(VideoSource from) {
		VideoSource_ref(from.handle);
		this = from;
	}

	public static unsafe VideoSource? WrapNativeUnsafe(IntPtr handle) {
		if(handle == 0)
			return null;
		[UnmanagedCallersOnly] static void onMode(IntPtr userptr, Mode mode) {
			try {
				if(userptr != 0)
					((Action<Mode>)GCHandle.FromIntPtr(userptr).Target!)(mode);
			} catch(Exception ex) {
				Console.WriteLine("Unhandled exception in VideoSource.onMode(): {0}", ex);
				throw;
			}
		}
		[UnmanagedCallersOnly] static void onFrame(IntPtr userptr, byte* data, uint data_len, ulong timestamp) {
			try {
				if(userptr != 0)
					((Action<ReadOnlySpan<byte>, ulong>)GCHandle.FromIntPtr(userptr).Target!)(new(data, (int)data_len), timestamp);
			} catch(Exception ex) {
				Console.WriteLine("Unhandled exception in VideoSource.onFrame(): {0}", ex);
				throw;
			}
		}
		VideoSource_onMode(handle, &onMode, 0);
		VideoSource_onFrame(handle, &onFrame, 0);
		return new VideoSource {handle = handle};
	}

	bool UpdateCallback(ref IntPtr userptr, System.Delegate? callback, bool add) {
		try {
			GCHandle? pin = (userptr != 0) ? GCHandle.FromIntPtr(userptr) : null;
			userptr = 0;
			pin?.Free();
			System.Delegate? callbacks = (System.Delegate?)pin?.Target;
			if(add)
				callbacks = Delegate.Combine(callbacks, callback);
			else
				callbacks = Delegate.Remove(callbacks, callback);
			if(callbacks != null)
				userptr = GCHandle.ToIntPtr(GCHandle.Alloc(callbacks));
			return callbacks != null;
		} finally {
			VideoSource_unlock(this.handle);
		}
	}

	public unsafe event Action<Mode> onMode {
		add => UpdateCallback(ref Unsafe.AsRef<IntPtr>(VideoSource_onMode_modifyUserptrLocked(this.handle)), value, true);
		remove => UpdateCallback(ref Unsafe.AsRef<IntPtr>(VideoSource_onMode_modifyUserptrLocked(this.handle)), value, false);
	}

	public unsafe event Action<ReadOnlySpan<byte>, ulong> onFrame {
		add => VideoSource_play(this.handle, Convert.ToByte(UpdateCallback(ref Unsafe.AsRef<IntPtr>(VideoSource_onFrame_modifyUserptrLocked(this.handle)), value, true)));
		remove => VideoSource_play(this.handle, Convert.ToByte(UpdateCallback(ref Unsafe.AsRef<IntPtr>(VideoSource_onFrame_modifyUserptrLocked(this.handle)), value, false)));
	}

	public nint gpuBlitTarget {get => 0; set {}} // TODO
	public uint ListModes(Span<Mode> modes_out) => 0; // TODO
	public uint ListControls(Span<Control> controls_out) => 0; // TODO
	public bool QueryControls(Span<ControlValue> controls) => false; // TODO
	public Task Configure(ReadOnlySpan<ControlValue> controls) => Task.FromException(new NotImplementedException("STUB")); // TODO
	public void Dispose() {
		if(VideoSource_unref(this.handle, out IntPtr onMode, out IntPtr onFrame) == 0)
			return;
		this.handle = 0;
		if(onMode != 0)
			GCHandle.FromIntPtr(onMode).Free();
		if(onFrame != 0)
			GCHandle.FromIntPtr(onFrame).Free();
	}
}

partial struct VideoContext : IDisposable {
	/*class NativeWaitHandle : WaitHandle, IDisposable {
		public NativeWaitHandle(IntPtr handle) => SafeWaitHandle = new Microsoft.Win32.SafeHandles.SafeWaitHandle(handle, false);
		new public void Dispose() => SafeWaitHandle.Dispose();
	}*/

	public enum SourceId : ulong {
		None,
	}

	[LibraryImport("runtime")] private static partial IntPtr VideoContext_new(ReadOnlySpan<byte> name, uint name_len, out IntPtr waitHandle);
	[LibraryImport("runtime")] private static partial IntPtr VideoContext_open(IntPtr handle, SourceId id);
	[LibraryImport("runtime")] private static partial IntPtr VideoContext_openPath(IntPtr handle, ReadOnlySpan<byte> path, uint path_len);
	[LibraryImport("runtime")] private static partial void VideoContext_tick(IntPtr handle);
	[LibraryImport("runtime")] private static partial void VideoContext_ref(IntPtr handle);
	[LibraryImport("runtime")] private static partial byte/*bool*/ VideoContext_unref(IntPtr handle);

	IntPtr handle;
	CancellationTokenSource loopCts = new();
	Task loopTask;
	public VideoContext(ReadOnlySpan<byte> name) {
		this.handle = VideoContext_new(name, (uint)name.Length, out IntPtr waitHandle);
		if(this.handle == 0)
			throw new Exception("VideoContext_new() failed");
		IntPtr handle = this.handle;
		CancellationToken cancellationToken = this.loopCts.Token;
		async Task Tick() {
			while(!cancellationToken.IsCancellationRequested) {
				VideoContext_tick((IntPtr)handle!);
				await Task.Delay(1); // TODO: use `waitHandle` with native .NET sync interop
			}
		}
		loopTask = Tick();
	}
	public IEnumerable<SourceId> Enumerate() {yield break;} // TODO
	public VideoSource? Open(SourceId id = SourceId.None) =>
		VideoSource.WrapNativeUnsafe(VideoContext_open(this.handle, id));
	public VideoSource? Open(ReadOnlySpan<byte> path) =>
		VideoSource.WrapNativeUnsafe(VideoContext_openPath(this.handle, path, (uint)path.Length));
	void IDisposable.Dispose() {
		if(this.handle == 0)
			return;
		this.loopCts.Cancel();
		this.loopTask.Wait();
		this.loopCts.Dispose();
		VideoContext_unref(this.handle);
	}
}
