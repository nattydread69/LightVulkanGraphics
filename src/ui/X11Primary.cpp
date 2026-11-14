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

#include "X11Primary.h"

#if defined(__linux__) && __has_include(<X11/Xlib.h>)

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <dlfcn.h>
#include <poll.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <type_traits>
#include <string>
#include <utility>
#include <thread>
#include <vector>

namespace lightGraphics::ui {

namespace {

// The handful of Xlib entry points used, resolved at run time.
struct Xlib {
	void* lib = nullptr;
	decltype(&::XOpenDisplay)          openDisplay = nullptr;
	decltype(&::XCloseDisplay)         closeDisplay = nullptr;
	decltype(&::XCreateSimpleWindow)   createSimpleWindow = nullptr;
	decltype(&::XDestroyWindow)        destroyWindow = nullptr;
	decltype(&::XDefaultRootWindow)    defaultRootWindow = nullptr;
	decltype(&::XInternAtom)           internAtom = nullptr;
	decltype(&::XSetSelectionOwner)    setSelectionOwner = nullptr;
	decltype(&::XGetSelectionOwner)    getSelectionOwner = nullptr;
	decltype(&::XConvertSelection)     convertSelection = nullptr;
	decltype(&::XNextEvent)            nextEvent = nullptr;
	decltype(&::XPending)              pending = nullptr;
	decltype(&::XSendEvent)            sendEvent = nullptr;
	decltype(&::XChangeProperty)       changeProperty = nullptr;
	decltype(&::XGetWindowProperty)    getWindowProperty = nullptr;
	decltype(&::XFree)                 free = nullptr;
	decltype(&::XFlush)                flush = nullptr;
	decltype(&::XConnectionNumber)     connectionNumber = nullptr;
	decltype(&::XSelectInput)          selectInput = nullptr;

	bool load() {
		lib = dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
		if (!lib) {
			return false;
		}
		auto sym = [this](auto& fn, const char* name) {
			fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(lib, name));
			return fn != nullptr;
		};
		return sym(openDisplay, "XOpenDisplay") && sym(closeDisplay, "XCloseDisplay") &&
		       sym(createSimpleWindow, "XCreateSimpleWindow") && sym(destroyWindow, "XDestroyWindow") &&
		       sym(defaultRootWindow, "XDefaultRootWindow") && sym(internAtom, "XInternAtom") &&
		       sym(setSelectionOwner, "XSetSelectionOwner") && sym(getSelectionOwner, "XGetSelectionOwner") &&
		       sym(convertSelection, "XConvertSelection") && sym(nextEvent, "XNextEvent") &&
		       sym(pending, "XPending") && sym(sendEvent, "XSendEvent") && sym(changeProperty, "XChangeProperty") &&
		       sym(getWindowProperty, "XGetWindowProperty") && sym(free, "XFree") && sym(flush, "XFlush") &&
		       sym(connectionNumber, "XConnectionNumber") &&
		       sym(selectInput, "XSelectInput");
	}
};

class X11PrimaryImpl final : public X11Primary {
public:
	~X11PrimaryImpl() override {
		m_quit = true;
		wake();
		if (m_thread.joinable()) {
			m_thread.join();
		}
		if (m_dpy) {
			m_x.destroyWindow(m_dpy, m_win);
			m_x.closeDisplay(m_dpy);
		}
		if (m_wakeFd[0] >= 0) { close(m_wakeFd[0]); }
		if (m_wakeFd[1] >= 0) { close(m_wakeFd[1]); }
		if (m_x.lib) { dlclose(m_x.lib); }
	}

	bool init() {
		if (!m_x.load()) {
			return false;
		}
		m_dpy = m_x.openDisplay(nullptr);
		if (!m_dpy || pipe(m_wakeFd) != 0) {
			return false;
		}
		m_win = m_x.createSimpleWindow(m_dpy, m_x.defaultRootWindow(m_dpy), 0, 0, 1, 1, 0, 0, 0);
		m_x.selectInput(m_dpy, m_win, PropertyChangeMask);   // INCR chunks arrive as PropertyNotify
		m_primary = XA_PRIMARY;
		m_targets = m_x.internAtom(m_dpy, "TARGETS", False);
		m_utf8 = m_x.internAtom(m_dpy, "UTF8_STRING", False);
		m_textAtom = m_x.internAtom(m_dpy, "TEXT", False);
		m_incr = m_x.internAtom(m_dpy, "INCR", False);
		m_property = m_x.internAtom(m_dpy, "LVG_PRIMARY_TRANSFER", False);
		m_thread = std::thread([this]() { run(); });
		return true;
	}

	void setText(std::string_view text) override {
		std::unique_lock<std::mutex> lock(m_mutex);
		m_text.assign(text);
		m_owned = true;
		m_claim = true;
		m_claimed = false;
		lock.unlock();
		wake();
		// Wait (briefly) until the worker has told the X server, so a request that follows at once finds us.
		lock.lock();
		m_cv.wait_for(lock, std::chrono::milliseconds(100), [this]() { return m_claimed; });
	}

	std::string getText() override {
		std::unique_lock<std::mutex> lock(m_mutex);
		if (m_owned) {
			return m_text;
		}
		m_replyReady = false;
		m_reply.clear();
		m_fetch = true;
		lock.unlock();
		wake();
		lock.lock();
		// A plain reply is quick: give up after 150 ms. A large selection arrives in INCR chunks;
		// every chunk (and the start of the transfer) counts as progress and buys another second,
		// up to a hard cap.
		using Clock = std::chrono::steady_clock;
		const auto hardDeadline = Clock::now() + std::chrono::seconds(30);
		auto deadline = Clock::now() + std::chrono::milliseconds(150);
		std::uint64_t seen = m_progress;
		while (!m_replyReady && Clock::now() < std::min(deadline, hardDeadline)) {
			m_cv.wait_for(lock, std::chrono::milliseconds(10), [this]() { return m_replyReady; });
			if (m_progress != seen) {
				seen = m_progress;
				deadline = Clock::now() + std::chrono::milliseconds(1000);
			}
		}
		m_fetch = false;
		if (!m_replyReady) {
			m_incrActive = false;   // abandon a transfer that stalled
			m_incrBuf.clear();
			m_fetchSent = false;
			return {};
		}
		return m_reply;
	}

private:
	void wake() {
		const char b = 1;
		if (m_wakeFd[1] >= 0) {
			[[maybe_unused]] const ssize_t n = write(m_wakeFd[1], &b, 1);
		}
	}

	void run() {
		const int xfd = m_x.connectionNumber(m_dpy);
		while (!m_quit) {
			pollfd fds[2] = { { xfd, POLLIN, 0 }, { m_wakeFd[0], POLLIN, 0 } };
			poll(fds, 2, 500);
			if (fds[1].revents & POLLIN) {
				char buf[64];
				[[maybe_unused]] const ssize_t n = read(m_wakeFd[0], buf, sizeof buf);
			}
			handleCommands();
			pruneTransfers();
			while (m_x.pending(m_dpy) > 0) {
				XEvent ev;
				m_x.nextEvent(m_dpy, &ev);
				handleEvent(ev);
			}
		}
	}

	void handleCommands() {
		bool claim = false, fetch = false;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			claim = std::exchange(m_claim, false);
			fetch = m_fetch && !m_fetchSent;
			if (fetch) { m_fetchSent = true; }
		}
		if (claim) {
			m_x.setSelectionOwner(m_dpy, m_primary, m_win, CurrentTime);
			m_x.flush(m_dpy);
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_claimed = true;
			}
			m_cv.notify_all();
		}
		if (fetch) {
			if (m_x.getSelectionOwner(m_dpy, m_primary) == None) {
				finishFetch({});
			} else {
				m_x.convertSelection(m_dpy, m_primary, m_utf8, m_property, m_win, CurrentTime);
				m_x.flush(m_dpy);
			}
		}
	}

	void finishFetch(std::string text) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_reply = std::move(text);
		m_replyReady = true;
		m_fetchSent = false;
		m_cv.notify_all();
	}

	void handleEvent(const XEvent& ev) {
		if (ev.type == SelectionClear) {
			std::lock_guard<std::mutex> lock(m_mutex);
			m_owned = false;   // another program took PRIMARY; keep the text only for the next claim
		} else if (ev.type == SelectionRequest) {
			serve(ev.xselectionrequest);
		} else if (ev.type == SelectionNotify) {
			if (ev.xselection.property == None) {
				finishFetch({});
				return;
			}
			Atom type = None;
			int format = 0;
			unsigned long count = 0, after = 0;
			unsigned char* data = nullptr;
			std::string text;
			bool incrStarted = false;
			if (m_x.getWindowProperty(m_dpy, m_win, m_property, 0, 1 << 20, True, AnyPropertyType, &type, &format,
			                          &count, &after, &data) == Success && data) {
				if (type == m_incr) {
					incrStarted = true;   // reading (and deleting) the property is what asks for chunk 1
				} else if (format == 8) {
					text.assign(reinterpret_cast<char*>(data), count);
				}
				m_x.free(data);
			}
			if (incrStarted) {
				std::lock_guard<std::mutex> lock(m_mutex);
				m_incrActive = true;
				m_incrBuf.clear();
				++m_progress;
				return;
			}
			finishFetch(std::move(text));
		} else if (ev.type == PropertyNotify) {
			const XPropertyEvent& pe = ev.xproperty;
			if (pe.window == m_win && pe.atom == m_property && pe.state == PropertyNewValue) {
				receiveIncrChunk();
			} else if (pe.state == PropertyDelete) {
				sendIncrChunk(pe.window, pe.atom);   // the requestor consumed our last chunk
			}
		}
	}

	// ---- INCR, receiving: the owner sends a large selection as a series of properties ----

	void receiveIncrChunk() {
		// The owner writes the property (raising a PropertyNotify here) BEFORE it sends the
		// SelectionNotify that handleEvent() answers, so outside an INCR transfer this must
		// leave the property alone for that handler to read.
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (!m_incrActive) {
				return;
			}
		}
		Atom type = None;
		int format = 0;
		unsigned long count = 0, after = 0;
		unsigned char* data = nullptr;
		std::string chunk;
		const bool ok = m_x.getWindowProperty(m_dpy, m_win, m_property, 0, 1 << 20, True, AnyPropertyType, &type,
		                                      &format, &count, &after, &data) == Success && type != None;
		if (data) {
			if (ok && format == 8) {
				chunk.assign(reinterpret_cast<char*>(data), count);
			}
			m_x.free(data);
		}
		if (!ok) {
			return;
		}
		std::string whole;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (!m_incrActive) {
				return;   // the transfer was abandoned while we were reading this chunk
			}
			if (!chunk.empty() && m_incrBuf.size() + chunk.size() <= kMaxIncrBytes) {
				m_incrBuf += chunk;
				++m_progress;
				return;
			}
			// An empty chunk ends the transfer (so does exceeding the size cap, keeping what we have).
			whole = std::move(m_incrBuf);
			m_incrBuf.clear();
			m_incrActive = false;
		}
		finishFetch(std::move(whole));
	}

	// ---- INCR, sending: serve a large selection in chunks, one per delete of the property ----

	struct Transfer {
		Window requestor;
		Atom property;
		Atom type;
		std::string data;
		std::size_t offset;
		std::chrono::steady_clock::time_point last;
	};

	void sendIncrChunk(Window window, Atom property) {
		for (auto it = m_transfers.begin(); it != m_transfers.end(); ++it) {
			if (it->requestor != window || it->property != property) {
				continue;
			}
			const std::size_t n = std::min(kIncrChunk, it->data.size() - it->offset);
			m_x.changeProperty(m_dpy, window, property, it->type, 8, PropModeReplace,
			                   reinterpret_cast<const unsigned char*>(it->data.data() + it->offset), static_cast<int>(n));
			it->offset += n;
			it->last = std::chrono::steady_clock::now();
			if (n == 0) {   // the empty chunk tells the requestor it has everything
				m_x.selectInput(m_dpy, window, NoEventMask);
				m_transfers.erase(it);
			}
			m_x.flush(m_dpy);
			return;
		}
	}

	void pruneTransfers() {
		const auto cutoff = std::chrono::steady_clock::now() - std::chrono::seconds(10);
		for (auto it = m_transfers.begin(); it != m_transfers.end();) {
			if (it->last < cutoff) {
				m_x.selectInput(m_dpy, it->requestor, NoEventMask);
				it = m_transfers.erase(it);
			} else {
				++it;
			}
		}
	}

	void serve(const XSelectionRequestEvent& req) {
		XSelectionEvent reply{};
		reply.type = SelectionNotify;
		reply.requestor = req.requestor;
		reply.selection = req.selection;
		reply.target = req.target;
		reply.time = req.time;
		reply.property = None;
		const Atom property = req.property != None ? req.property : req.target;
		std::string text;
		bool owned;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			text = m_text;
			owned = m_owned;
		}
		if (req.selection == m_primary && owned) {
			if (req.target == m_targets) {
				const Atom targets[] = { m_targets, m_utf8, XA_STRING, m_textAtom };
				m_x.changeProperty(m_dpy, req.requestor, property, XA_ATOM, 32, PropModeReplace,
				                   reinterpret_cast<const unsigned char*>(targets), 4);
				reply.property = property;
			} else if (req.target == m_utf8 || req.target == XA_STRING || req.target == m_textAtom) {
				const Atom type = req.target == XA_STRING ? XA_STRING : m_utf8;
				if (text.size() > kIncrThreshold) {
					// Too big for one property: announce INCR (value = total size) and send the text
					// chunk by chunk as the requestor deletes the property. Listening for its
					// deletions must start before the reply goes out.
					m_x.selectInput(m_dpy, req.requestor, PropertyChangeMask);
					long size = static_cast<long>(text.size());
					m_x.changeProperty(m_dpy, req.requestor, property, m_incr, 32, PropModeReplace,
					                   reinterpret_cast<const unsigned char*>(&size), 1);
					m_transfers.push_back({ req.requestor, property, type, std::move(text), 0,
					                        std::chrono::steady_clock::now() });
				} else {
					m_x.changeProperty(m_dpy, req.requestor, property, type, 8, PropModeReplace,
					                   reinterpret_cast<const unsigned char*>(text.data()),
					                   static_cast<int>(text.size()));
				}
				reply.property = property;
			}
		}
		XEvent out{};
		out.xselection = reply;
		m_x.sendEvent(m_dpy, req.requestor, False, NoEventMask, &out);
		m_x.flush(m_dpy);
	}

	// Above this many bytes a selection is sent as INCR chunks of kIncrChunk; a property that
	// large could exceed the server's maximum request size. Received INCR text is capped too.
	static constexpr std::size_t kIncrThreshold = 64 * 1024;
	static constexpr std::size_t kIncrChunk = 32 * 1024;
	static constexpr std::size_t kMaxIncrBytes = std::size_t(256) << 20;

	Xlib     m_x;
	Display* m_dpy = nullptr;
	Window   m_win = 0;
	Atom     m_primary = 0, m_targets = 0, m_utf8 = 0, m_textAtom = 0, m_incr = 0, m_property = 0;
	int      m_wakeFd[2] = { -1, -1 };
	std::thread m_thread;
	std::atomic<bool> m_quit{ false };

	std::mutex m_mutex;
	std::condition_variable m_cv;
	std::string m_text, m_reply, m_incrBuf;
	std::vector<Transfer> m_transfers;   // worker thread only
	bool m_incrActive = false;
	std::uint64_t m_progress = 0;        // bumped on every received INCR step, so getText() can wait on it
	bool m_owned = false, m_claim = false, m_claimed = false, m_fetch = false, m_fetchSent = false, m_replyReady = false;
};

} // namespace

std::shared_ptr<X11Primary> X11Primary::create() {
	auto impl = std::make_shared<X11PrimaryImpl>();
	if (!impl->init()) {
		return nullptr;
	}
	return impl;
}

} // namespace lightGraphics::ui

#else  // no X11 headers / not Linux

namespace lightGraphics::ui {
std::shared_ptr<X11Primary> X11Primary::create() { return nullptr; }
}

#endif
