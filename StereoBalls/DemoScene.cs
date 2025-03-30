// Copyright 2025, rcelyte
// SPDX-License-Identifier: Apache-2.0

using StereoKit;
using System.Threading.Tasks;
using System.Threading;
using System;
namespace StereoBalls;

class DemoScene : IScene {
	public static readonly Model world = new();

	public void Dispose() {}

	async Task IScene.Run(CancellationToken cancellationToken) {
		using CancellationTokenSource cancellationSource = new();
		StereoBalls.SetScene(world: world, onWindow: () => {
			if(UI.Button("< Back"))
				cancellationSource.Cancel();
			// TODO: filled Babble Green circle with pulse effect; follow line drops to ground below when traced by gaze
		});
		using(cancellationToken.Register(() => cancellationSource.Cancel())) {
			TaskCompletionSource temp = new();
			using(cancellationSource.Token.Register(() => temp.TrySetResult())) await temp.Task;
		}
	}
}
