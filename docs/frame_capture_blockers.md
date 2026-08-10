# Frame capture status

Framebuffer capture is not yet implemented. The swapchain is currently created for presentation and color attachment use, without a public offscreen target or a negotiated `VK_IMAGE_USAGE_TRANSFER_SRC_BIT` path.

A correct implementation requires:

- negotiating transfer-source support for the chosen surface format and swapchain;
- synchronizing a copy after rendering and before presentation;
- copying to host-visible memory while respecting row pitch and BGRA/RGBA swizzles;
- handling color space conversion and PNG encoding;
- defining behavior for minimized, resized, and headless windows.

Adding transfer usage alone is unsafe because `VkSurfaceCapabilitiesKHR::supportedUsageFlags` varies by platform. The capture request must be rejected clearly when transfer-source swapchains are unavailable, and swapchain recreation must also recreate row-pitch-aware readback buffers.

The recommended implementation is an asynchronous capture request queued into `drawFrame`, backed by reusable per-frame readback buffers and a completion queue. A synchronous capture would otherwise wait for the graphics fence and stall rendering.
