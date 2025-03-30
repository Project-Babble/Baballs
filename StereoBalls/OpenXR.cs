// Copyright 2025, rcelyte
// SPDX-License-Identifier: Apache-2.0

using StereoKit;
using System.Diagnostics;
using System.Numerics;
using System.Runtime.InteropServices;
using System;
namespace StereoBalls;

static partial class OpenXR {
	[StructLayout(LayoutKind.Sequential)]
	public struct timespec {
		public long tv_sec, tv_nsec;
	}

	public enum XrStructureType : uint {
		ViewLocateInfo = 6,
		View = 7,
		ViewState = 11,
	}

	public enum XrViewConfigurationType : uint {
		PrimaryStereo = 2,
	}

	[StructLayout(LayoutKind.Sequential)]
	public struct XrTime {
		long time;
		public static bool IsTimingAvailable => Stopwatch.IsHighResolution && (RuntimeInformation.IsOSPlatform(OSPlatform.Windows) || Stopwatch.Frequency == 1000000000);
		public unsafe XrTime(long timestamp) {
			if(instance == 0)
				throw new Exception("OpenXR not available");
			// TODO: make sure `Stopwatch.GetTimestamp()` is using `clock_gettime(CLOCK_MONOTONIC)` on POSIX and `QueryPerformanceCounter()` on Windows
			if(RuntimeInformation.IsOSPlatform(OSPlatform.Windows)) {
				if(xrConvertWin32PerformanceCounterToTimeKHR == null)
					throw new Exception("OpenXR time conversion not available");
				// TODO: is it valid to assume `Stopwatch.Frequency == QueryPerformanceFrequency()` here?
				if(/*XR_FAILED(*/xrConvertWin32PerformanceCounterToTimeKHR(instance, in timestamp, out this) < 0)
					throw new Exception("xrConvertWin32PerformanceCounterToTimeKHR() failed");
			} else {
				if(xrConvertTimespecTimeToTimeKHR == null)
					throw new Exception("OpenXR time conversion not available");
				timespec time = new() {tv_sec = timestamp / 1000000000};
				time.tv_nsec = timestamp - time.tv_sec * 1000000000;
				if(/*XR_FAILED(*/xrConvertTimespecTimeToTimeKHR(instance, in time, out this) < 0)
					throw new Exception("xrConvertTimespecTimeToTimeKHR() failed");
			}
		}
	}

	[StructLayout(LayoutKind.Sequential)]
	public struct XrViewLocateInfo {
		public XrStructureType type;
		public IntPtr next;
		public XrViewConfigurationType viewConfigurationType;
		public XrTime displayTime;
		public ulong/*XrSpace*/ space;
	}

	[StructLayout(LayoutKind.Sequential)]
	public struct XrViewState {
		public XrStructureType type;
		public IntPtr next;
		public ulong/*XrViewStateFlags*/ viewStateFlags;
	}

	[StructLayout(LayoutKind.Sequential)]
	public struct XrQuaternionf {
		public float x, y, z, w;
		public static implicit operator Quaternion(XrQuaternionf vec) => new(vec.x, vec.y, vec.z, vec.w);
	};

	[StructLayout(LayoutKind.Sequential)]
	public struct XrVector3f {
		public float x, y, z;
		public static implicit operator Vector3(XrVector3f vec) => new(vec.x, vec.y, vec.z);
	};

	[StructLayout(LayoutKind.Sequential)]
	public struct XrView {
		public XrStructureType type;
		public IntPtr next;
		public (XrQuaternionf orientation, XrVector3f position) pose;
		public (float angleLeft, float angleRight, float angleUp, float angleDown) fov;
	}

	static unsafe delegate*<ulong/*XrInstance*/, in long, out XrTime, int/*XrResult*/> xrConvertWin32PerformanceCounterToTimeKHR;
	static unsafe delegate*<ulong/*XrInstance*/, in timespec, out XrTime, int/*XrResult*/> xrConvertTimespecTimeToTimeKHR;

	[LibraryImport("StereoKitC"/*, CharSet=CharSet.Ansi, CallingConvention=CallingConvention.Cdecl*/)] // TODO: calling convention?
	public static partial int/*XrResult*/ xrLocateViews(ulong/*XrSession*/ session, in XrViewLocateInfo viewLocateInfo,
		ref XrViewState viewState, uint viewCapacityInput, out uint viewCountOutput, Span<XrView> views);

	static readonly ulong/*XrInstance*/ instance;

	static unsafe OpenXR() {
		if(Backend.XRType != BackendXRType.OpenXR)
			return;
		instance = Backend.OpenXR.Instance;
		if(instance == 0)
			throw new Exception("OpenXR not available");
		if(RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			xrConvertWin32PerformanceCounterToTimeKHR = (delegate*<ulong, in long, out XrTime, int>)Backend.OpenXR.GetFunctionPtr("xrConvertWin32PerformanceCounterToTimeKHR");
		else
			xrConvertTimespecTimeToTimeKHR = (delegate*<ulong, in timespec, out XrTime, int>)Backend.OpenXR.GetFunctionPtr("xrConvertTimespecTimeToTimeKHR");
	}
}
