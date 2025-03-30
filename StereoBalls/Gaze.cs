// Copyright 2025, rcelyte
// SPDX-License-Identifier: Apache-2.0

using System.Numerics;
using System;

struct GazeVector {
	public enum Format {
		Quaternion, // 2-axis truncated quaternion eye rotation
		Euler, // naïve YX-order Euler rotation, normalized to ±90°
		Planar, // XY coordinate on plane 1 meter ahead of eye
		WeightedPlanar, // arctan of XY coordinate on plane 1 meter ahead of eye
	}
	public static readonly Format format = Format.WeightedPlanar;
	public readonly Vector2 encoded;
	public GazeVector(Vector3 normal) {
		if(normal.Z <= 0)
			throw new ArgumentOutOfRangeException(nameof(normal));
		Vector2 Truncate(Quaternion quat) => new(quat.X, quat.Y);
		this.encoded = format switch {
			// TODO: does this experience precision issues around (0, 0, 1)?
			Format.Quaternion => Truncate(Quaternion.CreateFromAxisAngle(Vector3.Normalize(new(-normal.Y, normal.X, 0)), MathF.Acos(Vector3.Normalize(normal).Z))),
			Format.Euler => throw new NotImplementedException("TODO: encode Format.Euler"),
			Format.Planar => new Vector2(normal.X, normal.Y) / normal.Z,
			Format.WeightedPlanar => new Vector2(MathF.Atan(normal.X / normal.Z), MathF.Atan(normal.Y / normal.Z)),
			_ => throw new ArgumentOutOfRangeException(nameof(format)),
		};
	}
	public Vector3 normal => format switch {
		Format.Quaternion => Vector3.Transform(new(0, 0, 1), new Quaternion(encoded.X, encoded.Y, 0, MathF.Sqrt(1 - encoded.X * encoded.X - encoded.Y * encoded.Y))),
		Format.Euler => throw new NotImplementedException("TODO: decode Format.Euler"),
		Format.Planar => Vector3.Normalize(new(this.encoded, 1)),
		Format.WeightedPlanar => Vector3.Normalize(new(MathF.Tan(this.encoded.X), MathF.Tan(this.encoded.Y), 1)),
		_ => default,
	};
	/*static GazeVector() { // test roundtrip loss of each encoding
		foreach(GazeVector.Format format in stackalloc[] {GazeVector.Format.Quaternion, GazeVector.Format.Euler, GazeVector.Format.Planar, GazeVector.Format.WeightedPlanar}) {
			GazeVector.format = format;
			float maxError = 0;
			for(float y = MathF.PI * -.45f; y < MathF.PI * .45f; y += 1 / 1024f) {
				for(float x = MathF.PI * -.45f; x < MathF.PI * .45f; x += 1 / 1024f) {
					Vector3 normal = Vector3.Normalize(new(MathF.Tan(x), MathF.Tan(y), 1));
					Vector3 restored = new GazeVector(normal).normal;
					maxError = MathF.Max(maxError, Vector3.DistanceSquared(normal, restored));
				}
			}
			Console.WriteLine("{0}: {1}", format, MathF.Sqrt(maxError));
		}
	}*/
}
