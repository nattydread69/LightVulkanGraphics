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

#include <lightVulkanGraphics/ui/widgets/FileDialogBase.h>
#include <lightVulkanGraphics/ui/widgets/Button.h>
#include <lightVulkanGraphics/ui/widgets/Checkbox.h>
#include <lightVulkanGraphics/ui/widgets/DropDown.h>
#include <lightVulkanGraphics/ui/widgets/Label.h>
#include <lightVulkanGraphics/ui/widgets/Row.h>
#include <lightVulkanGraphics/ui/widgets/Spacer.h>
#include <lightVulkanGraphics/ui/widgets/TextBox.h>
#include <lightVulkanGraphics/ui/GuiContext.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace lightGraphics::ui {

namespace {
	constexpr float kDialogWidth = 640.0f;
	// open() shrinks the dialog to leave at least this much of the window showing
	// around it, so it never opens bigger than the window it's in.
	constexpr float kScreenMargin = 20.0f;

	char asciiLower(char c) {
		return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
	}

	bool endsWithIgnoreCase(std::string_view name, std::string_view suffix) {
		if (suffix.size() > name.size()) {
			return false;
		}
		std::size_t const offset = name.size() - suffix.size();
		for (std::size_t i = 0; i < suffix.size(); ++i) {
			if (asciiLower(name[offset + i]) != asciiLower(suffix[i])) {
				return false;
			}
		}
		return true;
	}

	bool containsIgnoreCase(std::string_view text, std::string_view needle) {
		if (needle.empty()) {
			return true;
		}
		auto const it = std::search(text.begin(), text.end(), needle.begin(), needle.end(),
			[](char a, char b) { return asciiLower(a) == asciiLower(b); });
		return it != text.end();
	}

	// Names travel through the GUI as UTF-8; std::filesystem::path::string() would
	// instead use the platform's narrow encoding (the ANSI code page on Windows), and
	// can throw for a name it can't represent there.
	std::string toUtf8(const fs::path& p) {
#if defined(__cpp_char8_t)
		std::u8string const s = p.u8string();
		return std::string(reinterpret_cast<const char*>(s.data()), s.size());
#else
		return p.u8string();
#endif
	}

	fs::path fromUtf8(std::string_view s) {
#if defined(__cpp_char8_t)
		std::u8string u8;
		u8.reserve(s.size());
		for (char c : s) {
			u8.push_back(static_cast<char8_t>(c));
		}
		return fs::path(u8);
#else
		return fs::u8path(s.begin(), s.end());
#endif
	}

	// Absolute and lexically normal, without a trailing separator (except for a root
	// like "/"), so parent_path() always means "up one folder".
	fs::path normaliseDirectory(std::string_view directory) {
		fs::path p = directory.empty() ? fs::path(".") : fromUtf8(directory);
		std::error_code ec;
		fs::path const absolute = fs::absolute(p, ec);
		if (!ec) {
			p = absolute;
		}
		p = p.lexically_normal();
		if (p.has_relative_path() && !p.has_filename()) {
			p = p.parent_path();
		}
		return p;
	}

	bool isHiddenName(std::string_view name) {
		return !name.empty() && name.front() == '.';
	}

	std::string countPhrase(std::size_t n, const char* singular) {
		return std::to_string(n) + " " + singular + (n == 1 ? "" : "s");
	}
}

FileDialogBase::FileDialogBase(GuiContext& context, std::string title, std::string directory,
                               std::string extensionFilter, float height)
	: m_context(context)
	, m_directory(toUtf8(normaliseDirectory(directory)))
{
	if (extensionFilter.empty()) {
		m_filters.push_back({ "All files", {} });
	} else {
		m_filters.push_back({ "Files (*" + extensionFilter + ")", { extensionFilter } });
	}

	// Centered over whatever the display size happens to be at construction time;
	// showDialog() re-centers against the current size every time it's shown, so a
	// window resize between construction and first use doesn't leave it off-centre.
	Vec2 const display = m_context.input().displaySize;
	Rect const bounds{
		std::max(0.0f, (display.x - kDialogWidth) * 0.5f),
		std::max(0.0f, (display.y - height) * 0.5f),
		kDialogWidth, height
	};

	m_panel = m_context.createPanel(std::move(title), bounds,
		PanelFlags::Modal | PanelFlags::Closable | PanelFlags::Movable | PanelFlags::Resizable);
	m_panel->setVisible(false);
	// Cancel button, the title-bar X, and Escape all route through requestClose(),
	// which fires this -- one path for every "the user backed out" case. finish()
	// deliberately does NOT go through requestClose() (it calls setVisible(false)
	// directly instead), so a successful confirm never also fires onCancel -- see
	// Panel::setOnClose's own doc comment for why setVisible(false) alone is silent.
	m_panel->setOnClose([this] { if (m_onCancel) { m_onCancel(); } });

	const Theme& th = m_context.theme();

	// ---- navigation row: [Up] [path] ----
	auto* navRow = m_panel->add<Row>();
	navRow->setWeights({ 1.0f, 13.0f });
	m_upButton = navRow->add<Button>("\xE2\x86\x91");   // U+2191 UPWARDS ARROW
	m_upButton->setTooltip("Up one folder (Backspace)");
	m_upButton->setOnClick([this] { navigateUp(); });
	m_pathField = navRow->add<TextBox>("");
	m_pathField->setTooltip("Type a folder and press Enter to go there");
	// Enter goes to the typed folder; clicking away instead puts back the folder actually
	// listed, so the field never names a folder the list isn't showing. TextBox fires
	// onCommit (Enter AND focus loss) before onSubmit (Enter only): the commit handler
	// keeps what was typed and restores the field, and the submit handler -- reached only
	// on Enter -- acts on what was kept.
	m_pathField->setOnCommit([this](std::string_view text) {
		m_typedPath = std::string(text);
		m_pathField->setText(m_directory, false);
	});
	m_pathField->setOnSubmit([this](std::string_view) {
		if (m_navigationEnabled && m_typedPath != m_directory && !navigateTo(m_typedPath)) {
			setStatus("Folder not found: " + m_typedPath, true);
		}
	});

	// ---- filter row: [search] [file type] Hidden [ ] ----
	auto* filterRow = m_panel->add<Row>();
	filterRow->setWeights({ 5.0f, 4.0f, 1.6f });
	m_searchField = filterRow->add<TextBox>("");
	m_searchField->setPlaceholder("Search");
	m_searchField->setTooltip("Show only names containing this text");
	m_searchField->setOnChange([this](std::string_view text) {
		m_searchText = std::string(text);
		applyFilters();
	});
	m_typeDropDown = filterRow->add<DropDown>("", std::vector<std::string>{}, 0);
	m_typeDropDown->setTooltip("Which kinds of file to list");
	m_typeDropDown->setOnChange([this](int index) {
		m_filterIndex = index;
		applyFilters();
	});
	m_hiddenToggle = filterRow->add<Checkbox>("Hidden", false);
	// Box right after the word instead of at the theme's label/control split -- the
	// cell is far too narrow for that ratio to leave the label readable.
	m_hiddenToggle->setLabelWidthOverride(
		m_context.font().measureText("Hidden", th.fontSize).x + 2.0f * th.itemSpacing);
	m_hiddenToggle->setTooltip("Show hidden files (names starting with '.')");
	m_hiddenToggle->setOnChange([this](bool show) {
		m_showHidden = show;
		applyFilters();
	});

	// ---- the list itself, taking whatever height the dialog has left ----
	m_fileList = m_panel->add<FileListView>();
	m_fileList->setFillPanel(true);
	m_fileList->setVisibleRows(5);
	m_fileList->setOnSelectionChange([this](const FileEntry* entry) { onSelectionChanged(entry); });
	m_fileList->setOnActivate([this](const FileEntry& entry) {
		if (entry.isDirectory) {
			navigateTo(pathInDirectory(entry.name));
		} else {
			onFileActivated(entry);
		}
	});
	m_fileList->setOnNavigateUp([this] { navigateUp(); });

	rebuildTypeDropDown();
	syncNavigationControls();
}

FileDialogBase::~FileDialogBase() = default;

void FileDialogBase::addFooter(std::string confirmLabel) {
	m_statusLabel = m_panel->add<Label>("");

	auto* buttonRow = m_panel->add<Row>();
	buttonRow->setWeights({ 3.0f, 1.0f, 1.0f });
	buttonRow->add<Spacer>(0.0f);
	m_confirmButton = buttonRow->add<Button>(std::move(confirmLabel));
	m_confirmButton->setOnClick([this] { onConfirmClicked(); });
	m_cancelButton = buttonRow->add<Button>("Cancel");
	m_cancelButton->setOnClick([this] { m_panel->requestClose(); });
}

bool FileDialogBase::isOpen() const {
	return m_panel && m_panel->visible();
}

// ---- showing / finishing -----------------------------------------------------------

void FileDialogBase::showDialog() {
	m_searchText.clear();
	m_searchField->setText("", false);
	m_fileList->setSelectedIndex(-1, false);
	rescan();

	Vec2 const display = m_context.input().displaySize;
	Rect bounds = m_panel->bounds();
	if (display.x > 0.0f && display.y > 0.0f) {
		bounds.w = std::min(bounds.w, std::max(200.0f, display.x - 2.0f * kScreenMargin));
		bounds.h = std::min(bounds.h, std::max(160.0f, display.y - 2.0f * kScreenMargin));
	}
	bounds.x = std::max(0.0f, (display.x - bounds.w) * 0.5f);
	bounds.y = std::max(0.0f, (display.y - bounds.h) * 0.5f);
	m_panel->setBounds(bounds);

	m_panel->setVisible(true);
	m_panel->bringToFront();
}

void FileDialogBase::finish(const std::string& fullPath) {
	if (m_onConfirm) {
		m_onConfirm(fullPath);
	}
	m_panel->setVisible(false);
}

// ---- folder ------------------------------------------------------------------------

void FileDialogBase::setDirectory(std::string directory) {
	m_directory = toUtf8(normaliseDirectory(directory));
	m_fileList->setSelectedIndex(-1, false);
	if (isOpen()) {
		rescan();
	} else {
		syncNavigationControls();
	}
}

void FileDialogBase::setNavigationEnabled(bool enabled) {
	m_navigationEnabled = enabled;
	syncNavigationControls();
	applyFilters();
}

bool FileDialogBase::navigateTo(const std::string& path, std::string_view selectName) {
	if (!m_navigationEnabled) {
		return false;
	}
	fs::path target = fromUtf8(path);
	if (target.is_relative()) {
		target = fromUtf8(m_directory) / target;
	}
	std::error_code ec;
	if (!fs::is_directory(target, ec)) {
		return false;
	}

	m_directory = toUtf8(normaliseDirectory(toUtf8(target)));
	// A search typed for the old folder means nothing in the new one.
	m_searchText.clear();
	m_searchField->setText("", false);
	m_fileList->setSelectedIndex(-1, false);
	rescan();
	if (!selectName.empty()) {
		m_fileList->selectName(selectName, false);
	}
	return true;
}

void FileDialogBase::navigateUp() {
	fs::path const current = fromUtf8(m_directory);
	if (!m_navigationEnabled || !current.has_relative_path()) {
		return;
	}
	navigateTo(toUtf8(current.parent_path()), toUtf8(current.filename()));
}

void FileDialogBase::refresh() {
	rescan();
}

std::string FileDialogBase::pathInDirectory(std::string_view name) const {
	return toUtf8(fromUtf8(m_directory) / fromUtf8(name));
}

bool FileDialogBase::existsInDirectory(std::string_view name) const {
	std::error_code ec;
	return fs::exists(fromUtf8(m_directory) / fromUtf8(name), ec);
}

bool FileDialogBase::isFolderInDirectory(std::string_view name) const {
	std::error_code ec;
	return fs::is_directory(fromUtf8(m_directory) / fromUtf8(name), ec);
}

// ---- what is listed ----------------------------------------------------------------

void FileDialogBase::setFileTypeFilters(std::vector<FileTypeFilter> filters, int selectedIndex) {
	if (filters.empty()) {
		filters.push_back({ "All files", {} });
	}
	m_filters = std::move(filters);
	m_filterIndex = std::clamp(selectedIndex, 0, static_cast<int>(m_filters.size()) - 1);
	rebuildTypeDropDown();
	applyFilters();
}

void FileDialogBase::setFileTypeFilterIndex(int index) {
	m_filterIndex = std::clamp(index, 0, static_cast<int>(m_filters.size()) - 1);
	m_typeDropDown->setSelectedIndex(m_filterIndex, false);
	applyFilters();
}

void FileDialogBase::setSearchText(std::string text) {
	m_searchText = std::move(text);
	m_searchField->setText(m_searchText, false);
	applyFilters();
}

void FileDialogBase::setShowHiddenFiles(bool show) {
	m_showHidden = show;
	m_hiddenToggle->setValue(show, false);
	applyFilters();
}

void FileDialogBase::setSort(FileSortKey key, bool ascending) {
	m_fileList->setSort(key, ascending);
}

const FileTypeFilter* FileDialogBase::activeFilter() const {
	if (m_filterIndex < 0 || m_filterIndex >= static_cast<int>(m_filters.size())) {
		return nullptr;
	}
	const FileTypeFilter& filter = m_filters[static_cast<std::size_t>(m_filterIndex)];
	return filter.extensions.empty() ? nullptr : &filter;
}

bool FileDialogBase::matchesTypeFilter(std::string_view filename) const {
	const FileTypeFilter* filter = activeFilter();
	if (!filter) {
		return true;
	}
	return std::any_of(filter->extensions.begin(), filter->extensions.end(),
		[filename](const std::string& ext) { return endsWithIgnoreCase(filename, ext); });
}

void FileDialogBase::rebuildTypeDropDown() {
	std::vector<std::string> labels;
	labels.reserve(m_filters.size());
	for (const FileTypeFilter& filter : m_filters) {
		labels.push_back(filter.label);
	}
	m_typeDropDown->setItems(std::move(labels));
	m_typeDropDown->setSelectedIndex(m_filterIndex, false);
	// Still shown with a single entry -- it tells the user WHY other files are missing
	// -- but there is nothing to choose.
	m_typeDropDown->setEnabled(m_filters.size() > 1);
}

void FileDialogBase::syncNavigationControls() {
	m_pathField->setText(m_directory, false);
	m_pathField->setReadOnly(!m_navigationEnabled);
	bool const atRoot = !fromUtf8(m_directory).has_relative_path();
	m_upButton->setEnabled(m_navigationEnabled && !atRoot);
}

void FileDialogBase::rescan() {
	m_scanned.clear();
	m_scanError.clear();

	std::error_code ec;
	fs::directory_iterator it(fromUtf8(m_directory), fs::directory_options::skip_permission_denied, ec);
	if (ec) {
		m_scanError = "Can't open this folder.";
	}

	// file_time_type's clock has no portable conversion to system_clock in C++17, so
	// go via "now" on both clocks -- taken once per scan, so every entry is shifted by
	// exactly the same offset and their relative order is preserved.
	auto const fileNow = fs::file_time_type::clock::now();
	auto const systemNow = std::chrono::system_clock::now();

	for (; !ec && it != fs::directory_iterator(); it.increment(ec)) {
		const fs::directory_entry& entry = *it;
		std::error_code entryEc;
		FileEntry fileEntry;
		fileEntry.isDirectory = entry.is_directory(entryEc);
		if (!fileEntry.isDirectory && !entry.is_regular_file(entryEc)) {
			continue;   // sockets, devices, broken links: nothing a user would open
		}
		fileEntry.name = toUtf8(entry.path().filename());
		if (!fileEntry.isDirectory) {
			std::uintmax_t const size = entry.file_size(entryEc);
			if (!entryEc) {
				fileEntry.size = static_cast<std::uint64_t>(size);
			}
		}
		fs::file_time_type const written = entry.last_write_time(entryEc);
		if (!entryEc) {
			auto const asSystem = systemNow +
				std::chrono::duration_cast<std::chrono::system_clock::duration>(written - fileNow);
			fileEntry.modifiedTime = static_cast<std::int64_t>(
				std::chrono::duration_cast<std::chrono::seconds>(asSystem.time_since_epoch()).count());
		}
		m_scanned.push_back(std::move(fileEntry));
	}
	if (ec && m_scanError.empty()) {
		m_scanError = "Couldn't read everything in this folder.";
	}

	syncNavigationControls();
	applyFilters();
}

void FileDialogBase::applyFilters() {
	std::string keepSelected;
	if (const FileEntry* selected = m_fileList->selectedEntry()) {
		keepSelected = selected->name;
	}

	std::vector<FileEntry> shown;
	for (const FileEntry& entry : m_scanned) {
		if (!m_showHidden && isHiddenName(entry.name)) {
			continue;
		}
		if (entry.isDirectory ? !m_navigationEnabled : !matchesTypeFilter(entry.name)) {
			continue;
		}
		if (!containsIgnoreCase(entry.name, m_searchText)) {
			continue;
		}
		shown.push_back(entry);
	}
	m_fileList->setEntries(std::move(shown));
	if (!keepSelected.empty()) {
		m_fileList->selectName(keepSelected, false);
	}

	if (!m_scanError.empty()) {
		m_fileList->setEmptyText("Can't open this folder");
	} else if (!m_searchText.empty()) {
		m_fileList->setEmptyText("No matches for \"" + m_searchText + "\"");
	} else if (m_scanned.empty()) {
		m_fileList->setEmptyText("This folder is empty");
	} else {
		m_fileList->setEmptyText("No files of this type here");
	}
	updateStatusSummary();
}

void FileDialogBase::updateStatusSummary() {
	if (!m_scanError.empty()) {
		setStatus(m_scanError, true);
		return;
	}
	std::size_t files = 0, folders = 0;
	for (const FileEntry& entry : m_fileList->entries()) {
		(entry.isDirectory ? folders : files) += 1;
	}
	// Files the type filter alone is keeping out of the list -- said out loud, so a
	// folder that looks empty or half-empty doesn't look like a scanning problem.
	std::size_t otherTypes = 0;
	for (const FileEntry& entry : m_scanned) {
		if (!entry.isDirectory && (m_showHidden || !isHiddenName(entry.name)) &&
		    containsIgnoreCase(entry.name, m_searchText) && !matchesTypeFilter(entry.name)) {
			++otherTypes;
		}
	}

	std::string text;
	if (files > 0 || folders == 0) {
		text = countPhrase(files, "file");
	}
	if (folders > 0) {
		text += (text.empty() ? "" : ", ") + countPhrase(folders, "folder");
	}
	if (otherTypes > 0) {
		text += "  \xC2\xB7  " + std::to_string(otherTypes) + (otherTypes == 1 ? " file" : " files") +
			" of other types not shown";
	}
	setStatus(text, false);
}

void FileDialogBase::setStatus(std::string text, bool isError) {
	if (!m_statusLabel) {
		return;   // before addFooter() -- nothing to show it in yet
	}
	const Theme& th = m_context.theme();
	m_statusLabel->setText(std::move(text));
	m_statusLabel->setColor(isError ? th.error : Color::lerp(th.text, th.textDisabled, 0.45f));
}

} // namespace lightGraphics::ui
