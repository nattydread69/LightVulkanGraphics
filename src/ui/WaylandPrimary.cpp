// SPDX-License-Identifier: LGPL-3.0-or-later
//
// Light Vulkan Graphics
// Copyright (C) 2026 Dr. Nathanael John Inkson
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "WaylandPrimary.h"

#if defined(__linux__)

#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace lightGraphics::ui {

namespace {

// ---- the few libwayland-client types and entry points used ---------------------------------
// Layout-compatible with wayland-util.h; declared here so no Wayland header is needed.

struct WlInterface;
struct WlMessage {
	const char* name;
	const char* signature;
	const WlInterface** types;
};
struct WlInterface {
	const char* name;
	int version;
	int methodCount;
	const WlMessage* methods;
	int eventCount;
	const WlMessage* events;
};
struct WlProxy;
struct WlDisplay;
struct WlQueue;

constexpr std::uint32_t kMarshalDestroy = 1;   // WL_MARSHAL_FLAG_DESTROY

using Listener = void (*)();

struct Wl {
	void* lib = nullptr;
	WlProxy* (*marshalFlags)(WlProxy*, std::uint32_t, const WlInterface*, std::uint32_t, std::uint32_t, ...) = nullptr;
	int (*addListener)(WlProxy*, Listener*, void*) = nullptr;
	void (*proxyDestroy)(WlProxy*) = nullptr;
	void (*setQueue)(WlProxy*, WlQueue*) = nullptr;
	void* (*createWrapper)(void*) = nullptr;
	void (*wrapperDestroy)(void*) = nullptr;
	WlQueue* (*createQueue)(WlDisplay*) = nullptr;
	void (*destroyQueue)(WlQueue*) = nullptr;
	int (*prepareReadQueue)(WlDisplay*, WlQueue*) = nullptr;
	int (*readEvents)(WlDisplay*) = nullptr;
	void (*cancelRead)(WlDisplay*) = nullptr;
	int (*dispatchQueuePending)(WlDisplay*, WlQueue*) = nullptr;
	int (*flush)(WlDisplay*) = nullptr;
	int (*getFd)(WlDisplay*) = nullptr;
	int (*roundtripQueue)(WlDisplay*, WlQueue*) = nullptr;
	const WlInterface* registryIface = nullptr;
	const WlInterface* seatIface = nullptr;
	const WlInterface* keyboardIface = nullptr;
	const WlInterface* pointerIface = nullptr;

	bool load() {
		// GLFW's Wayland backend has already dlopen()ed this; asking again returns that instance.
		lib = dlopen("libwayland-client.so.0", RTLD_LAZY | RTLD_LOCAL);
		if (!lib) {
			return false;
		}
		auto sym = [this](auto& out, const char* name) {
			out = reinterpret_cast<std::remove_reference_t<decltype(out)>>(dlsym(lib, name));
			return out != nullptr;
		};
		return sym(marshalFlags, "wl_proxy_marshal_flags") && sym(addListener, "wl_proxy_add_listener") &&
		       sym(proxyDestroy, "wl_proxy_destroy") && sym(setQueue, "wl_proxy_set_queue") &&
		       sym(createWrapper, "wl_proxy_create_wrapper") && sym(wrapperDestroy, "wl_proxy_wrapper_destroy") &&
		       sym(createQueue, "wl_display_create_queue") && sym(destroyQueue, "wl_event_queue_destroy") &&
		       sym(prepareReadQueue, "wl_display_prepare_read_queue") && sym(readEvents, "wl_display_read_events") &&
		       sym(cancelRead, "wl_display_cancel_read") &&
		       sym(dispatchQueuePending, "wl_display_dispatch_queue_pending") && sym(flush, "wl_display_flush") &&
		       sym(getFd, "wl_display_get_fd") && sym(roundtripQueue, "wl_display_roundtrip_queue") &&
		       sym(registryIface, "wl_registry_interface") && sym(seatIface, "wl_seat_interface") &&
		       sym(keyboardIface, "wl_keyboard_interface") && sym(pointerIface, "wl_pointer_interface");
	}
};

// ---- zwp_primary_selection_unstable_v1, written out by hand --------------------------------

struct Protocol {
	WlInterface manager{}, device{}, offer{}, source{};

	const WlInterface* createSourceTypes[1] = { &source };
	const WlInterface* getDeviceTypes[2] = { &device, nullptr };   // [1] = wl_seat, set in init
	const WlInterface* setSelectionTypes[2] = { &source, nullptr };
	const WlInterface* oneNull[1] = { nullptr };
	const WlInterface* twoNull[2] = { nullptr, nullptr };
	const WlInterface* dataOfferTypes[1] = { &offer };
	const WlInterface* selectionTypes[1] = { &offer };

	WlMessage managerRequests[3];
	WlMessage deviceRequests[2], deviceEvents[2];
	WlMessage offerRequests[2], offerEvents[1];
	WlMessage sourceRequests[2], sourceEvents[2];

	explicit Protocol(const WlInterface* seat) {
		getDeviceTypes[1] = seat;
		setSelectionTypes[0] = &source;

		managerRequests[0] = { "create_source", "n", const_cast<const WlInterface**>(createSourceTypes) };
		managerRequests[1] = { "get_device", "no", const_cast<const WlInterface**>(getDeviceTypes) };
		managerRequests[2] = { "destroy", "", nullptr };
		manager = { "zwp_primary_selection_device_manager_v1", 1, 3, managerRequests, 0, nullptr };

		deviceRequests[0] = { "set_selection", "?ou", const_cast<const WlInterface**>(setSelectionTypes) };
		deviceRequests[1] = { "destroy", "", nullptr };
		deviceEvents[0] = { "data_offer", "n", const_cast<const WlInterface**>(dataOfferTypes) };
		deviceEvents[1] = { "selection", "?o", const_cast<const WlInterface**>(selectionTypes) };
		device = { "zwp_primary_selection_device_v1", 1, 2, deviceRequests, 2, deviceEvents };

		offerRequests[0] = { "receive", "sh", const_cast<const WlInterface**>(twoNull) };
		offerRequests[1] = { "destroy", "", nullptr };
		offerEvents[0] = { "offer", "s", const_cast<const WlInterface**>(oneNull) };
		offer = { "zwp_primary_selection_offer_v1", 1, 2, offerRequests, 1, offerEvents };

		sourceRequests[0] = { "offer", "s", const_cast<const WlInterface**>(oneNull) };
		sourceRequests[1] = { "destroy", "", nullptr };
		sourceEvents[0] = { "send", "sh", const_cast<const WlInterface**>(twoNull) };
		sourceEvents[1] = { "cancelled", "", nullptr };
		source = { "zwp_primary_selection_source_v1", 1, 2, sourceRequests, 2, sourceEvents };
	}
};

// Request opcodes.
constexpr std::uint32_t kDisplayGetRegistry = 1;
constexpr std::uint32_t kRegistryBind = 0;
constexpr std::uint32_t kSeatGetPointer = 0, kSeatGetKeyboard = 1;
constexpr std::uint32_t kManagerCreateSource = 0, kManagerGetDevice = 1, kManagerDestroy = 2;
constexpr std::uint32_t kDeviceSetSelection = 0, kDeviceDestroy = 1;
constexpr std::uint32_t kOfferReceive = 0, kOfferDestroy = 1;
constexpr std::uint32_t kSourceOffer = 0, kSourceDestroy = 1;

constexpr std::uint32_t kSeatCapPointer = 1, kSeatCapKeyboard = 2;

bool debugEnabled() {
	static const bool on = std::getenv("LVG_DEBUG_PRIMARY") != nullptr;
	return on;
}

#define LVG_WLP_LOG(...) \
	do { if (debugEnabled()) { std::fprintf(stderr, "[WaylandPrimary] " __VA_ARGS__); std::fputc('\n', stderr); } } while (0)

const char* const kOfferedMimes[] = { "text/plain;charset=utf-8", "UTF8_STRING", "text/plain", "TEXT", "STRING" };

class WaylandPrimaryImpl final : public WaylandPrimary {
public:
	~WaylandPrimaryImpl() override { shutdown(); }

	bool init(void* display) {
		m_dpy = static_cast<WlDisplay*>(display);
		if (!m_dpy || !m_wl.load()) {
			return false;
		}
		m_proto = std::make_unique<Protocol>(m_wl.seatIface);
		if (pipe2(m_wakeFd, O_CLOEXEC | O_NONBLOCK) != 0) {
			return false;
		}
		m_queue = m_wl.createQueue(m_dpy);
		if (!m_queue) {
			return false;
		}
		// Requests made through a wrapper put their new objects on the wrapper's queue.
		m_wrapper = static_cast<WlProxy*>(m_wl.createWrapper(m_dpy));
		if (!m_wrapper) {
			return false;
		}
		m_wl.setQueue(m_wrapper, m_queue);

		m_registry = m_wl.marshalFlags(m_wrapper, kDisplayGetRegistry, m_wl.registryIface, 1, 0, nullptr);
		if (!m_registry) {
			return false;
		}
		static Listener registryListener[2] = { reinterpret_cast<Listener>(&onRegistryGlobal),
		                                        reinterpret_cast<Listener>(&onRegistryGlobalRemove) };
		m_wl.addListener(m_registry, registryListener, this);
		if (m_wl.roundtripQueue(m_dpy, m_queue) < 0) {
			return false;
		}
		if (!m_seat || !m_manager) {
			LVG_WLP_LOG("unavailable: seat=%d primary-selection manager=%d", m_seat != nullptr, m_manager != nullptr);
			return false;
		}

		m_device = m_wl.marshalFlags(m_manager, kManagerGetDevice, &m_proto->device, 1, 0, nullptr, m_seat);
		if (!m_device) {
			return false;
		}
		static Listener deviceListener[2] = { reinterpret_cast<Listener>(&onDataOffer),
		                                      reinterpret_cast<Listener>(&onSelection) };
		m_wl.addListener(m_device, deviceListener, this);
		// Seat capabilities arrive on the next round trip; the handler creates keyboard/pointer.
		if (m_wl.roundtripQueue(m_dpy, m_queue) < 0) {
			return false;
		}
		LVG_WLP_LOG("ready (keyboard=%d pointer=%d)", m_keyboard != nullptr, m_pointer != nullptr);

		m_alive = true;
		m_thread = std::thread([this]() { run(); });
		return true;
	}

	void shutdown() override {
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (!m_alive && !m_thread.joinable()) {
				releaseObjects();
				return;
			}
			m_quit = true;
		}
		wake();
		if (m_thread.joinable()) {
			m_thread.join();
		}
		std::lock_guard<std::mutex> lock(m_mutex);
		m_alive = false;
		releaseObjects();
	}

	void setText(std::string_view text) override {
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (!m_alive) {
				return;
			}
			m_pendingText.assign(text);
			m_hasPending = true;
		}
		wake();
	}

	std::string getText() override {
		std::unique_lock<std::mutex> lock(m_mutex);
		if (!m_alive) {
			return {};
		}
		if (m_source) {
			return m_ownedText;
		}
		if (!m_current) {
			LVG_WLP_LOG("getText: no selection offered to us (window unfocused, or nothing selected)");
			return {};
		}
		const char* mime = nullptr;
		for (const char* wanted : kOfferedMimes) {
			if (std::find(m_current->mimes.begin(), m_current->mimes.end(), wanted) != m_current->mimes.end()) {
				mime = wanted;
				break;
			}
		}
		if (!mime) {
			LVG_WLP_LOG("getText: offer has no text mime type");
			return {};
		}
		int fds[2];
		if (pipe2(fds, O_CLOEXEC) != 0) {
			return {};
		}
		m_wl.marshalFlags(m_current->proxy, kOfferReceive, nullptr, 1, 0, mime, fds[1]);
		m_wl.flush(m_dpy);
		close(fds[1]);
		lock.unlock();

		std::string out;
		constexpr std::size_t kMaxBytes = std::size_t(64) << 20;
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
		char buf[16384];
		for (;;) {
			const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
				deadline - std::chrono::steady_clock::now()).count();
			if (left <= 0) {
				LVG_WLP_LOG("getText: timed out after %zu bytes", out.size());
				break;
			}
			pollfd p{ fds[0], POLLIN, 0 };
			if (poll(&p, 1, static_cast<int>(left)) <= 0) {
				continue;
			}
			const ssize_t n = read(fds[0], buf, sizeof buf);
			if (n <= 0) {
				break;   // EOF: the owner finished
			}
			out.append(buf, static_cast<std::size_t>(n));
			if (out.size() >= kMaxBytes) {
				break;
			}
		}
		close(fds[0]);
		LVG_WLP_LOG("getText: read %zu bytes via %s", out.size(), mime);
		return out;
	}

private:
	struct OfferInfo {
		WaylandPrimaryImpl* self = nullptr;
		WlProxy* proxy = nullptr;
		std::vector<std::string> mimes;
	};

	// ---- worker thread ----

	void wake() {
		const char b = 1;
		if (m_wakeFd[1] >= 0) {
			[[maybe_unused]] const ssize_t n = write(m_wakeFd[1], &b, 1);
		}
	}

	void run() {
		// Writing to a pipe whose reader went away must fail with EPIPE, not kill the process.
		sigset_t set;
		sigemptyset(&set);
		sigaddset(&set, SIGPIPE);
		pthread_sigmask(SIG_BLOCK, &set, nullptr);

		const int dfd = m_wl.getFd(m_dpy);
		for (;;) {
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				if (m_quit) {
					return;
				}
			}
			while (m_wl.prepareReadQueue(m_dpy, m_queue) != 0) {
				m_wl.dispatchQueuePending(m_dpy, m_queue);
			}
			m_wl.flush(m_dpy);
			pollfd fds[2] = { { dfd, POLLIN, 0 }, { m_wakeFd[0], POLLIN, 0 } };
			// A timeout, not an infinite wait: another thread (GLFW's) may read the socket and
			// leave events queued for us without anything to wake this poll.
			const int r = poll(fds, 2, 50);
			if (r > 0 && (fds[0].revents & POLLIN)) {
				if (m_wl.readEvents(m_dpy) < 0) {
					return;
				}
			} else {
				m_wl.cancelRead(m_dpy);
				if (r > 0 && (fds[0].revents & (POLLERR | POLLHUP))) {
					LVG_WLP_LOG("display connection lost");
					return;
				}
			}
			m_wl.dispatchQueuePending(m_dpy, m_queue);
			if (r > 0 && (fds[1].revents & POLLIN)) {
				char drain[64];
				while (read(m_wakeFd[0], drain, sizeof drain) > 0) {}
			}
			applyPending();
		}
	}

	void applyPending() {
		std::string text;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (!m_hasPending || !m_manager) {
				return;
			}
			text = std::move(m_pendingText);
			m_hasPending = false;
		}
		std::lock_guard<std::mutex> lock(m_mutex);
		WlProxy* old = m_source;
		WlProxy* fresh = nullptr;
		if (!text.empty()) {
			fresh = m_wl.marshalFlags(m_manager, kManagerCreateSource, &m_proto->source, 1, 0, nullptr);
			if (!fresh) {
				return;
			}
			static Listener sourceListener[2] = { reinterpret_cast<Listener>(&onSourceSend),
			                                      reinterpret_cast<Listener>(&onSourceCancelled) };
			m_wl.addListener(fresh, sourceListener, this);
			for (const char* mime : kOfferedMimes) {
				m_wl.marshalFlags(fresh, kSourceOffer, nullptr, 1, 0, mime);
			}
		}
		const std::uint32_t serial = m_serial.load();
		m_wl.marshalFlags(m_device, kDeviceSetSelection, nullptr, 1, 0, fresh, serial);
		m_source = fresh;
		m_ownedText = std::move(text);
		if (old) {
			m_wl.marshalFlags(old, kSourceDestroy, nullptr, 1, kMarshalDestroy);
		}
		m_wl.flush(m_dpy);
		LVG_WLP_LOG("setText: %zu bytes, serial %u", m_ownedText.size(), serial);
	}

	void destroyOffer(OfferInfo* info) {
		m_wl.marshalFlags(info->proxy, kOfferDestroy, nullptr, 1, kMarshalDestroy);
	}

	// Called with the worker or init thread, never concurrently with shutdown()'s teardown.
	void releaseObjects() {
		if (m_released) {
			return;
		}
		m_released = true;
		if (m_source) {
			m_wl.marshalFlags(m_source, kSourceDestroy, nullptr, 1, kMarshalDestroy);
			m_source = nullptr;
		}
		for (auto& info : m_offers) {
			destroyOffer(info.get());
		}
		m_offers.clear();
		m_current = nullptr;
		if (m_device) {
			m_wl.marshalFlags(m_device, kDeviceDestroy, nullptr, 1, kMarshalDestroy);
			m_device = nullptr;
		}
		if (m_manager) {
			m_wl.marshalFlags(m_manager, kManagerDestroy, nullptr, 1, kMarshalDestroy);
			m_manager = nullptr;
		}
		// wl_keyboard/wl_pointer/wl_seat bound at version 1 have no destructor request.
		for (WlProxy** p : { &m_keyboard, &m_pointer, &m_seat, &m_registry }) {
			if (*p) {
				m_wl.proxyDestroy(*p);
				*p = nullptr;
			}
		}
		if (m_dpy) {
			m_wl.flush(m_dpy);
		}
		// The wrapper is attached to the queue, so it goes first.
		if (m_wrapper) {
			m_wl.wrapperDestroy(m_wrapper);
			m_wrapper = nullptr;
		}
		if (m_queue) {
			m_wl.destroyQueue(m_queue);
			m_queue = nullptr;
		}
		for (int& fd : m_wakeFd) {
			if (fd >= 0) {
				close(fd);
				fd = -1;
			}
		}
		m_dpy = nullptr;
	}

	// ---- protocol callbacks (all run on the thread dispatching m_queue) ----

	static WaylandPrimaryImpl* self(void* data) { return static_cast<WaylandPrimaryImpl*>(data); }

	static void onRegistryGlobal(void* data, WlProxy* registry, std::uint32_t name, const char* iface,
	                             std::uint32_t version) {
		WaylandPrimaryImpl* s = self(data);
		if (std::strcmp(iface, "wl_seat") == 0 && !s->m_seat) {
			s->m_seat = s->m_wl.marshalFlags(registry, kRegistryBind, s->m_wl.seatIface, 1, 0, name, s->m_wl.seatIface->name,
			                                 1u, nullptr);
			static Listener seatListener[2] = { reinterpret_cast<Listener>(&onSeatCapabilities),
			                                    reinterpret_cast<Listener>(&onSeatName) };
			s->m_wl.addListener(s->m_seat, seatListener, s);
		} else if (std::strcmp(iface, "zwp_primary_selection_device_manager_v1") == 0 && !s->m_manager) {
			s->m_manager = s->m_wl.marshalFlags(registry, kRegistryBind, &s->m_proto->manager, 1, 0, name,
			                                    s->m_proto->manager.name, 1u, nullptr);
		}
		(void)version;
	}
	static void onRegistryGlobalRemove(void*, WlProxy*, std::uint32_t) {}

	static void onSeatCapabilities(void* data, WlProxy* seat, std::uint32_t caps) {
		WaylandPrimaryImpl* s = self(data);
		if ((caps & kSeatCapKeyboard) && !s->m_keyboard) {
			s->m_keyboard = s->m_wl.marshalFlags(seat, kSeatGetKeyboard, s->m_wl.keyboardIface, 1, 0, nullptr);
			static Listener keyboardListener[5] = {
				reinterpret_cast<Listener>(&onKeymap), reinterpret_cast<Listener>(&onKeyboardEnter),
				reinterpret_cast<Listener>(&onKeyboardLeave), reinterpret_cast<Listener>(&onKey),
				reinterpret_cast<Listener>(&onModifiers) };
			s->m_wl.addListener(s->m_keyboard, keyboardListener, s);
		}
		if ((caps & kSeatCapPointer) && !s->m_pointer) {
			s->m_pointer = s->m_wl.marshalFlags(seat, kSeatGetPointer, s->m_wl.pointerIface, 1, 0, nullptr);
			static Listener pointerListener[5] = {
				reinterpret_cast<Listener>(&onPointerEnter), reinterpret_cast<Listener>(&onPointerLeave),
				reinterpret_cast<Listener>(&onPointerMotion), reinterpret_cast<Listener>(&onPointerButton),
				reinterpret_cast<Listener>(&onPointerAxis) };
			s->m_wl.addListener(s->m_pointer, pointerListener, s);
		}
	}
	static void onSeatName(void*, WlProxy*, const char*) {}

	// The compositor validates set_selection against the serial of the input event that caused
	// it, and accepts only the serial of a key or button PRESS: enter/leave/release serials are
	// not valid, so they must not overwrite a good one.
	static void onKeymap(void*, WlProxy*, std::uint32_t, std::int32_t fd, std::uint32_t) { close(fd); }
	static void onKeyboardEnter(void*, WlProxy*, std::uint32_t, WlProxy*, void*) {}
	static void onKeyboardLeave(void*, WlProxy*, std::uint32_t, WlProxy*) {}
	static void onKey(void* data, WlProxy*, std::uint32_t serial, std::uint32_t, std::uint32_t, std::uint32_t state) {
		if (state == 1) { self(data)->m_serial = serial; }
	}
	static void onModifiers(void*, WlProxy*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) {}
	static void onPointerEnter(void*, WlProxy*, std::uint32_t, WlProxy*, std::int32_t, std::int32_t) {}
	static void onPointerLeave(void*, WlProxy*, std::uint32_t, WlProxy*) {}
	static void onPointerMotion(void*, WlProxy*, std::uint32_t, std::int32_t, std::int32_t) {}
	static void onPointerButton(void* data, WlProxy*, std::uint32_t serial, std::uint32_t, std::uint32_t, std::uint32_t state) {
		if (state == 1) { self(data)->m_serial = serial; }
	}
	static void onPointerAxis(void*, WlProxy*, std::uint32_t, std::uint32_t, std::int32_t) {}

	static void onDataOffer(void* data, WlProxy*, WlProxy* offer) {
		WaylandPrimaryImpl* s = self(data);
		auto info = std::make_unique<OfferInfo>();
		info->self = s;
		info->proxy = offer;
		static Listener offerListener[1] = { reinterpret_cast<Listener>(&onOfferMime) };
		s->m_wl.addListener(offer, offerListener, info.get());
		std::lock_guard<std::mutex> lock(s->m_mutex);
		s->m_offers.push_back(std::move(info));
	}

	static void onOfferMime(void* data, WlProxy*, const char* mime) {
		auto* info = static_cast<OfferInfo*>(data);
		std::lock_guard<std::mutex> lock(info->self->m_mutex);
		info->mimes.emplace_back(mime);
	}

	static void onSelection(void* data, WlProxy*, WlProxy* offer) {
		WaylandPrimaryImpl* s = self(data);
		std::lock_guard<std::mutex> lock(s->m_mutex);
		OfferInfo* chosen = nullptr;
		for (auto& info : s->m_offers) {
			if (info->proxy == offer) {
				chosen = info.get();
			}
		}
		// Only the current selection's offer is kept; every older one is released.
		for (auto it = s->m_offers.begin(); it != s->m_offers.end();) {
			if (it->get() != chosen) {
				s->destroyOffer(it->get());
				it = s->m_offers.erase(it);
			} else {
				++it;
			}
		}
		s->m_current = chosen;
		LVG_WLP_LOG("selection event: %s", chosen ? "offer" : "none");
	}

	static void onSourceSend(void* data, WlProxy*, const char*, std::int32_t fd) {
		WaylandPrimaryImpl* s = self(data);
		std::string text;
		{
			std::lock_guard<std::mutex> lock(s->m_mutex);
			text = s->m_ownedText;
		}
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
		std::size_t sent = 0;
		while (sent < text.size()) {
			const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
				deadline - std::chrono::steady_clock::now()).count();
			if (left <= 0) {
				break;
			}
			pollfd p{ fd, POLLOUT, 0 };
			if (poll(&p, 1, static_cast<int>(left)) <= 0) {
				continue;
			}
			const ssize_t n = write(fd, text.data() + sent, text.size() - sent);
			if (n <= 0) {
				break;   // EPIPE: the reader gave up
			}
			sent += static_cast<std::size_t>(n);
		}
		close(fd);
		LVG_WLP_LOG("served %zu of %zu bytes", sent, text.size());
	}

	static void onSourceCancelled(void* data, WlProxy* source) {
		WaylandPrimaryImpl* s = self(data);
		std::lock_guard<std::mutex> lock(s->m_mutex);
		if (s->m_source == source) {
			s->m_source = nullptr;
		}
		s->m_wl.marshalFlags(source, kSourceDestroy, nullptr, 1, kMarshalDestroy);
		LVG_WLP_LOG("our selection was replaced by another program");
	}

	Wl m_wl;
	std::unique_ptr<Protocol> m_proto;
	WlDisplay* m_dpy = nullptr;
	WlQueue* m_queue = nullptr;
	WlProxy* m_wrapper = nullptr;
	WlProxy* m_registry = nullptr;
	WlProxy* m_seat = nullptr;
	WlProxy* m_manager = nullptr;
	WlProxy* m_device = nullptr;
	WlProxy* m_keyboard = nullptr;
	WlProxy* m_pointer = nullptr;
	int m_wakeFd[2] = { -1, -1 };
	std::thread m_thread;
	std::atomic<std::uint32_t> m_serial{ 0 };

	std::mutex m_mutex;   // guards everything below, and the objects used from getText()/applyPending()
	bool m_alive = false, m_quit = false, m_released = false;
	std::string m_pendingText, m_ownedText;
	bool m_hasPending = false;
	WlProxy* m_source = nullptr;   // non-null while we own PRIMARY
	std::vector<std::unique_ptr<OfferInfo>> m_offers;
	OfferInfo* m_current = nullptr;   // the offer for the current selection, if any
};

} // namespace

std::shared_ptr<WaylandPrimary> WaylandPrimary::create(void* wlDisplay) {
	auto impl = std::make_shared<WaylandPrimaryImpl>();
	if (!impl->init(wlDisplay)) {
		impl->shutdown();
		return nullptr;
	}
	return impl;
}

} // namespace lightGraphics::ui

#else  // not Linux

namespace lightGraphics::ui {
std::shared_ptr<WaylandPrimary> WaylandPrimary::create(void*) { return nullptr; }
}

#endif
