/*
 * QDOS's simulator backend, ported from SDL3 to Qt.
 *
 * The keypad, the case and the store are the simulator's own, so the phone
 * shows and keeps what the Android app does; what changes is only where the
 * pixels go and where the presses come from. See external/qdos/src/hal/sim/
 * sim_sdl3.c, which this follows function for function.
 */

#include "machine.h"

#include "hal/wallclock.h"

#include <qdos/shell.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <dirent.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

/**
 * The panel is 1bpp and the kernel cuts at 128, so a stray mid-grey shows up
 * here as it would on the hardware
 */
#define PANEL_THRESHOLD 128

/* Reflective silver, not white: the panel has no backlight */
static const uint8_t PANEL_INK[3] = {0x1A, 0x1C, 0x1A};
static const uint8_t PANEL_PAPER[3] = {0xC9, 0xCE, 0xC6};

#define WATCH_EVENTS (IN_CLOSE_WRITE | IN_MOVED_TO | IN_MOVED_FROM | IN_DELETE | IN_CREATE)

Machine::Machine(const Stores& stores, QObject* parent)
		: QObject(parent)
		, m_system(QFile::encodeName(stores.system))
		, m_user(QFile::encodeName(stores.user))
		, m_inbox(QFile::encodeName(stores.inbox)) {
	memset(&m_hal, 0, sizeof(m_hal));
	memset(m_rgb, 0, sizeof(m_rgb));

	// Read by the shell's module loader: nothing is mapped out of the inbox
	qputenv("QDOS_NATIVE_CACHE", QFile::encodeName(stores.nativeCache));

	// The phone's own battery is the calculator's: whichever supply says it is one
	const QDir supplies(QStringLiteral("/sys/class/power_supply"));
	for (const QString& name : supplies.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
		QFile type(supplies.filePath(name + QStringLiteral("/type")));
		if (type.open(QIODevice::ReadOnly) && type.readAll().trimmed() == "Battery") {
			const QString capacity = supplies.filePath(name + QStringLiteral("/capacity"));
			if (QFileInfo(capacity).isReadable()) {
				m_battery = QFile::encodeName(capacity);
				break;
			}
		}
	}

	connect(this, &Machine::frameReady, this, [this](const QImage& image) {
		m_frame = image;
		emit frameChanged();
	});
	connect(this, &Machine::stackReady, this, [this](const QStringList& stack, int depth) {
		m_stack = stack;
		m_depth = depth;
		emit stackChanged();
	});

	m_hal.init = halInit;
	m_hal.shutdown = halShutdown;
	m_hal.present = halPresent;
	m_hal.poll_key = halPollKey;
	m_hal.modifier = halModifier;
	m_hal.running = halRunning;
	m_hal.ticks_ms = halTicks;
	m_hal.wait = halWait;
	m_hal.store_read = halStoreRead;
	m_hal.store_write = halStoreWrite;
	m_hal.store_remove = halStoreRemove;
	m_hal.store_list = halStoreList;
	m_hal.store_path = halStorePath;
	m_hal.store_changed = halStoreChanged;
	m_hal.time_of_day = halTimeOfDay;
	m_hal.battery = halBattery;
	m_hal.show_stack = halShowStack;
	// No usb_export: the inbox is in the phone's Documents, which a PC reaches
	// over MTP at any time, so there is nothing to hand over and the shell
	// does not offer it
	m_hal.impl = this;
}

Machine::~Machine() {
	stop();
}

void Machine::start() {
	{
		std::lock_guard<std::mutex> hold(m_lock);
		if (m_running) {
			return;
		}
		m_running = true;
	}
	m_shell = std::thread(&Machine::run, this);
}

void Machine::stop() {
	{
		std::lock_guard<std::mutex> hold(m_lock);
		m_running = false;
	}
	m_wake.notify_all();

	if (m_shell.joinable()) {
		m_shell.join();
	}
}

void Machine::run() {
	if (m_hal.init(&m_hal) == 0) {
		qdos_shell* shell = qdos_shell_create(&m_hal);
		if (shell != nullptr) {
			qdos_shell_run(shell);
			qdos_shell_destroy(shell);
		} else {
			fprintf(stderr, "qdos: out of memory\n");
		}
	}
	m_hal.shutdown(&m_hal);

	// Still running means nobody asked it to stop: it turned itself off
	bool byItself;
	{
		std::lock_guard<std::mutex> hold(m_lock);
		byItself = m_running;
		m_running = false;
	}
	if (byItself) {
		QMetaObject::invokeMethod(this, "poweredOff", Qt::QueuedConnection);
	}
}

void Machine::post(const Event& ev) {
	{
		std::lock_guard<std::mutex> hold(m_lock);
		m_events.push_back(ev);
	}
	m_wake.notify_all();
}

void Machine::press(int x, int y) {
	post(Event{Kind::Press, x, y, QDOS_KEY_NONE, 0});
}

void Machine::release() {
	post(Event{Kind::Release, 0, 0, QDOS_KEY_NONE, 0});
}

void Machine::key(qdos_key k) {
	post(Event{Kind::Key, 0, 0, k, 0});
}

void Machine::typed(char ch) {
	post(Event{Kind::Char, 0, 0, QDOS_KEY_NONE, ch});
}

void Machine::save() {
	post(Event{Kind::Save, 0, 0, QDOS_KEY_NONE, 0});
}

/* --- Lifecycle ------------------------------------------------------------ */

int Machine::halInit(qdos_hal* hal) {
	Machine* m = self(hal);
	m->m_start = std::chrono::steady_clock::now();

	mkdir(m->dirFor(QDOS_SCOPE_INBOX), 0755);

	m->m_watchFd = inotify_init1(IN_CLOEXEC);
	if (m->m_watchFd >= 0 && pipe(m->m_stopFd) == 0) {
		m->watchInbox();
		m->m_watcher = std::thread(&Machine::watchThread, m);
	}
	return 0;
}

void Machine::halShutdown(qdos_hal* hal) {
	Machine* m = self(hal);

	// Told to stop before being waited for: closing the descriptor it is
	// blocked on leaves the read blocked and the join never returns
	if (m->m_watcher.joinable()) {
		const char stop = 'x';
		ssize_t ignored = write(m->m_stopFd[1], &stop, 1);
		(void)ignored;
		m->m_watcher.join();
	}

	if (m->m_stopFd[0] >= 0) {
		close(m->m_stopFd[0]);
		close(m->m_stopFd[1]);
		m->m_stopFd[0] = -1;
		m->m_stopFd[1] = -1;
	}
	if (m->m_watchFd >= 0) {
		close(m->m_watchFd);
		m->m_watchFd = -1;
	}
}

bool Machine::halRunning(qdos_hal* hal) {
	Machine* m = self(hal);
	std::lock_guard<std::mutex> hold(m->m_lock);
	return m->m_running;
}

uint32_t Machine::halTicks(qdos_hal* hal) {
	const auto since = std::chrono::steady_clock::now() - self(hal)->m_start;
	return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(since).count();
}

void Machine::halWait(qdos_hal* hal, int timeout_ms) {
	Machine* m = self(hal);
	std::unique_lock<std::mutex> hold(m->m_lock);

	// Anything queued is left there, for halPollKey to see
	auto ready = [m] { return !m->m_events.empty() || !m->m_running; };
	if (timeout_ms < 0) {
		m->m_wake.wait(hold, ready);
	} else {
		m->m_wake.wait_for(hold, std::chrono::milliseconds(timeout_ms), ready);
	}
}

/* --- Display -------------------------------------------------------------- */

/** @brief Redraw the keypad over the last panel image and send it to the view */
void Machine::pushFrame() {
	qdos_frame_draw(m_rgb, QDOS_WINDOW_W);
	qdos_pad_draw(m_rgb, QDOS_WINDOW_W, QDOS_PAD_X, QDOS_PAD_Y, m_layer, m_pressed);

	const QImage image(m_rgb, QDOS_WINDOW_W, QDOS_WINDOW_H, QDOS_WINDOW_W * 3, QImage::Format_RGB888);
	// A copy: the buffer is redrawn under the view the next time round
	emit frameReady(image.copy());
}

void Machine::halPresent(qdos_hal* hal, const uint8_t* fb) {
	Machine* m = self(hal);

	for (int y = 0; y < QDOS_SCREEN_H; y++) {
		for (int x = 0; x < QDOS_SCREEN_W; x++) {
			const bool lit = fb[(size_t)y * QDOS_SCREEN_W + x] < PANEL_THRESHOLD;
			const uint8_t* c = lit ? PANEL_INK : PANEL_PAPER;
			uint8_t* p = &m->m_rgb[(((size_t)(y + QDOS_PANEL_Y)) * QDOS_WINDOW_W + x + QDOS_PANEL_X) * 3];
			p[0] = c[0];
			p[1] = c[1];
			p[2] = c[2];
		}
	}

	m->pushFrame();
}

/* --- Keys ----------------------------------------------------------------- */

bool Machine::halPollKey(qdos_hal* hal, qdos_key_event* out) {
	Machine* m = self(hal);

	// A text button delivers one character per poll, like typing it
	if (m->m_pending != nullptr && *m->m_pending != '\0') {
		out->key = QDOS_KEY_CHAR;
		out->ch = *m->m_pending++;
		return true;
	}

	for (;;) {
		Event ev;
		{
			std::lock_guard<std::mutex> hold(m->m_lock);
			if (m->m_events.empty()) {
				return false;
			}
			ev = m->m_events.front();
			m->m_events.pop_front();
		}

		switch (ev.kind) {
		// The key sinks on the way down and comes back on the way up, the
		// only acknowledgement a touchscreen can give
		case Kind::Release:
			if (m->m_pressed != nullptr) {
				m->m_pressed = nullptr;
				m->pushFrame();
			}
			break;

		case Kind::Press: {
			const qdos_pad_button* b = qdos_pad_at(ev.x, ev.y);
			if (b == nullptr) {
				break;
			}

			// Shown before the key is acted on, so the press lands even where
			// the action itself changes nothing on screen
			m->m_pressed = b;
			m->pushFrame();

			qdos_pad_layer selects;
			if (qdos_pad_modifier(b, &selects)) {
				if (m->m_layer == selects) {
					m->m_layer = QDOS_PAD_PLAIN;
					m->m_locked = QDOS_PAD_PLAIN;
				} else {
					m->m_layer = selects;
					// Letters lock, since a name is more than one press;
					// symbols do not, and hand back to what was showing
					if (selects == QDOS_PAD_ALPHA) {
						m->m_locked = QDOS_PAD_ALPHA;
					}
				}
				m->pushFrame();
				break;
			}

			const qdos_pad_action* a = qdos_pad_action_for(b, m->m_layer);
			const qdos_pad_layer was = m->m_layer;
			m->m_layer = m->m_locked;
			if (was != m->m_layer) {
				m->pushFrame();
			}
			if (a == nullptr) {
				break;
			}

			if (a->key != QDOS_KEY_NONE) {
				out->key = a->key;
				out->ch = 0;
				return true;
			}
			m->m_pending = a->text;
			out->key = QDOS_KEY_CHAR;
			out->ch = *m->m_pending++;
			return true;
		}

		case Kind::Key:
			out->key = ev.key;
			out->ch = 0;
			return true;

		case Kind::Char:
			if (ev.ch >= '0' && ev.ch <= '9') {
				out->key = (qdos_key)(QDOS_KEY_0 + (ev.ch - '0'));
				out->ch = 0;
				return true;
			}
			out->ch = 0;
			switch (ev.ch) {
			case '.':
				out->key = QDOS_KEY_DOT;
				return true;
			case '+':
				out->key = QDOS_KEY_ADD;
				return true;
			case '-':
				out->key = QDOS_KEY_SUB;
				return true;
			case '*':
				out->key = QDOS_KEY_MUL;
				return true;
			case '/':
				out->key = QDOS_KEY_DIV;
				return true;
			default:
				out->key = QDOS_KEY_CHAR;
				out->ch = ev.ch;
				return true;
			}

		// Sailfish may kill an app in the background to free memory
		case Kind::Save:
			if (hal->save != nullptr) {
				hal->save(hal->save_user);
			}
			break;

		case Kind::Wake:
			break;
		}
	}
}

qdos_keypad_mod Machine::halModifier(qdos_hal* hal) {
	switch (self(hal)->m_layer) {
	case QDOS_PAD_ALPHA:
		return QDOS_MOD_ALPHA;
	case QDOS_PAD_SYMBOL:
		return QDOS_MOD_SYMBOL;
	default:
		return QDOS_MOD_NONE;
	}
}

/* --- Status --------------------------------------------------------------- */

bool Machine::halTimeOfDay(qdos_hal* hal, int* seconds) {
	(void)hal;
	return qdos_wallclock(seconds);
}

int Machine::halBattery(qdos_hal* hal) {
	Machine* m = self(hal);
	if (m->m_battery.isEmpty()) {
		return -1;
	}

	FILE* f = fopen(m->m_battery.constData(), "r");
	if (f == nullptr) {
		return -1;
	}
	int percent = -1;
	if (fscanf(f, "%d", &percent) != 1 || percent < 0 || percent > 100) {
		percent = -1;
	}
	fclose(f);
	return percent;
}

/** @brief For the cover, which is too small for the panel to be read on */
void Machine::halShowStack(qdos_hal* hal, const char* const* rows, size_t count, size_t depth) {
	Machine* m = self(hal);
	QStringList stack;
	for (size_t i = 0; i < count; i++) {
		stack.append(QString::fromUtf8(rows[i]));
	}
	if (m->m_shownStack == stack && m->m_shownDepth == depth) {
		return;
	}
	m->m_shownStack = stack;
	m->m_shownDepth = depth;
	emit m->stackReady(stack, (int)depth);
}

/* --- Store ---------------------------------------------------------------- */

const char* Machine::dirFor(qdos_store_scope scope) const {
	switch (scope) {
	case QDOS_SCOPE_SYSTEM:
		return m_system.constData();
	case QDOS_SCOPE_INBOX:
		return m_inbox.constData();
	default:
		return m_user.constData();
	}
}

static bool storePath(const char* dir, const char* name, char* buf, size_t cap) {
	if (!qdos_store_name_ok(name)) {
		return false;
	}

	const int written = snprintf(buf, cap, "%s/%s", dir, name);
	return written > 0 && (size_t)written < cap;
}

static bool isDir(const char* dir, const char* name) {
	char path[512];
	if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path)) {
		return false;
	}

	struct stat sb;
	return stat(path, &sb) == 0 && S_ISDIR(sb.st_mode);
}

qdos_store_result Machine::halStoreRead(
		qdos_hal* hal, qdos_store_scope scope, const char* name, void* buf, size_t cap, size_t* len) {
	char path[512];
	if (!storePath(self(hal)->dirFor(scope), name, path, sizeof(path))) {
		return QDOS_STORE_IO_ERROR;
	}

	FILE* f = fopen(path, "rb");
	if (!f) {
		return QDOS_STORE_NOT_FOUND;
	}

	const size_t got = fread(buf, 1, cap, f);
	// A full buffer with bytes left is too-small, not a short read
	const bool overflowed = (got == cap) && (fgetc(f) != EOF);
	fclose(f);

	if (overflowed) {
		return QDOS_STORE_TOO_BIG;
	}

	if (len) {
		*len = got;
	}
	return QDOS_STORE_OK;
}

qdos_store_result Machine::halStoreWrite(qdos_hal* hal, const char* name, const void* buf, size_t len) {
	const char* dir = self(hal)->dirFor(QDOS_SCOPE_USER);

	char path[512];
	if (!storePath(dir, name, path, sizeof(path))) {
		return QDOS_STORE_IO_ERROR;
	}

	mkdir(dir, 0755); // may already exist, which is fine

	// An app is written into a folder of its own, which may not be there yet
	char* slash = strrchr(path, '/');
	if (slash != nullptr && strchr(name, '/') != nullptr) {
		*slash = '\0';
		mkdir(path, 0755);
		*slash = '/';
	}

	FILE* f = fopen(path, "wb");
	if (!f) {
		return QDOS_STORE_IO_ERROR;
	}

	const size_t written = fwrite(buf, 1, len, f);
	const bool ok = (fclose(f) == 0) && (written == len);
	return ok ? QDOS_STORE_OK : QDOS_STORE_IO_ERROR;
}

qdos_store_result Machine::halStoreRemove(qdos_hal* hal, const char* name) {
	char path[512];
	if (!storePath(self(hal)->dirFor(QDOS_SCOPE_USER), name, path, sizeof(path))) {
		return QDOS_STORE_IO_ERROR;
	}

	if (remove(path) == 0) {
		return QDOS_STORE_OK;
	}
	return (errno == ENOENT) ? QDOS_STORE_NOT_FOUND : QDOS_STORE_IO_ERROR;
}

bool Machine::halStorePath(qdos_hal* hal, qdos_store_scope scope, const char* name, char* buf, size_t cap) {
	return storePath(self(hal)->dirFor(scope), name, buf, cap);
}

qdos_store_result Machine::halStoreList(
		qdos_hal* hal, qdos_store_scope scope, const char* folder, qdos_store_visit visit, void* user) {
	const char* base = self(hal)->dirFor(scope);

	char root[512];
	if (folder == nullptr || *folder == '\0') {
		snprintf(root, sizeof(root), "%s", base);
	} else if (!storePath(base, folder, root, sizeof(root))) {
		return QDOS_STORE_IO_ERROR;
	}

	DIR* dir = opendir(root);
	if (!dir) {
		return QDOS_STORE_NOT_FOUND;
	}

	const struct dirent* ent;
	while ((ent = readdir(dir)) != nullptr) {
		if (ent->d_name[0] == '.') {
			continue;
		}

		// A folder is listed with the mark on it, being an app and not a file
		char name[288];
		const int written = snprintf(name, sizeof(name), "%s%s", ent->d_name, isDir(root, ent->d_name) ? "/" : "");
		if (written <= 0 || (size_t)written >= sizeof(name)) {
			continue;
		}

		if (!visit(name, user)) {
			break;
		}
	}

	closedir(dir);
	return QDOS_STORE_OK;
}

bool Machine::halStoreChanged(qdos_hal* hal) {
	Machine* m = self(hal);
	const bool changed = m->m_storeDirty;
	m->m_storeDirty = false;
	return changed;
}

/** @brief The inbox and every app folder in it; inotify does not recurse */
void Machine::watchInbox() {
	if (m_watchFd < 0) {
		return;
	}

	if (m_watchId >= 0) {
		inotify_rm_watch(m_watchFd, m_watchId);
	}
	for (size_t i = 0; i < m_subCount; i++) {
		inotify_rm_watch(m_watchFd, m_subId[i]);
	}
	m_subCount = 0;

	const char* inbox = dirFor(QDOS_SCOPE_INBOX);
	m_watchId = inotify_add_watch(m_watchFd, inbox, WATCH_EVENTS);

	DIR* dir = opendir(inbox);
	if (dir == nullptr) {
		return;
	}

	const size_t most = sizeof(m_subId) / sizeof(m_subId[0]);
	const struct dirent* ent;
	while ((ent = readdir(dir)) != nullptr && m_subCount < most) {
		if (ent->d_name[0] == '.' || !isDir(inbox, ent->d_name)) {
			continue;
		}

		char path[512];
		if (snprintf(path, sizeof(path), "%s/%s", inbox, ent->d_name) >= (int)sizeof(path)) {
			continue;
		}

		const int id = inotify_add_watch(m_watchFd, path, WATCH_EVENTS);
		if (id >= 0) {
			m_subId[m_subCount++] = id;
		}
	}
	closedir(dir);
}

void Machine::watchThread() {
	struct pollfd fds[2] = {
			{m_watchFd, POLLIN, 0},
			{m_stopFd[0], POLLIN, 0},
	};

	for (;;) {
		if (poll(fds, 2, -1) < 0) {
			break;
		}
		if (fds[1].revents != 0) {
			break; // shutdown
		}

		char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
		if (read(m_watchFd, buf, sizeof(buf)) <= 0) {
			break;
		}

		m_storeDirty = true;

		// A folder that has just arrived is not being watched yet
		watchInbox();

		post(Event{Kind::Wake, 0, 0, QDOS_KEY_NONE, 0});
	}
}
