# Frame capture design

LVG-VOL-2 does not expose a framebuffer capture function. See `frame_capture_blockers.md` for the precise platform and synchronization blockers.

The intended API queues a request before a frame, copies the presented color image into a reusable host-visible buffer after the render pass, and publishes completion only after the corresponding fence signals. Encoding should run after the mapped bytes have been swizzled from the negotiated BGRA/RGBA swapchain format.

A synchronous implementation is simpler but stalls the graphics queue while waiting for the capture fence. The future asynchronous design should use one readback buffer per frame in flight, return a request identifier, expose polling/completion results, and recreate buffers when the swapchain extent changes.

Current limitations include platform-dependent transfer-source support, row pitch, minimized windows, color-space conversion, PNG encoding, and headless/offscreen rendering.
