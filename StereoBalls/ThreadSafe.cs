// Copyright 2025, rcelyte
// SPDX-License-Identifier: Apache-2.0

using StereoKit;
using System.Collections.Concurrent;
using System.Threading.Tasks;
using System.Threading;
using System;
namespace StereoBalls;

static class ThreadSafe {
	static ReaderWriterLockSlim mutex = new(LockRecursionPolicy.NoRecursion);
	static int? mainThreadId = null;
	static ConcurrentQueue<(Action, TaskCompletionSource)> tasks = new();

	static void Flush() {
		while(tasks.TryDequeue(out (Action task, TaskCompletionSource completion) item)) {
			item.task();
			item.completion.SetResult();
		}
	}

	public static void ClaimMainThread() {
		mutex.EnterWriteLock();
		mainThreadId = Thread.CurrentThread.ManagedThreadId;
		mutex.ExitWriteLock();
	}

	public static bool SK_Step() {
		ClaimMainThread();
		bool result = SK.Step();
		Flush();
		return result;
	}

	public static void Shutdown() {
		mutex.EnterWriteLock();
		mainThreadId = null;
		mutex.ExitWriteLock();
		Flush();
	}

	public static void SyncCall(Action task) {
		TaskCompletionSource completion;
		mutex.EnterReadLock();
		try {
			if(mainThreadId == null)
				return;
			if(Thread.CurrentThread.ManagedThreadId == mainThreadId.Value) {
				task();
				return;
			}
			completion = new TaskCompletionSource();
			tasks.Enqueue((task, completion));
		} finally {
			mutex.ExitReadLock();
		}
		completion.Task.Wait();
	}

	public static void SetData(this Mesh mesh, Vertex[] vertices, uint[] indices, bool calculateBounds = true) => SyncCall(() => mesh.SetData(vertices, indices, calculateBounds));
	public static void SetVerts(this Mesh mesh, Vertex[] vertices, bool calculateBounds = true) => SyncCall(() => mesh.SetVerts(vertices, calculateBounds));
	public static void SetInds(this Mesh mesh, uint[] indices) => SyncCall(() => mesh.SetInds(indices));
	public static void SetColors(this Tex texture, int width, int height, IntPtr data) => SyncCall(() => texture.SetColors(width, height, data));
	public static void SetColors(this Tex texture, int width, int height, Color32[] data) => SyncCall(() => texture.SetColors(width, height, data));
	public static void SetColors(this Tex texture, int width, int height, Color[] data) => SyncCall(() => texture.SetColors(width, height, data));
	public static void SetColors(this Tex texture, int width, int height, byte[] data) => SyncCall(() => texture.SetColors(width, height, data));
	public static void SetColors(this Tex texture, int width, int height, ushort[] data) => SyncCall(() => texture.SetColors(width, height, data));
	public static void SetColors(this Tex texture, int width, int height, float[] data) => SyncCall(() => texture.SetColors(width, height, data));
	public static void SetSize(this Tex texture, int width, int height) => SyncCall(() => texture.SetSize(width, height));
	public static void GetColors(this Tex texture, ref Color32[] colorData, int mipLevel = 0) => GetColorData(texture, ref colorData, mipLevel);
	public static Color32[] GetColors(this Tex texture, int mipLevel = 0) => GetColorData<Color32>(texture, mipLevel);
	public static T[] GetColorData<T>(this Tex texture, int mipLevel = 0, int structPerPixel = 1) where T:struct {
		T[] result = null!;
		GetColorData(texture, ref result, mipLevel, structPerPixel);
		return result;
	}
	public static void GetColorData<T>(this Tex texture, ref T[] colorData, int mipLevel = 0, int structPerPixel = 1) where T:struct {
		T[] data = colorData;
		SyncCall(() => texture.GetColorData(ref data, mipLevel, structPerPixel));
		colorData = data;
	}

	public static Shader? Shader_FromMemory(byte[] data) {
		Shader? result = null;
		SyncCall(() => result = Shader.FromMemory(data));
		return result;
	}
	public static Shader? Shader_FromFile(string file) {
		Shader? result = null;
		SyncCall(() => result = Shader.FromFile(file));
		return result;
	}

	public static Model? Model_FromFile(string file, Shader? shader = null) {
		Model? result = null;
		SyncCall(() => result = Model.FromFile(file, shader));
		return result;
	}
	public static Model? Model_FromMemory(string filename, byte[] data, Shader? shader = null) {
		Model? result = null;
		SyncCall(() => result = Model.FromMemory(filename, data, shader));
		return result;
	}
}
