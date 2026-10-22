// Orbit Store TV app - Library in Crossbar: the storage operation and any
// notice first, then search, location and the status views as folders, the
// games, Storage and the scan. A game opens like a folder: its actions are
// rows and its details sit on the right. Confirmations are bands.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orbit/i18n.hpp"
#include "orbit/look.hpp"
#include "orbit/screens.hpp"
#include "orbit/thread.hpp"

#include <algorithm>

namespace orbit
{

namespace
{

constexpr const char *kViewNames[] = {/* i18n */ "All games", /* i18n */ "Installed", /* i18n */ "On drive",
                                      /* i18n */ "Unavailable"};
constexpr icons::Icon kViewIcons[] = {icons::Icon::books, icons::Icon::check, icons::Icon::drive, icons::Icon::warn};

bool in_view(const LibraryGame &game, int view)
{
    return view == 1 ? game.installed : view == 2 ? game.on_drive : view == 3 ? !game.on_drive : true;
}

std::string status_of(const LibraryGame &game)
{
    return !game.on_drive ? tr("Unavailable") : game.mounted ? tr("Mounted") : game.installed ? tr("Installed") : tr("On drive");
}

// The catalogue game with this title ID, for its cover.
const Game *art_of(const Snapshot &state, std::string_view title_id)
{
    for (const Game &game : state.games)
        for (const Release &release : game.releases)
            if (release.title_id == title_id)
                return &game;
    return nullptr;
}

std::string game_key(const LibraryGame &game)
{
    return "g:" + game.title_id + "|" + game.source_key;
}

// Under Mount game: mounted and where, or whether the source is there and installed.
std::string status_of_line(const LibraryGame &game)
{
    if (game.mounted)
        return std::string(tr("Mounted")) + " \xC2\xB7 " + i18n::place(game.location);
    return std::string(game.on_drive ? tr("Available on drive") : tr("Unavailable")) + " \xC2\xB7 " +
           (game.installed ? tr("Registered on the console") : tr("Not installed on the console"));
}

std::string last_refreshed(const LibrarySnapshot &library)
{
    return library.updated_at > 0.0 ? tr("Saved snapshot \xC2\xB7 Last refreshed {time}",
                                         {{"time", format_age(library.updated_at, unix_seconds())}})
                                    : std::string(tr("Saved snapshot \xC2\xB7 Not refreshed yet"));
}

} // namespace

std::vector<LibraryScreen::XbEntry> LibraryScreen::crossbar_entries() const
{
    const Snapshot &state = ctx_.store.state();
    const LibrarySnapshot &library = state.library;
    std::vector<XbEntry> out;
    if (!selected_.empty())
    {
        // A game: mount or unmount, copy, move; or details only.
        const LibraryGame *game = selected_game();
        if (game == nullptr)
            return {{'x', 0}};
        const bool transfer = game->managed && game->can_manage_source && game->on_drive;
        if (game->managed && library.can(game->mounted ? "unmount_game" : "mount_game"))
            out.push_back({'M', 0});
        if (transfer && library.can("copy_game_source") && library.can("storage_job_status"))
            out.push_back({'C', 0});
        if (transfer && library.can("move_game_source") && library.can("storage_job_status"))
            out.push_back({'V', 0});
        if (out.empty())
            out.push_back({'D', 0});
        return out;
    }
    if (!state.have_library || !library.ready())
        return {{'L', 0}};
    if (library.has_storage_job && library.storage_job.id > 0)
        out.push_back({'j', 0});
    if (section_ == 1)
    {
        // Storage: the drives with their space, then Refresh drives.
        const std::vector<LibraryDrive> &drives = state.library_storage.drives;
        for (int d = 0; d < static_cast<int>(drives.size()); ++d)
            out.push_back({'d', d});
        if (drives.empty())
            out.push_back({'k', 0});
        out.push_back({'r', 0});
        return out;
    }
    if (library.stale)
        out.push_back({'s', 0});
    if (!state.library_error.empty() || !library.message.empty())
        out.push_back({'m', 0});
    if (view_ == 0)
    {
        out.push_back({'q', 0});
        out.push_back({'l', 0});
        for (int v = 1; v < 4; ++v)
            out.push_back({'v', v});
    }
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i)
        out.push_back({'g', i});
    if (rows_.empty())
        out.push_back({library.games.empty() ? 'n' : 'e', 0});
    if (view_ == 0)
    {
        out.push_back({'S', 0});
        if (library.can("rescan"))
            out.push_back({'c', 0});
    }
    return out;
}

xb::List LibraryScreen::crossbar_list() const
{
    xb::List list;
    const std::vector<XbEntry> entries = crossbar_entries();
    list.size = static_cast<int>(entries.size());
    list.fill = [this, entries](int i, xb::Row &row)
    {
        const Snapshot &state = ctx_.store.state();
        const LibrarySnapshot &library = state.library;
        const XbEntry entry = entries[static_cast<std::size_t>(i)];
        row.has_icon = true;
        switch (entry.kind)
        {
        case 'L':
        {
            // Not read yet, or not answering.
            const bool busy = !state.have_library || library.busy;
            row.key = "connect";
            row.icon = icons::Icon::books;
            row.title = busy ? tr("Finding your games") : tr("Connect your library");
            row.sub = busy ? tr("Reading installed titles and drive availability from ShadowMount.")
                           : tr("Library uses ShadowMount\xE2\x80\x99s local games API on your PS5. "
                                "Start a version with that API enabled, then refresh here.");
            if (!busy)
            {
                row.text = tr("Games are not inferred from download history. An unavailable library "
                              "does not mean your games are missing.");
                if (!state.library_error.empty())
                    row.detail = tr(std::string_view(state.library_error));
                row.hint = tr("Refresh library");
            }
            break;
        }
        case 'j':
        {
            // The copy or move under way, pinned at the top.
            const StorageJob &job = library.storage_job;
            std::string title = job.title_id;
            for (const LibraryGame &g : library.games)
                if (g.title_id == job.title_id)
                    title = g.title;
            row.key = "job";
            row.primary = true;
            row.icon = job.operation == "move" ? icons::Icon::move : icons::Icon::copy;
            row.title = job.operation == "move" ? tr("Move {title}", {{"title", title}}) : tr("Copy {title}", {{"title", title}});
            if (!job.destination.empty())
                row.sub = tr("To {path}", {{"path", job.destination}});
            const float progress = job.state == "completed" ? 1.0f
                                   : job.total > 0.0       ? static_cast<float>(std::min(1.0, job.processed / job.total))
                                                           : 0.0f;
            row.bar = progress;
            row.detail = (job.total > 0.0 ? tr("{done} of {total}", {{"done", format_bytes(job.processed)},
                                                                      {"total", format_bytes(job.total)}}) +
                                                " \xC2\xB7 "
                                          : std::string()) +
                         i18n::percent(progress * 100.0);
            if (job.cancel_requested)
                row.detail += " \xC2\xB7 " + std::string(tr("Cancelling\xE2\x80\xA6"));
            if (!job.error.empty())
            {
                row.text = tr(std::string_view(job.error));
                row.warn = true;
            }
            break;
        }
        case 's':
            row.key = "stale";
            row.icon = icons::Icon::history;
            row.title = last_refreshed(library);
            row.sub = tr("Showing the last successful result. Game and drive status may have changed.");
            row.warn = true;
            row.hint = tr("Refresh library");
            break;
        case 'm':
            row.key = "message";
            row.icon = icons::Icon::warn;
            row.title = tr(std::string_view(!state.library_error.empty() ? state.library_error : library.message));
            row.warn = true;
            break;
        case 'q':
            row.key = "search";
            row.icon = icons::Icon::search;
            row.title = query_.empty() ? std::string(tr("Search your library")) : "\xE2\x80\x9C" + query_ + "\xE2\x80\x9D";
            if (!query_.empty())
                row.sub = trn(static_cast<long>(rows_.size()), "{count} game", "{count} games");
            break;
        case 'l':
            row.key = "location";
            row.icon = icons::Icon::drive;
            row.title = tr("Location");
            row.value = location_.empty() ? std::string(tr("All locations")) : i18n::place(location_);
            break;
        case 'v':
        {
            row.key = "v:" + std::to_string(entry.index);
            row.icon = kViewIcons[entry.index];
            row.folder = true;
            row.title = tr(kViewNames[entry.index]);
            const long count = std::count_if(library.games.begin(), library.games.end(),
                                             [&](const LibraryGame &g) { return in_view(g, entry.index); });
            row.sub = trn(count, "{count} game", "{count} games");
            break;
        }
        case 'g':
        {
            const LibraryGame &game = library.games[static_cast<std::size_t>(rows_[static_cast<std::size_t>(entry.index)])];
            row.key = game_key(game);
            row.game = art_of(state, game.title_id);
            row.has_icon = row.game == nullptr;
            row.mark = row.game == nullptr;
            row.title = game.title;
            if (game.platform == "ps5" || game.platform == "ps4")
                row.tag = game.platform == "ps5" ? "PS5" : "PS4";
            row.sub = status_of(game) + " \xC2\xB7 " + (game.on_drive ? i18n::place(game.location) : std::string(tr("Drive disconnected")));
            row.warn = !game.on_drive;
            break;
        }
        case 'e':
            row.key = "empty";
            row.icon = icons::Icon::search;
            row.title = tr("No matching games");
            row.sub = tr("Try a different title, status or location.");
            row.hint = tr("Reset library filters");
            break;
        case 'n':
            row.key = "none";
            row.icon = icons::Icon::books;
            row.title = tr("No games reported yet");
            row.sub = tr("ShadowMount has not reported any installed or on-drive games. "
                         "Refresh after its inventory updates.");
            break;
        case 'S':
        {
            double free = 0.0;
            for (const Drive &drive : state.drives)
                free += drive.free_bytes;
            row.key = "storage";
            row.icon = icons::Icon::drive;
            row.folder = true;
            row.title = tr("Storage");
            row.sub = trn(static_cast<long>(state.drives.size()), "{count} drive", "{count} drives") + " \xC2\xB7 " +
                      tr("{size} free", {{"size", format_bytes(free)}});
            break;
        }
        case 'c':
            row.key = "scan";
            row.icon = icons::Icon::scan;
            row.title = tr("Scan for games");
            row.sub = tr("Search your drives with ShadowMount");
            row.off = library.working();
            break;
        case 'd':
        {
            const LibraryDrive &drive = state.library_storage.drives[static_cast<std::size_t>(entry.index)];
            row.key = "d:" + drive.id;
            row.icon = icons::Icon::drive;
            row.title = i18n::place(drive.label);
            if (drive.read_only)
                row.tag = tr("Read only");
            row.sub = tr("{free} free of {total}", {{"free", format_bytes(drive.free)}, {"total", format_bytes(drive.total)}});
            row.bar = drive.total > 0.0 ? static_cast<float>(std::clamp(drive.used / drive.total, 0.0, 1.0)) : 0.0f;
            row.detail = drive.path + " \xC2\xB7 " +
                         tr("{used} used of {total}", {{"used", format_bytes(drive.used)}, {"total", format_bytes(drive.total)}});
            break;
        }
        case 'k':
            row.key = "drives";
            row.icon = state.library_storage.error.empty() && !state.have_library_storage ? icons::Icon::drive : icons::Icon::warn;
            row.title = !state.library_storage.error.empty() ? std::string(tr(std::string_view(state.library_storage.error)))
                        : state.have_library_storage         ? std::string(tr("No drives reported."))
                                                             : std::string(tr("Checking your drives\xE2\x80\xA6"));
            row.sub = tr("Capacity as ShadowMount reports it. Select a drive to see its games.");
            break;
        case 'r':
            row.key = "refresh-drives";
            row.icon = icons::Icon::refresh;
            row.title = state.library_storage.busy ? tr("Refreshing\xE2\x80\xA6") : tr("Refresh drives");
            break;
        case 'x':
            row.key = "gone";
            row.icon = icons::Icon::warn;
            row.title = tr("This game is no longer reported");
            row.sub = tr("Refresh the library, then try again.");
            row.warn = true;
            break;
        case 'D':
            row.key = "details";
            row.icon = icons::Icon::info;
            row.title = tr("Details");
            row.sub = tr("This item supports viewing details only. ShadowMount does not manage "
                         "its source.");
            break;
        default:
        {
            // A game's actions: mount or unmount first, then copy and move.
            const LibraryGame *game = selected_game();
            const bool blocked = library.working() || ctx_.store.library_action().busy || library.stale || game == nullptr ||
                                 !game->on_drive;
            if (entry.kind == 'M')
            {
                row.key = "mount";
                row.primary = true;
                row.icon = game != nullptr && game->mounted ? icons::Icon::eject : icons::Icon::play;
                row.title = game != nullptr && game->mounted ? tr("Unmount game") : tr("Mount game");
                if (game != nullptr)
                    row.sub = std::string(game->mounted ? tr("Mounted") : tr("Not mounted")) + " \xC2\xB7 " +
                              (game->on_drive ? tr("Available on drive") : tr("Unavailable; the drive or source may be missing"));
                row.off = blocked;
            }
            else
            {
                row.key = entry.kind == 'C' ? "copy" : "move";
                row.icon = entry.kind == 'C' ? icons::Icon::copy : icons::Icon::move;
                row.title = entry.kind == 'C' ? tr("Copy to drive") : tr("Move to drive");
                row.off = blocked || (game != nullptr && game->mounted);
                if (game != nullptr && game->mounted)
                    row.sub = tr("Unmount this game before copying or moving its source.");
            }
            break;
        }
        }
    };
    return list;
}

xb::Band LibraryScreen::crossbar_band() const
{
    // The scan, or a game's unmount, copy or move, as Store's dialogs ask them.
    const Snapshot &state = ctx_.store.state();
    xb::Band band;
    if (scan_open_)
    {
        band.title = tr("Scan your drives?");
        band.body = tr("ShadowMount will search its configured locations for games. It may "
                       "register games on the console or mount detected sources. If a game is "
                       "running, the scan may wait until it is safe.");
        band.buttons = {tr("Start scan"), tr("Back")};
    }
    else if (const LibraryGame *game = selected_game(); game != nullptr && mode_ != 0)
    {
        if (mode_ == 1)
        {
            band.title = tr("Unmount this game?");
            band.body = tr("Close the game on your PS5 before unmounting its source.");
        }
        else
        {
            band.kicker = mode_ == 2 ? tr("Copy {title}", {{"title", game->title}}) : tr("Move {title}", {{"title", game->title}});
            band.title = mode_ == 2 ? tr("Copy to drive") : tr("Move to drive");
            band.body = std::string(mode_ == 3 ? tr("Move the source to another location. The original is removed "
                                                    "only after the move succeeds.")
                                               : tr("Create a second copy on the selected drive. The original stays "
                                                    "in place.")) +
                        " " +
                        tr("Orbit checks the source and free space before starting. Pause "
                           "active and queued downloads first. Existing destination files "
                           "are not overwritten.");
            band.note = tr("From {path}", {{"path", game->path}});
            for (const LibraryDrive *drive : destinations())
            {
                xb::MenuItem option;
                option.label = i18n::place(drive->label) + " \xC2\xB7 " + drive->path + " \xC2\xB7 " +
                               tr("{size} free", {{"size", format_bytes(drive->free)}});
                option.checked = drive->id == destination_id_;
                band.options.push_back(std::move(option));
            }
            if (band.options.empty())
            {
                const bool fresh = state.have_library_storage && !state.library_storage.busy;
                band.busy = !fresh && state.library_storage.error.empty();
                band.error = !state.library_storage.error.empty()
                                 ? std::string(tr(std::string_view(state.library_storage.error)))
                             : fresh ? std::string(tr("Connect another writable drive, or configure another scan "
                                                      "location in ShadowMount."))
                                     : std::string(tr("Checking available destinations\xE2\x80\xA6"));
            }
        }
        const char *confirm = ctx_.store.library_action().busy ? tr("Starting\xE2\x80\xA6")
                              : mode_ == 1                     ? tr("Confirm unmount")
                              : mode_ == 2                     ? tr("Start copy")
                                                               : tr("Confirm move");
        band.buttons = {confirm, tr("Back")};
    }
    if (!error_.empty())
        band.error = tr(std::string_view(error_));
    band.focus = xb_band_;
    return band;
}

std::vector<xb::MenuItem> LibraryScreen::crossbar_menu() const
{
    const Snapshot &state = ctx_.store.state();
    const LibrarySnapshot &library = state.library;
    std::vector<xb::MenuItem> items;
    if (xb_menu_kind_ == 0)
    {
        // Location, with how many games each holds.
        const std::vector<std::string> values = [&]
        {
            std::vector<std::string> out;
            for (const LibraryGame &game : library.games)
                if (!game.location.empty() && std::find(out.begin(), out.end(), game.location) == out.end())
                    out.push_back(game.location);
            std::sort(out.begin(), out.end());
            return out;
        }();
        items.push_back(xb::choice(tr("All locations"), location_.empty()));
        for (const std::string &value : values)
        {
            const long count = std::count_if(library.games.begin(), library.games.end(),
                                             [&](const LibraryGame &g) { return g.location == value; });
            items.push_back(xb::choice(i18n::place(value), value == location_, trn(count, "{count} game", "{count} games")));
        }
    }
    else if (xb_menu_kind_ == 1)
    {
        const StorageJob &job = library.storage_job;
        items.push_back(xb::action(icons::Icon::stop, tr("Cancel operation"),
                                   job.active && job.cancellable && !job.cancel_requested && library.can("storage_job_cancel") &&
                                       !ctx_.store.library_action().busy));
        items.push_back(xb::action(icons::Icon::info, tr("Details"),
                                   std::any_of(library.games.begin(), library.games.end(),
                                               [&](const LibraryGame &g) { return g.title_id == job.title_id; })));
    }
    else
    {
        // A drive: measure the sizes, or open one of its games.
        const bool measuring = library.has_action && library.action.action == "measure" && library.working();
        items.push_back(xb::action(icons::Icon::drive, measuring ? tr("Measuring\xE2\x80\xA6") : tr("Measure game sizes"),
                                   library.can("list_games") && !library.working() && !ctx_.store.library_action().busy));
        for (const LibraryGame *game : drive_games())
        {
            xb::MenuItem item = xb::action(icons::Icon::books, game->title);
            item.value = game->size >= 0.0                     ? format_bytes(game->size)
                         : game->size_status == "unavailable" ? std::string(tr("Unavailable"))
                                                              : std::string(tr("Not measured"));
            items.push_back(std::move(item));
        }
    }
    return items;
}

void LibraryScreen::leave_crossbar(Feedback &feedback)
{
    // Out of a game, a status view or Storage, back to the row that opened it.
    std::string to;
    if (!selected_.empty())
    {
        if (const LibraryGame *game = selected_game())
            to = game_key(*game);
        selected_.clear();
        mode_ = 0;
        error_.clear();
    }
    else if (section_ == 1)
    {
        section_ = 0;
        ctx_.store.want_library_storage(false);
        to = "storage";
    }
    else if (view_ != 0)
    {
        to = "v:" + std::to_string(view_);
        view_ = 0;
        refilter();
    }
    xb_.focus_key(crossbar_list(), to);
    feedback.play(hui::audio::Cue::back);
}

void LibraryScreen::update_band(const InputFrame &input, Feedback &feedback)
{
    // A question: its options (destinations) down, its buttons across.
    const LibrarySnapshot &library = ctx_.store.state().library;
    const xb::Band question = crossbar_band();
    const int options = static_cast<int>(question.options.size());
    const int count = options + static_cast<int>(question.buttons.size());
    int to = xb_band_;
    if (input.nav == Direction::down)
        to = xb_band_ < options ? xb_band_ + 1 : -1;
    else if (input.nav == Direction::up)
        to = xb_band_ >= options ? options - 1 : xb_band_ - 1;
    else if (input.nav == Direction::right)
        to = xb_band_ >= options && xb_band_ + 1 < count ? xb_band_ + 1 : -1;
    else if (input.nav == Direction::left)
        to = xb_band_ > options ? xb_band_ - 1 : -1;
    if (input.nav != Direction::none)
    {
        if (to >= 0 && to < count)
        {
            xb_band_ = to;
            feedback.play(hui::audio::Cue::focus);
        }
        else
            refuse(feedback, input);
    }
    const bool back = input.is_pressed(Action::back) || (input.is_pressed(Action::confirm) && xb_band_ == count - 1);
    if (back)
    {
        scan_open_ = false;
        mode_ = 0;
        error_.clear();
        feedback.play(hui::audio::Cue::modal_close);
    }
    else if (input.is_pressed(Action::confirm) && xb_band_ < options)
    {
        const std::vector<const LibraryDrive *> targets = destinations();
        destination_id_ = targets[static_cast<std::size_t>(xb_band_)]->id;
        feedback.play(hui::audio::Cue::toggle);
    }
    else if (input.is_pressed(Action::confirm))
    {
        const LibraryGame *game = selected_game();
        const bool blocked = library.working() || ctx_.store.library_action().busy;
        if (scan_open_)
        {
            if (blocked)
                refuse(feedback, input);
            else
                run_library("scan", true, nullptr, {}, 0, feedback);
        }
        else if (game == nullptr || blocked || library.stale || !game->on_drive || (mode_ >= 2 && destination_id_.empty()))
            refuse(feedback, input);
        else
            run_library(mode_ == 1 ? "unmount" : mode_ == 2 ? "copy" : "move", true, game,
                        mode_ >= 2 ? destination_id_ : std::string(), 0, feedback);
    }
}

Intent LibraryScreen::update_crossbar(const InputFrame &input, float dt, Feedback &feedback, bool focused)
{
    Intent intent;
    const Snapshot &state = ctx_.store.state();
    const LibrarySnapshot &library = state.library;
    const xb::List list = crossbar_list();
    xb_.clamp(list);
    xb::Row row;
    if (list.size > 0)
        list.fill(xb_.index, row);
    const bool band = scan_open_ || (!selected_.empty() && mode_ != 0);
    if (focused && keyboard_open_)
        update_keyboard(input, feedback);
    else if (focused && band)
        update_band(input, feedback);
    else if (focused && xb_menu_.open)
    {
        const int chosen = xb_menu_.update(input, crossbar_menu(), feedback);
        if (chosen >= 0 && xb_menu_kind_ == 0)
        {
            std::vector<std::string> values;
            for (const LibraryGame &game : library.games)
                if (!game.location.empty() && std::find(values.begin(), values.end(), game.location) == values.end())
                    values.push_back(game.location);
            std::sort(values.begin(), values.end());
            location_ = chosen > 0 && chosen <= static_cast<int>(values.size()) ? values[static_cast<std::size_t>(chosen - 1)]
                                                                                 : std::string();
            refilter();
        }
        else if (chosen == 0 && xb_menu_kind_ == 1)
            run_library("cancel", false, nullptr, {}, library.storage_job.id, feedback);
        else if (chosen == 1 && xb_menu_kind_ == 1)
        {
            for (const LibraryGame &game : library.games)
                if (game.title_id == library.storage_job.title_id && selected_.empty())
                {
                    show_game(game.title_id, game.source_key);
                    xb_.index = 0;
                    xb_.snap();
                }
        }
        else if (chosen == 0 && xb_menu_kind_ == 2)
            run_library("measure", false, nullptr, {}, 0, feedback);
        else if (chosen > 0 && xb_menu_kind_ == 2)
        {
            const std::vector<const LibraryGame *> games = drive_games();
            if (chosen - 1 < static_cast<int>(games.size()))
            {
                show_game(games[static_cast<std::size_t>(chosen - 1)]->title_id, games[static_cast<std::size_t>(chosen - 1)]->source_key);
                xb_.index = 0;
                xb_.snap();
                feedback.play(hui::audio::Cue::open);
            }
        }
    }
    else if (focused)
    {
        if (input.nav == Direction::up || input.nav == Direction::down)
        {
            xb_.move(list, input, feedback);
            xb::Row now;
            list.fill(xb_.index, now);
            if (now.key.rfind("d:", 0) == 0)
                drive_id_ = now.key.substr(2);
        }
        else if (input.is_pressed(Action::back) && (!selected_.empty() || section_ == 1 || view_ != 0))
            leave_crossbar(feedback);
        else if (input.is_pressed(Action::north) && selected_.empty())
        {
            // Triangle refreshes, as the design's hint says.
            if (!state.have_library || library.busy)
                refuse(feedback, input);
            else
            {
                ctx_.store.refresh_library();
                feedback.play(hui::audio::Cue::select);
            }
        }
        else if (input.is_pressed(Action::west) && selected_.empty() && section_ == 0 && state.have_library && library.ready())
        {
            keyboard_open_ = true;
            keyboard_.enter();
            keyboard_.set_length(static_cast<int>(query_.size()));
            feedback.play(hui::audio::Cue::modal_open);
        }
        else if (input.is_pressed(Action::confirm) && list.size > 0)
        {
            const std::string &key = row.key;
            const LibraryGame *game = selected_game();
            if (row.off)
                refuse(feedback, input);
            else if (key == "connect" || key == "stale")
            {
                if (!state.have_library || library.busy)
                    refuse(feedback, input);
                else
                {
                    ctx_.store.refresh_library();
                    feedback.play(hui::audio::Cue::select);
                }
            }
            else if (key == "job")
            {
                xb_menu_kind_ = 1;
                xb_menu_.show(0);
                feedback.play(hui::audio::Cue::modal_open);
            }
            else if (key == "search")
            {
                keyboard_open_ = true;
                keyboard_.enter();
                keyboard_.set_length(static_cast<int>(query_.size()));
                feedback.play(hui::audio::Cue::modal_open);
            }
            else if (key == "location")
            {
                xb_menu_kind_ = 0;
                int at = 0;
                const std::vector<xb::MenuItem> items = crossbar_menu();
                for (int i = 0; i < static_cast<int>(items.size()); ++i)
                    if (items[static_cast<std::size_t>(i)].checked)
                        at = i;
                xb_menu_.show(at);
                feedback.play(hui::audio::Cue::modal_open);
            }
            else if (key.rfind("v:", 0) == 0)
            {
                view_ = key[2] - '0';
                refilter();
                xb_.index = 0;
                xb_.snap();
                feedback.play(hui::audio::Cue::open);
            }
            else if (key.rfind("g:", 0) == 0)
            {
                const std::size_t bar = key.find('|');
                show_game(key.substr(2, bar - 2), key.substr(bar + 1));
                xb_.index = 0;
                xb_.snap();
                feedback.play(hui::audio::Cue::open);
            }
            else if (key == "empty")
            {
                view_ = 0;
                query_.clear();
                location_.clear();
                format_.clear();
                refilter();
                feedback.play(hui::audio::Cue::toggle);
            }
            else if (key == "storage")
            {
                section_ = 1;
                ctx_.store.want_library_storage(true, true);
                xb_.index = 0;
                xb_.snap();
                feedback.play(hui::audio::Cue::open);
            }
            else if (key == "scan")
            {
                scan_open_ = true;
                xb_band_ = 0;
                error_.clear();
                feedback.play(hui::audio::Cue::modal_open);
            }
            else if (key.rfind("d:", 0) == 0)
            {
                drive_id_ = key.substr(2);
                xb_menu_kind_ = 2;
                xb_menu_.show(0);
                feedback.play(hui::audio::Cue::modal_open);
            }
            else if (key == "refresh-drives")
            {
                ctx_.store.want_library_storage(true, true);
                feedback.play(hui::audio::Cue::select);
            }
            else if (key == "mount" && game != nullptr)
            {
                if (!game->mounted)
                    run_library("mount", false, game, {}, 0, feedback);
                else
                {
                    mode_ = 1;
                    xb_band_ = 0;
                    feedback.play(hui::audio::Cue::modal_open);
                }
            }
            else if ((key == "copy" || key == "move") && game != nullptr)
            {
                mode_ = key == "copy" ? 2 : 3;
                destination_id_.clear();
                xb_band_ = 0;
                ctx_.store.want_library_storage(true, true);
                feedback.play(hui::audio::Cue::modal_open);
            }
            else
                refuse(feedback, input);
        }
    }
    keyboard_.set_bounds(xb::keyboard_keys());
    keyboard_.set_active(keyboard_open_);
    keyboard_.update(dt);
    keyboard_in_.target = keyboard_open_ ? 1.0f : 0.0f;
    keyboard_in_.update(dt, 14.0f);
    xb_band_in_.target = scan_open_ || (!selected_.empty() && mode_ != 0) ? 1.0f : 0.0f;
    xb_band_in_.update(dt, 16.0f);
    xb_.animate(dt);
    xb_menu_.animate(dt);
    return intent;
}

void LibraryScreen::draw_crossbar(Frame &frame) const
{
    hui::gfx::DrawList &list = frame.scene;
    const Snapshot &state = ctx_.store.state();
    const LibrarySnapshot &library = state.library;
    const ui::Type &type = ctx_.type;
    // The bar: a game, a status view or Storage takes Library's place.
    std::vector<xb::BarItem> bar = xb::sections(ctx_, false);
    int current = section::library;
    const LibraryGame *game = selected_game();
    if (!selected_.empty() || section_ == 1 || view_ != 0)
    {
        xb::BarItem here;
        if (!selected_.empty())
        {
            here.game = game != nullptr ? art_of(state, game->title_id) : nullptr;
            here.icon = icons::Icon::box;
            here.label = game != nullptr ? game->title : std::string();
        }
        else if (section_ == 1)
        {
            here.icon = icons::Icon::drive;
            here.label = tr("Storage");
        }
        else
        {
            here.icon = kViewIcons[view_];
            here.label = tr(kViewNames[view_]);
        }
        bar = {bar[section::library], here};
        current = 1;
    }
    xb::draw_bar(ctx_, list, bar, current);
    const xb::List rows = crossbar_list();
    xb_.draw(ctx_, list, rows);
    xb::Row row;
    if (rows.size > 0)
        rows.fill(std::clamp(xb_.index, 0, rows.size - 1), row);
    const float hide = std::max({xb_menu_.in.value, keyboard_in_.value});
    list.push_opacity(1.0f - hide);
    if (!selected_.empty() && game != nullptr)
    {
        // The game's statuses, as Store's details list them.
        std::vector<std::pair<std::string, std::string>> facts = {
            {tr("Installation"), game->installed ? tr("Registered on the console") : tr("Not installed on the console")},
            {tr("Mount status"), game->mounted ? tr("Mounted") : tr("Not mounted")},
            {tr("Source"), game->on_drive ? tr("Available on drive") : tr("Unavailable; the drive or source may be missing")},
            {tr("Format"), game->format},
            {tr("Location"), i18n::place(game->location)},
            {tr("Source size"), game->size >= 0.0                     ? format_bytes(game->size)
                                : game->size_status == "unavailable" ? std::string(tr("Unavailable"))
                                                                     : std::string(tr("Use Measure game sizes in Storage"))},
            {tr("Size reported by console"), game->installed_size < 0.0 ? std::string(tr("Not reported")) : format_bytes(game->installed_size)}};
        const std::string kicker = (game->platform == "ps5"   ? std::string("PS5")
                                    : game->platform == "ps4" ? std::string("PS4")
                                                              : std::string(tr("Platform unknown"))) +
                                   " \xC2\xB7 " + game->title_id;
        std::vector<std::string> notes;
        if (library.has_action && library.action.title_id == game->title_id && !library.action.message.empty())
            notes.push_back(std::string(tr(std::string_view(library.action.message))));
        if (library.stale)
            notes.push_back(tr("Saved snapshot. Refresh the library before relying on these statuses."));
        if (!error_.empty() && mode_ == 0)
            notes.push_back(std::string(tr(std::string_view(error_))));
        xb::draw_panel(list, 400.0f, 0.0f, 0.0f, [&](hui::gfx::DrawList &out, float x, float y, float w)
        {
            y = xb::draw_label(ctx_, out, x, y, kicker);
            y = xb::draw_facts(ctx_, out, x, y + 18.0f, w, facts, 2);
            ui::text(out, type.regular, tr("Source path"), x, y + 22.0f + 14.4f + 5.9f, 18.0f, ui::Color::rgb(0xffffff, 0.62f));
            y += 22.0f + 28.8f + 4.0f;
            const std::string path = game->path.empty() ? std::string(tr("Not reported")) : game->path;
            y = ui::paragraph(out, type.regular, path, x, y + 14.4f + 5.9f, 18.0f, w, 26.0f, ui::Color::rgb(0xffffff), 3) - 20.3f + 26.0f;
            // What the statuses mean is on Library's own panel; here, only what needs attention.
            for (const std::string &note : notes)
                y = ui::paragraph(out, type.regular, note, x, y + 16.0f + 12.0f + 5.3f, 17.0f, w, 24.0f, ui::Color::rgb(0xffd27a), 3) -
                    17.3f + 24.0f + 4.0f;
            return y;
        });
    }
    else if (section_ == 1 && library.ready())
    {
        // The focused drive's games and their measured sizes.
        const LibraryDrive *drive = nullptr;
        for (const LibraryDrive &d : state.library_storage.drives)
            if (d.id == drive_id_)
                drive = &d;
        if (drive == nullptr && !state.library_storage.drives.empty())
            drive = &state.library_storage.drives.front();
        const std::vector<const LibraryGame *> games = drive_games();
        if (drive != nullptr)
            xb::draw_panel(list, 430.0f, 0.0f, 0.0f, [&](hui::gfx::DrawList &out, float x, float y, float w)
            {
                y = xb::draw_label(ctx_, out, x, y, tr("Games on {drive}", {{"drive", i18n::place(drive->label)}})) + 8.0f;
                for (std::size_t i = 0; i < games.size() && i < 6; ++i)
                {
                    const LibraryGame &g = *games[i];
                    const ui::Rect art{x, y + 16.0f, 44.0f, 44.0f};
                    if (const Game *cover = art_of(state, g.title_id))
                        draw_cover(ctx_, out, *cover, art, 8.0f);
                    else
                        out.rounded_rect(art, 8.0f, ui::Color::rgb(0xffffff, 0.12f));
                    const std::string size = g.size >= 0.0                     ? format_bytes(g.size)
                                             : g.size_status == "unavailable" ? std::string(tr("Unavailable"))
                                                                              : std::string(tr("Not measured"));
                    const float sw = type.regular.measure(size, 19.0f);
                    ui::text_fit(out, type.regular, g.title, x + 58.0f, y + 38.0f + 7.0f, 21.0f, w - 58.0f - sw - 20.0f,
                                 ui::Color::rgb(0xffffff));
                    ui::text(out, type.regular, size, x + w, y + 38.0f + 6.3f, 19.0f, ui::Color::rgb(0xffffff, 0.6f), ui::Align::right);
                    y += 76.0f;
                    out.rounded_rect({x, y, w, 1.0f}, 0.0f, ui::Color::rgb(0xffffff, 0.12f));
                }
                if (games.empty())
                {
                    ui::text(out, type.regular, trn(0, "{count} game", "{count} games"), x, y + 40.0f, 19.0f,
                             ui::Color::rgb(0xffffff, 0.6f));
                    y += 56.0f;
                }
                if (library.can("list_games"))
                {
                    // The design's button: Cross on the drive offers it first.
                    const bool measuring = library.has_action && library.action.action == "measure" && library.working();
                    const std::string label = measuring ? tr("Measuring\xE2\x80\xA6") : tr("Measure game sizes");
                    const float bw = type.medium.measure(label, 19.0f) + 52.0f + 26.0f;
                    const ui::Rect b{x, y + 24.0f, bw, 54.0f};
                    out.bordered_rect(b, 27.0f, ui::Color::rgb(0xffffff, 0.12f), 1.0f, ui::Color::rgb(0xffffff, 0.18f));
                    icons::draw(out, icons::Icon::drive, b.x + 36.0f, b.cy(), 20.0f, ui::Color::rgb(0xffffff), 1.8f);
                    ui::text(out, type.medium, label, b.x + 56.0f, b.cy() + 19.0f * 0.35f, 19.0f, ui::Color::rgb(0xffffff));
                    y += 24.0f + 54.0f;
                }
                const std::string fine = tr("Sizes are measured on request. Shared source files can appear under "
                                            "more than one title, so these sizes are not a drive total.");
                return ui::paragraph(out, type.regular, fine, x, y + 20.0f + 11.6f + 5.3f, 16.0f, w, 23.2f,
                                     ui::Color::rgb(0xffffff, 0.55f), 4) -
                       16.9f + 23.2f;
            });
    }
    else if (library.ready())
    {
        // The snapshot: when it was read, what the statuses mean, the drives' space.
        std::vector<std::pair<std::string, std::string>> drives;
        for (const Drive &drive : state.drives)
            drives.emplace_back(i18n::place(drive.label), tr("{size} free", {{"size", format_bytes(drive.free_bytes)}}));
        xb::draw_panel(list, 430.0f, 0.0f, 0.0f, [&](hui::gfx::DrawList &out, float x, float y, float w)
        {
            y = xb::draw_label(ctx_, out, x, y, last_refreshed(library));
            const std::string text = tr("Installed means registered on the console. Mounted means its source is "
                                        "currently mounted. On drive means the source is available. These "
                                        "statuses do not confirm that a game will launch.");
            y = ui::paragraph(out, type.regular, text, x, y + 12.0f + 15.5f + 6.6f, 20.0f, w, 31.0f, ui::Color::rgb(0xffffff, 0.82f), 5) -
                22.1f + 31.0f;
            y += 22.0f;
            for (const auto &[label, value] : drives)
            {
                ui::text_fit(out, type.regular, label, x, y + 12.8f + 5.3f, 16.0f, w, ui::Color::rgb(0xffffff, 0.62f));
                ui::text_fit(out, type.regular, value, x, y + 29.6f + 15.2f + 6.3f, 19.0f, w, ui::Color::rgb(0xffffff));
                y += 60.0f + 14.0f;
            }
            return y - 14.0f;
        });
    }
    list.pop_opacity();
    if (xb_menu_.in.value > 0.01f)
    {
        std::string title = tr("Location");
        if (xb_menu_kind_ == 1)
            title = tr("Storage");
        else if (xb_menu_kind_ == 2)
            for (const LibraryDrive &d : state.library_storage.drives)
                if (d.id == drive_id_)
                    title = i18n::place(d.label);
        xb::draw_menu(ctx_, frame.overlay, title, crossbar_menu(), xb_menu_);
    }
    xb::draw_band(ctx_, frame.overlay, crossbar_band(), xb_band_in_.value);
    xb::draw_keyboard(ctx_, frame, keyboard_, tr("Search your library"), query_, false, keyboard_in_.value);
}

std::vector<hui::ui::Hint> LibraryScreen::crossbar_hints() const
{
    if (keyboard_open_)
        return {{hui::ui::Button::square, tr("Delete")},
                {hui::ui::Button::triangle, tr("Space")},
                {hui::ui::Button::circle, tr("Close")}};
    if (scan_open_ || (!selected_.empty() && mode_ != 0))
        return {{hui::ui::Button::cross, tr("Select")}, {hui::ui::Button::circle, tr("Back")}};
    if (xb_menu_.open)
        return {{hui::ui::Button::cross, tr("Choose")}, {hui::ui::Button::circle, tr("Close")}};
    const xb::List rows = crossbar_list();
    xb::Row row;
    if (rows.size > 0)
        rows.fill(std::clamp(xb_.index, 0, rows.size - 1), row);
    if (!selected_.empty() || section_ == 1)
        return {{hui::ui::Button::cross, tr("Select")}, {hui::ui::Button::circle, tr("Back")}};
    std::vector<hui::ui::Hint> hints;
    if (row.key == "connect" || row.key == "stale")
        hints.push_back({hui::ui::Button::cross, tr("Refresh library")});
    else if (row.key == "empty")
        hints.push_back({hui::ui::Button::cross, tr("Reset library filters")});
    else if (row.key == "job")
        hints.push_back({hui::ui::Button::cross, tr("Actions")});
    else if (row.key == "location")
        hints.push_back({hui::ui::Button::cross, tr("Change")});
    else if (row.key != "none" && row.key != "message")
        hints.push_back({hui::ui::Button::cross, tr("Open")});
    if (row.key != "connect" && row.key != "stale")
        hints.push_back({hui::ui::Button::triangle, tr("Refresh library")});
    if (view_ != 0)
        hints.push_back({hui::ui::Button::circle, tr("Back")});
    else
        hints.push_back({hui::ui::Button::l1, tr("Categories"), hui::ui::Button::r1});
    return hints;
}

// ---------------------------------------------------------------- Cinema

int LibraryScreen::cinema_collection() const
{
    return section_ == 1 ? 4 : std::clamp(view_, 0, 3);
}

std::vector<int> LibraryScreen::cinema_rows() const
{
    // The view's games; a copy or move under way comes first in All games.
    const LibrarySnapshot &library = ctx_.store.state().library;
    std::vector<int> rows;
    if (view_ == 0 && library.has_storage_job && library.storage_job.id > 0)
        rows.push_back(-1);
    rows.insert(rows.end(), rows_.begin(), rows_.end());
    return rows;
}

bool LibraryScreen::page(int step)
{
    // L1 and R1: All games, Installed, On drive, Unavailable, then Storage.
    if (!look::is(look::Interface::cinema) || keyboard_open_ || cn_sheet_ || cn_details_ || scan_open_ || mode_ != 0)
        return false;
    const int next = cinema_collection() + step;
    if (next < 0 || next > 4)
        return false;
    cn_buttons_ = false;
    cn_button_ = 0;
    if (next == 4)
    {
        section_ = 1;
        ctx_.store.want_library_storage(true, true);
    }
    else
    {
        if (section_ == 1)
            ctx_.store.want_library_storage(false);
        section_ = 0;
        view_ = next;
        refilter();
    }
    return true;
}

std::vector<char> LibraryScreen::cinema_buttons() const
{
    const Snapshot &state = ctx_.store.state();
    const LibrarySnapshot &library = state.library;
    if (!state.have_library || !library.ready())
        return state.have_library && !library.busy ? std::vector<char>{'R'} : std::vector<char>{};
    const int collection = cinema_collection();
    if (collection == 4)
        return state.library_storage.drives.empty() ? std::vector<char>{} : std::vector<char>{'S', 'R'};
    const std::vector<int> rows = cinema_rows();
    if (rows.empty())
        return library.games.empty() ? std::vector<char>{} : std::vector<char>{'F'};
    const int row = rows[static_cast<std::size_t>(std::clamp(cn_at_[collection], 0, static_cast<int>(rows.size()) - 1))];
    if (row < 0)
        return {'X', 'D'};
    std::vector<char> actions;
    for (const auto &[key, label] : panorama_actions(library.games[static_cast<std::size_t>(row)]))
        if (key != 'R')
            actions.push_back(key);
    return actions;
}

Intent LibraryScreen::update_cinema(const InputFrame &input, float dt, Feedback &feedback, bool focused)
{
    cn_focused_ = focused;
    Intent intent;
    const Snapshot &state = ctx_.store.state();
    const LibrarySnapshot &library = state.library;
    cn::keyboard_look(keyboard_.style);
    keyboard_.set_bounds(cn::keyboard_keys());
    const int collection = cinema_collection();
    const std::vector<int> rows = cinema_rows();
    const int count = collection == 4 ? static_cast<int>(state.library_storage.drives.size()) : static_cast<int>(rows.size());
    int &at = cn_at_[collection];
    at = std::clamp(at, 0, std::max(0, count - 1));
    const bool band = scan_open_ || (!selected_.empty() && mode_ != 0);
    // The game a question or the details sheet is about; nothing once they close.
    if (!band && !cn_details_ && !selected_.empty())
        selected_.clear();
    const LibraryGame *game = collection < 4 && count > 0 && rows[static_cast<std::size_t>(at)] >= 0
                                  ? &library.games[static_cast<std::size_t>(rows[static_cast<std::size_t>(at)])]
                                  : nullptr;
    const bool job = collection < 4 && count > 0 && rows[static_cast<std::size_t>(at)] < 0;
    // A game's first action: mount or unmount; view-only items show their details.
    const auto primary = [&](const LibraryGame &g)
    {
        if (!g.managed || !library.can(g.mounted ? "unmount_game" : "mount_game"))
        {
            show_game(g.title_id, g.source_key);
            cn_details_ = true;
            feedback.play(hui::audio::Cue::open);
        }
        else if (library.working() || ctx_.store.library_action().busy || library.stale || !g.on_drive)
            refuse(feedback, input);
        else if (!g.mounted)
            run_library("mount", false, &g, {}, 0, feedback);
        else
        {
            show_game(g.title_id, g.source_key);
            mode_ = 1;
            xb_band_ = 0;
            feedback.play(hui::audio::Cue::modal_open);
        }
    };
    if (focused && keyboard_open_)
        update_keyboard(input, feedback);
    else if (focused && band)
        update_band(input, feedback);
    else if (focused && cn_values_)
    {
        // Location: every location the inventory reports.
        std::vector<std::string> values;
        for (const LibraryGame &g : library.games)
            if (!g.location.empty() && std::find(values.begin(), values.end(), g.location) == values.end())
                values.push_back(g.location);
        std::sort(values.begin(), values.end());
        if (input.nav == Direction::up || input.nav == Direction::down)
        {
            const int to = cn_value_ + (input.nav == Direction::down ? 1 : -1);
            if (to >= 0 && to <= static_cast<int>(values.size()))
            {
                cn_value_ = to;
                feedback.play(hui::audio::Cue::focus);
            }
            else
                refuse(feedback, input);
        }
        else if (input.is_pressed(Action::confirm))
        {
            location_ = cn_value_ > 0 ? values[static_cast<std::size_t>(cn_value_ - 1)] : std::string();
            refilter();
            cn_values_ = false;
            feedback.play(hui::audio::Cue::select);
        }
        else if (input.is_pressed(Action::back) || input.nav == Direction::left)
        {
            cn_values_ = false;
            feedback.play(hui::audio::Cue::back);
        }
    }
    else if (focused && cn_sheet_)
    {
        // Options: search, location, then the scan and a refresh.
        const int rows_count = library.can("rescan") ? 4 : 3;
        int row = cn_row_;
        if (input.nav == Direction::up || input.nav == Direction::down)
        {
            const int to = row + (input.nav == Direction::down ? 1 : -1);
            if (to >= 0 && to < rows_count)
            {
                cn_row_ = to;
                feedback.play(hui::audio::Cue::focus);
            }
            else
                refuse(feedback, input);
        }
        else if (input.is_pressed(Action::back) || input.is_pressed(Action::menu))
        {
            cn_sheet_ = false;
            feedback.play(hui::audio::Cue::modal_close);
        }
        else if (input.is_pressed(Action::confirm))
        {
            const int kind = row < 2 ? row : row == 2 && rows_count == 4 ? 2 : 3;
            if (kind == 0)
            {
                keyboard_open_ = true;
                keyboard_.enter();
                keyboard_.set_length(static_cast<int>(query_.size()));
                feedback.play(hui::audio::Cue::modal_open);
            }
            else if (kind == 1)
            {
                cn_values_ = true;
                cn_value_ = 0;
                feedback.play(hui::audio::Cue::modal_open);
            }
            else if (kind == 2)
            {
                cn_sheet_ = false;
                scan_open_ = true;
                xb_band_ = 0;
                error_.clear();
                feedback.play(hui::audio::Cue::modal_open);
            }
            else if (!state.have_library || library.busy)
                refuse(feedback, input);
            else
            {
                ctx_.store.refresh_library();
                feedback.play(hui::audio::Cue::select);
            }
        }
    }
    else if (focused && cn_details_)
    {
        // The details sheet: its actions across.
        const LibraryGame *g = selected_game();
        std::vector<int> actions;
        if (g != nullptr && g->managed && library.can(g->mounted ? "unmount_game" : "mount_game"))
            actions.push_back(0);
        const bool transfer = g != nullptr && g->managed && g->can_manage_source && g->on_drive;
        if (transfer && library.can("copy_game_source") && library.can("storage_job_status"))
            actions.push_back(1);
        if (transfer && library.can("move_game_source") && library.can("storage_job_status"))
            actions.push_back(2);
        if (input.nav == Direction::left || input.nav == Direction::right)
        {
            const int to = panel_index_ + (input.nav == Direction::right ? 1 : -1);
            if (to >= 0 && to < static_cast<int>(actions.size()))
            {
                panel_index_ = to;
                feedback.play(hui::audio::Cue::focus);
            }
            else
                refuse(feedback, input);
        }
        else if (input.is_pressed(Action::back) || input.is_pressed(Action::west))
        {
            cn_details_ = false;
            feedback.play(hui::audio::Cue::modal_close);
        }
        else if (input.is_pressed(Action::confirm) && g != nullptr && !actions.empty())
        {
            const int action = actions[static_cast<std::size_t>(std::clamp(panel_index_, 0, static_cast<int>(actions.size()) - 1))];
            const bool blocked = library.working() || ctx_.store.library_action().busy || library.stale || !g->on_drive;
            if (action == 0)
                primary(*g);
            else if (blocked || g->mounted)
                refuse(feedback, input);
            else
            {
                mode_ = action == 1 ? 2 : 3;
                destination_id_.clear();
                xb_band_ = 0;
                ctx_.store.want_library_storage(true, true);
                feedback.play(hui::audio::Cue::modal_open);
            }
        }
    }
    else if (focused && cn_buttons_)
    {
        const std::vector<char> actions = cinema_buttons();
        cn_button_ = std::clamp(cn_button_, 0, std::max(0, static_cast<int>(actions.size()) - 1));
        if (input.nav == Direction::left || input.nav == Direction::right)
        {
            const int to = cn_button_ + (input.nav == Direction::right ? 1 : -1);
            if (to >= 0 && to < static_cast<int>(actions.size()))
            {
                cn_button_ = to;
                feedback.play(hui::audio::Cue::focus);
            }
            else if (input.nav == Direction::right)
                cn_buttons_ = false;
            else
                refuse(feedback, input);
        }
        else if (input.nav == Direction::up)
            intent.kind = Intent::Kind::top_bar;
        else if (input.nav == Direction::down || input.is_pressed(Action::back))
            cn_buttons_ = false;
        else if (input.is_pressed(Action::confirm) && !actions.empty())
        {
            const char action = actions[static_cast<std::size_t>(cn_button_)];
            if (action == 'M' && game != nullptr)
                primary(*game);
            else if ((action == 'C' || action == 'V') && game != nullptr)
            {
                if (library.working() || ctx_.store.library_action().busy || library.stale || game->mounted || !game->on_drive)
                    refuse(feedback, input);
                else
                {
                    show_game(game->title_id, game->source_key);
                    cn_details_ = true;
                    mode_ = action == 'C' ? 2 : 3;
                    destination_id_.clear();
                    xb_band_ = 0;
                    ctx_.store.want_library_storage(true, true);
                    feedback.play(hui::audio::Cue::modal_open);
                }
            }
            else if (action == 'D')
            {
                const LibraryGame *g = game;
                if (job)
                    for (const LibraryGame &candidate : library.games)
                        if (candidate.title_id == library.storage_job.title_id)
                            g = &candidate;
                if (g != nullptr)
                {
                    show_game(g->title_id, g->source_key);
                    cn_details_ = true;
                    panel_index_ = 0;
                    feedback.play(hui::audio::Cue::open);
                }
                else
                    refuse(feedback, input);
            }
            else if (action == 'R')
            {
                if (collection == 4)
                    ctx_.store.want_library_storage(true, true);
                else
                    ctx_.store.refresh_library();
            }
            else if (action == 'S')
            {
                if (library.working() || ctx_.store.library_action().busy || !library.can("list_games"))
                    refuse(feedback, input);
                else
                    run_library("measure", false, nullptr, {}, 0, feedback);
            }
            else if (action == 'X')
            {
                const StorageJob &j = library.storage_job;
                if (!j.active || !j.cancellable || j.cancel_requested || !library.can("storage_job_cancel") || ctx_.store.library_action().busy)
                    refuse(feedback, input);
                else
                    run_library("cancel", false, nullptr, {}, j.id, feedback);
            }
            else if (action == 'F')
            {
                view_ = 0;
                query_.clear();
                location_.clear();
                format_.clear();
                refilter();
                cn_buttons_ = false;
            }
        }
    }
    else if (focused)
    {
        if (input.nav == Direction::left || input.nav == Direction::right)
        {
            const int to = at + (input.nav == Direction::right ? 1 : -1);
            if (to >= 0 && to < count)
            {
                at = to;
                if (collection == 4)
                    drive_id_ = state.library_storage.drives[static_cast<std::size_t>(to)].id;
                feedback.play(hui::audio::Cue::focus);
            }
            else if (input.nav == Direction::left && !cinema_buttons().empty())
            {
                cn_buttons_ = true;
                cn_button_ = static_cast<int>(cinema_buttons().size()) - 1;
                feedback.play(hui::audio::Cue::focus);
            }
            else
                refuse(feedback, input);
        }
        else if (input.nav == Direction::up)
        {
            if (!cinema_buttons().empty())
            {
                cn_buttons_ = true;
                cn_button_ = 0;
                feedback.play(hui::audio::Cue::focus);
            }
            else
                intent.kind = Intent::Kind::top_bar;
        }
        else if (input.is_pressed(Action::menu) && collection < 4 && state.have_library && library.ready())
        {
            cn_sheet_ = true;
            cn_row_ = 0;
            feedback.play(hui::audio::Cue::modal_open);
        }
        else if (input.is_pressed(Action::north))
        {
            // Triangle refreshes: the library, or the drives in Storage.
            if (collection == 4)
                ctx_.store.want_library_storage(true, true);
            else if (!state.have_library || library.busy)
            {
                refuse(feedback, input);
                return intent;
            }
            else
                ctx_.store.refresh_library();
            feedback.play(hui::audio::Cue::select);
        }
        else if (input.is_pressed(Action::confirm))
        {
            if (!state.have_library || !library.ready())
            {
                // Not answering: Cross refreshes.
                if (library.busy)
                    refuse(feedback, input);
                else
                    ctx_.store.refresh_library();
            }
            else if (collection == 4)
            {
                if (count == 0 || library.working() || ctx_.store.library_action().busy || !library.can("list_games"))
                    refuse(feedback, input);
                else
                    run_library("measure", false, nullptr, {}, 0, feedback);
            }
            else if (job)
            {
                const StorageJob &j = library.storage_job;
                if (!j.active || !j.cancellable || j.cancel_requested || !library.can("storage_job_cancel") ||
                    ctx_.store.library_action().busy)
                    refuse(feedback, input);
                else
                    run_library("cancel", false, nullptr, {}, j.id, feedback);
            }
            else if (game != nullptr)
                primary(*game);
            else if (count == 0 && !library.games.empty())
            {
                // Nothing matches: Reset library filters.
                view_ = 0;
                query_.clear();
                location_.clear();
                format_.clear();
                refilter();
                feedback.play(hui::audio::Cue::toggle);
            }
            else
                refuse(feedback, input);
        }
        else if (input.is_pressed(Action::west))
        {
            // Square: the focused game's details (the operation's game, for a copy or move).
            const LibraryGame *g = game;
            if (job)
                for (const LibraryGame &candidate : library.games)
                    if (candidate.title_id == library.storage_job.title_id && g == nullptr)
                        g = &candidate;
            if (g == nullptr)
                refuse(feedback, input);
            else
            {
                show_game(g->title_id, g->source_key);
                cn_details_ = true;
                panel_index_ = 0;
                feedback.play(hui::audio::Cue::open);
            }
        }
    }
    keyboard_.set_active(keyboard_open_);
    keyboard_.update(dt);
    keyboard_in_.target = keyboard_open_ ? 1.0f : 0.0f;
    keyboard_in_.update(dt, 14.0f);
    xb_band_in_.target = scan_open_ || (!selected_.empty() && mode_ != 0) ? 1.0f : 0.0f;
    xb_band_in_.update(dt, 16.0f);
    cn_sheet_in_.target = cn_sheet_ || cn_details_ ? 1.0f : 0.0f;
    cn_sheet_in_.update(dt, 16.0f);
    return intent;
}

void LibraryScreen::draw_cinema(Frame &frame) const
{
    hui::gfx::DrawList &list = frame.scene;
    const Snapshot &state = ctx_.store.state();
    const LibrarySnapshot &library = state.library;
    const ui::Type &type = ctx_.type;
    static constexpr const char *kNames[] = {/* i18n */ "All games", /* i18n */ "Installed", /* i18n */ "On drive",
                                             /* i18n */ "Unavailable", /* i18n */ "Storage"};
    const int collection = cinema_collection();
    const std::vector<int> rows = cinema_rows();
    const std::vector<LibraryDrive> &drives = state.library_storage.drives;
    const int count = collection == 4 ? static_cast<int>(drives.size()) : static_cast<int>(rows.size());
    const int at = std::clamp(cn_at_[collection], 0, std::max(0, count - 1));
    if (keyboard_open_)
    {
        cn::backdrop(ctx_, list, nullptr, cn::Veil::dark);
        cn::draw_keyboard(ctx_, frame, keyboard_, tr("Search your library"), query_, tr("Search your library"), false);
        return;
    }
    const LibraryGame *game = collection < 4 && count > 0 && rows[static_cast<std::size_t>(at)] >= 0
                                  ? &library.games[static_cast<std::size_t>(rows[static_cast<std::size_t>(at)])]
                                  : nullptr;
    const bool job = collection < 4 && count > 0 && rows[static_cast<std::size_t>(at)] < 0;
    const auto art = [&](const LibraryGame &g) -> const Game *
    {
        for (const Game &candidate : state.games)
            for (const Release &r : candidate.releases)
                if (r.title_id == g.title_id)
                    return &candidate;
        return nullptr;
    };
    const Game *backdrop = game != nullptr ? art(*game) : nullptr;
    if (job)
        for (const LibraryGame &g : library.games)
            if (g.title_id == library.storage_job.title_id)
                backdrop = art(g);
    cn::backdrop(ctx_, list, backdrop, backdrop != nullptr ? cn::Veil::hero : cn::Veil::dark);
    cn::collection(ctx_, list, tr(kNames[collection]), collection, 5);
    // The stage.
    cn::Hero hero;
    hero.y = 380.0f;
    hero.size = 124.0f;
    float bar = -1.0f;
    if (!state.have_library || !library.ready())
    {
        const bool busy = !state.have_library || library.busy;
        hero.kicker = busy ? tr("Library") : tr("Connect your library");
        hero.title = busy ? tr("Finding your games") : tr("Connect your library");
        hero.body = busy ? tr("Reading installed titles and drive availability from ShadowMount.")
                         : std::string(tr("Library uses ShadowMount\xE2\x80\x99s local games API on your PS5. "
                                          "Start a version with that API enabled, then refresh here.")) +
                               " " +
                               tr("Games are not inferred from download history. An unavailable library "
                                  "does not mean your games are missing.");
        if (!busy)
            hero.buttons = {cn::button(tr("Refresh library"), icons::Icon::refresh, true)};
        else
            cn::spinner(ctx_, list, 122.0f, 330.0f, 44.0f, 4.0f);
    }
    else if (collection == 4)
    {
        if (drives.empty())
        {
            hero.kicker = tr("Storage");
            hero.title = !state.library_storage.error.empty() ? std::string(tr(std::string_view(state.library_storage.error)))
                         : state.have_library_storage         ? std::string(tr("No drives reported."))
                                                              : std::string(tr("Checking your drives\xE2\x80\xA6"));
            hero.body = tr("Capacity as ShadowMount reports it. Select a drive to see its games.");
        }
        else
        {
            // A drive: its path and use in gold, its name, its games' sizes.
            const LibraryDrive &d = drives[static_cast<std::size_t>(at)];
            hero.kicker = d.path + " \xC2\xB7 " + tr("{used} used of {total}", {{"used", format_bytes(d.used)}, {"total", format_bytes(d.total)}});
            hero.title = i18n::place(d.label);
            std::string games;
            for (const LibraryGame *g : drive_games())
                games += (games.empty() ? "" : ", ") + g->title + " (" +
                         (g->size >= 0.0 ? format_bytes(g->size)
                          : g->size_status == "unavailable" ? std::string(tr("Unavailable"))
                                                            : std::string(tr("Not measured"))) +
                         ")";
            hero.body = (games.empty() ? std::string() : tr("Games on {drive}", {{"drive", i18n::place(d.label)}}) + ": " + games + ". ") +
                        tr("Sizes are measured on request. Shared source files can appear under "
                           "more than one title, so these sizes are not a drive total.");
            bar = d.total > 0.0 ? static_cast<float>(std::clamp(d.used / d.total, 0.0, 1.0)) : 0.0f;
            const bool measuring = library.has_action && library.action.action == "measure" && library.working();
            cn::Button measure = cn::button(measuring ? tr("Measuring\xE2\x80\xA6") : tr("Measure game sizes"), icons::Icon::drive, true);
            measure.focus = true;
            measure.enabled = library.can("list_games") && !library.working();
            hero.buttons = {measure, cn::button(tr("Refresh drives"), icons::Icon::refresh)};
        }
    }
    else if (job)
    {
        // A copy or move under way takes the stage.
        const StorageJob &j = library.storage_job;
        std::string title = j.title_id;
        for (const LibraryGame &g : library.games)
            if (g.title_id == j.title_id)
                title = g.title;
        hero.kicker = std::string(j.operation == "move" ? tr("Move") : tr("Copy")) + (j.destination.empty() ? "" : " \xC2\xB7 " + tr("To {path}", {{"path", j.destination}}));
        hero.title = title;
        const float progress = j.state == "completed" ? 1.0f : j.total > 0.0 ? static_cast<float>(std::min(1.0, j.processed / j.total)) : 0.0f;
        hero.body = (j.total > 0.0 ? tr("{done} of {total}", {{"done", format_bytes(j.processed)}, {"total", format_bytes(j.total)}}) + " \xC2\xB7 " : std::string()) +
                    i18n::whole_percent(progress * 100.0f) + (j.cancel_requested ? " \xC2\xB7 " + std::string(tr("Cancelling\xE2\x80\xA6")) : std::string()) +
                    (j.error.empty() ? std::string() : " \xC2\xB7 " + std::string(tr(std::string_view(j.error))));
        hero.warn = !j.error.empty();
        bar = progress;
        cn::Button cancel = cn::button(tr("Cancel operation"), icons::Icon::stop, true);
        cancel.focus = true;
        cancel.enabled = j.active && j.cancellable && !j.cancel_requested && library.can("storage_job_cancel");
        hero.buttons = {cancel, cn::button(tr("Details"), icons::Icon::info)};
    }
    else if (game == nullptr)
    {
        hero.kicker = query_.empty() ? std::string(tr(kNames[collection])) : "\xE2\x80\x9C" + query_ + "\xE2\x80\x9D";
        hero.title = library.games.empty() ? tr("No games reported yet") : tr("No matching games");
        hero.body = library.games.empty() ? tr("ShadowMount has not reported any installed or on-drive games. "
                                               "Refresh after its inventory updates.")
                                          : tr("Try a different title, status or location.");
        if (!library.games.empty())
        {
            hero.buttons = {cn::button(tr("Reset library filters"), icons::Icon::refresh, true)};
            hero.buttons.front().focus = true;
        }
    }
    else
    {
        // A game: its status and place in gold, its title, and its actions.
        const std::string status = !game->on_drive ? tr("Unavailable") : game->mounted ? tr("Mounted") : game->installed ? tr("Installed") : tr("On drive");
        hero.kicker = status + " \xC2\xB7 " + (game->on_drive ? i18n::place(game->location) : std::string(tr("Drive disconnected")));
        if (library.stale)
        {
            hero.kicker = library.updated_at > 0.0 ? tr("Saved snapshot \xC2\xB7 Last refreshed {time}", {{"time", format_age(library.updated_at, unix_seconds())}})
                                                   : std::string(tr("Saved snapshot \xC2\xB7 Not refreshed yet"));
            hero.warn = true;
            hero.body = tr("Showing the last successful result. Game and drive status may have changed.");
        }
        else if (!game->managed)
            hero.body = tr("This item supports viewing details only. ShadowMount does not manage "
                           "its source.");
        hero.title = game->title;
        const bool transfer = game->managed && game->can_manage_source && game->on_drive;
        if (game->managed && library.can(game->mounted ? "unmount_game" : "mount_game"))
        {
            cn::Button mount = cn::button(game->mounted ? tr("Unmount game") : tr("Mount game"), game->mounted ? icons::Icon::eject : icons::Icon::play, true);
            mount.focus = true;
            hero.buttons.push_back(mount);
        }
        if (transfer && library.can("copy_game_source") && library.can("storage_job_status"))
            hero.buttons.push_back(cn::button(tr("Copy to drive"), icons::Icon::copy));
        if (transfer && library.can("move_game_source") && library.can("storage_job_status"))
            hero.buttons.push_back(cn::button(tr("Move to drive"), icons::Icon::move));
        cn::Button details = cn::button(tr("Details"), icons::Icon::info, hero.buttons.empty());
        details.focus = hero.buttons.empty();
        hero.buttons.push_back(details);
    }
    for (std::size_t i = 0; i < hero.buttons.size(); ++i)
        hero.buttons[i].focus = cn_focused_ && cn_buttons_ && static_cast<int>(i) == cn_button_;
    if (cn_details_ || cn_sheet_)
        hero.buttons.clear();
    if (cn_details_ && selected_game() != nullptr)
    {
        // Details: the title moves up over the sheet, as a game page's does.
        const LibraryGame &g = *selected_game();
        cn::kick(ctx_, list, 100.0f, 150.0f,
                 (g.platform == "ps5" ? std::string("PS5") : g.platform == "ps4" ? std::string("PS4") : std::string(tr("Platform unknown"))) +
                     " \xC2\xB7 " + g.title_id,
                 20.0f);
        const auto &title_font = ui::face(type.cinema, type.semibold, g.title);
        const float size = ui::fit_size(title_font, g.title, 112.0f, 72.0f, 1720.0f);
        ui::text(list, title_font, g.title, 100.0f, 150.0f + 32.0f + 12.0f + size * 0.82f, size, cn::kInk);
        hero.kicker.clear();
        hero.title.clear();
        hero.body.clear();
    }
    const float buttons_y = hero.title.empty() ? 0.0f : cn::draw_hero(ctx_, list, hero);
    if (bar >= 0.0f && !hero.title.empty())
    {
        list.rounded_rect({100.0f, buttons_y - 34.0f + 8.0f, 820.0f, 8.0f}, 4.0f, ui::Color::rgb(0xf7f3ec, 0.18f));
        list.rounded_rect({100.0f, buttons_y - 34.0f + 8.0f, 820.0f * bar, 8.0f}, 4.0f, cn::kGold);
    }
    // The strip: the view's games (or the drives) with their status.
    if (count > 0 && library.ready())
    {
        std::vector<cn::Still> stills;
        if (collection == 4)
            for (const LibraryDrive &d : drives)
            {
                cn::Still still;
                still.icon = icons::Icon::drive;
                still.title = i18n::place(d.label);
                still.sub = tr("{size} free", {{"size", format_bytes(d.free)}});
                stills.push_back(std::move(still));
            }
        else
            for (int r : rows)
            {
                cn::Still still;
                if (r < 0)
                {
                    const StorageJob &j = library.storage_job;
                    still.icon = j.operation == "move" ? icons::Icon::move : icons::Icon::copy;
                    still.title = j.operation == "move" ? tr("Move") : tr("Copy");
                    still.progress = j.total > 0.0 ? static_cast<float>(std::min(1.0, j.processed / j.total)) : 0.0f;
                }
                else
                {
                    const LibraryGame &g = library.games[static_cast<std::size_t>(r)];
                    still.game = art(g);
                    still.title = g.title;
                    still.sub = g.installed ? tr("Installed") : std::string();
                    still.badge = !g.on_drive ? tr("Unavailable") : g.mounted ? tr("Mounted") : g.installed ? tr("Installed") : tr("On drive");
                    still.dim = !g.on_drive;
                }
                stills.push_back(std::move(still));
            }
        cn::Strip strip;
        strip.focused = cn_focused_ && !cn_buttons_ && !cn_sheet_ && !cn_details_;
        strip.label = tr("{n} of {count}", {{"n", i18n::count(at + 1)}, {"count", i18n::count(count)}});
        cn::draw_strip(ctx_, list, stills, at, strip);
    }
    hui::gfx::DrawList &over = frame.overlay;
    if (cn_sheet_in_.value > 0.01f && cn_sheet_)
    {
        // Options: search and location, the scan and a refresh; the locations beside them.
        over.push_opacity(cn_sheet_in_.value);
        const float top = cn::draw_sheet(over, 560.0f) + 44.0f;
        std::vector<cn::Row> sheet_rows;
        cn::Row search;
        search.icon = icons::Icon::search;
        search.title = tr("Search your library");
        search.value = query_.empty() ? std::string() : "\xE2\x80\x9C" + query_ + "\xE2\x80\x9D";
        sheet_rows.push_back(search);
        cn::Row location;
        location.icon = icons::Icon::drive;
        location.title = tr("Location");
        location.value = location_.empty() ? std::string(tr("All locations")) : i18n::place(location_);
        sheet_rows.push_back(location);
        if (library.can("rescan"))
        {
            cn::Row scan;
            scan.icon = icons::Icon::scan;
            scan.title = tr("Scan for games");
            scan.sub = tr("Search your drives with ShadowMount");
            scan.off = library.working();
            sheet_rows.push_back(scan);
        }
        cn::Row refresh;
        refresh.icon = icons::Icon::refresh;
        refresh.title = library.busy ? tr("Refreshing\xE2\x80\xA6") : tr("Refresh library");
        sheet_rows.push_back(refresh);
        cn::kick(ctx_, over, 100.0f, top, tr("Library"));
        cn::draw_rows(ctx_, &over, 100.0f, top + 25.6f + 10.0f, 700.0f, sheet_rows, cn_values_ ? 1 : cn_row_);
        if (cn_values_)
        {
            std::vector<std::string> values;
            for (const LibraryGame &g : library.games)
                if (!g.location.empty() && std::find(values.begin(), values.end(), g.location) == values.end())
                    values.push_back(g.location);
            std::sort(values.begin(), values.end());
            std::vector<cn::Card> cards;
            cn::Card all;
            all.title = tr("All locations");
            all.chosen = location_.empty();
            all.focus = cn_value_ == 0;
            cards.push_back(all);
            for (int i = 0; i < static_cast<int>(values.size()); ++i)
            {
                cn::Card card;
                card.title = i18n::place(values[static_cast<std::size_t>(i)]);
                const long n = std::count_if(library.games.begin(), library.games.end(),
                                             [&](const LibraryGame &g) { return g.location == values[static_cast<std::size_t>(i)]; });
                card.sub = trn(n, "{count} game", "{count} games");
                card.chosen = values[static_cast<std::size_t>(i)] == location_;
                card.focus = cn_value_ == i + 1;
                cards.push_back(std::move(card));
            }
            cn::kick(ctx_, over, 860.0f, top, tr("Location"));
            cn::draw_cards(ctx_, &over, 860.0f, top + 25.6f + 18.0f, 960.0f, cards, 2);
        }
        over.pop_opacity();
    }
    if (cn_sheet_in_.value > 0.01f && cn_details_)
    {
        // The game's statuses, and what can be done with it.
        const LibraryGame *g = selected_game();
        over.push_opacity(cn_sheet_in_.value);
        const float top = cn::draw_sheet(over, 640.0f) + 56.0f;
        if (g != nullptr)
        {
            const std::vector<std::pair<std::string, std::string>> facts = {
                {tr("Installation"), g->installed ? tr("Registered on the console") : tr("Not installed on the console")},
                {tr("Mount status"), g->mounted ? tr("Mounted") : tr("Not mounted")},
                {tr("Source"), g->on_drive ? tr("Available on drive") : tr("Unavailable; the drive or source may be missing")},
                {tr("Format"), g->format},
                {tr("Location"), i18n::place(g->location)},
                {tr("Source size"), g->size >= 0.0 ? format_bytes(g->size)
                                    : g->size_status == "unavailable" ? std::string(tr("Unavailable"))
                                                                      : std::string(tr("Use Measure game sizes in Storage"))},
                {tr("Size reported by console"), g->installed_size < 0.0 ? std::string(tr("Not reported")) : format_bytes(g->installed_size)}};
            cn::kick(ctx_, over, 100.0f, top, tr("Library game details"));
            float y = top + 25.6f + 16.0f;
            const float column = (1100.0f - 60.0f) / 3.0f;
            for (std::size_t i = 0; i < facts.size(); ++i)
            {
                const float cx = 100.0f + static_cast<float>(i % 3) * (column + 30.0f);
                const float cy = y + static_cast<float>(i / 3) * 78.0f;
                cn::kick(ctx_, over, cx, cy, facts[i].first, 13.0f);
                ui::text_fit(over, type.regular, facts[i].second, cx, cy + 20.8f + 6.0f + 21.0f, 21.0f, column, cn::kInk);
            }
            y += 78.0f * static_cast<float>((facts.size() + 2) / 3);
            cn::kick(ctx_, over, 100.0f, y + 8.0f, tr("Source path"), 13.0f);
            ui::text_fit(over, type.regular, g->path.empty() ? std::string(tr("Not reported")) : g->path, 100.0f, y + 8.0f + 20.8f + 6.0f + 21.0f, 21.0f, 1100.0f,
                         cn::kInk);
            ui::paragraph(over, type.regular,
                          tr("Installed means registered on the console. Mounted means its source is "
                             "currently mounted. On drive means the source is available. These "
                             "statuses do not confirm that a game will launch."),
                          1300.0f, top + 17.0f, 17.0f, 520.0f, 25.5f, cn::kFaint, 6);
            if (g->managed && g->can_manage_source && g->on_drive && g->mounted)
                ui::paragraph(over, type.regular, tr("Unmount this game before copying or moving its source."), 1300.0f, top + 200.0f, 19.0f,
                              520.0f, 28.5f, cn::kWarn, 3);
            // Its actions.
            std::vector<cn::Button> buttons;
            const bool transfer = g->managed && g->can_manage_source && g->on_drive;
            if (g->managed && library.can(g->mounted ? "unmount_game" : "mount_game"))
                buttons.push_back(cn::button(g->mounted ? tr("Unmount game") : tr("Mount game"), g->mounted ? icons::Icon::eject : icons::Icon::play, true));
            if (transfer && library.can("copy_game_source") && library.can("storage_job_status"))
                buttons.push_back(cn::button(tr("Copy to drive"), icons::Icon::copy));
            if (transfer && library.can("move_game_source") && library.can("storage_job_status"))
                buttons.push_back(cn::button(tr("Move to drive"), icons::Icon::move));
            for (int i = 0; i < static_cast<int>(buttons.size()); ++i)
            {
                buttons[static_cast<std::size_t>(i)].focus = i == panel_index_;
                buttons[static_cast<std::size_t>(i)].enabled = !(i > 0 && g->mounted);
            }
            cn::draw_buttons(ctx_, over, 100.0f, y + 8.0f + 20.8f + 6.0f + 40.0f + 30.0f, buttons);
            std::vector<std::string> notes;
            if (library.has_action && library.action.title_id == g->title_id && !library.action.message.empty())
                notes.push_back(std::string(tr(std::string_view(library.action.message))));
            if (!error_.empty() && mode_ == 0)
                notes.push_back(std::string(tr(std::string_view(error_))));
            if (!notes.empty())
                ui::text_fit(over, type.regular, notes.front(), 100.0f, 1080.0f - 90.0f, 19.0f, 1100.0f, cn::kWarn);
        }
        over.pop_opacity();
    }
    // A question: Store's words as a title card.
    if (xb_band_in_.value > 0.01f)
    {
        const xb::Band band = crossbar_band();
        cn::Title question;
        question.kicker = band.kicker;
        question.title = band.title;
        question.body = band.body + (band.note.empty() ? std::string() : " " + band.note);
        question.small = band.error;
        question.spin = band.busy;
        for (int i = 0; i < static_cast<int>(band.options.size()); ++i)
        {
            cn::Option option;
            option.label = band.options[static_cast<std::size_t>(i)].label;
            option.checked = band.options[static_cast<std::size_t>(i)].checked;
            option.focus = i == xb_band_;
            question.options.push_back(std::move(option));
        }
        for (int i = 0; i < static_cast<int>(band.buttons.size()); ++i)
        {
            cn::Button b;
            b.label = band.buttons[static_cast<std::size_t>(i)];
            b.primary = i == 0;
            b.focus = static_cast<int>(band.options.size()) + i == xb_band_;
            question.buttons.push_back(std::move(b));
        }
        cn::draw_title(ctx_, over, question, xb_band_in_.value);
    }
}

std::vector<hui::ui::Hint> LibraryScreen::cinema_hints() const
{
    const Snapshot &state = ctx_.store.state();
    const LibrarySnapshot &library = state.library;
    if (keyboard_open_)
        return {{hui::ui::Button::square, tr("Delete")}, {hui::ui::Button::triangle, tr("Space")}, {hui::ui::Button::circle, tr("Close")}};
    if (scan_open_ || (!selected_.empty() && mode_ != 0))
        return {{hui::ui::Button::cross, tr("Select")}, {hui::ui::Button::circle, tr("Back")}};
    if (cn_values_)
        return {{hui::ui::Button::cross, tr("Choose")}, {hui::ui::Button::circle, tr("Back")}};
    if (cn_sheet_ || cn_details_)
        return {{hui::ui::Button::cross, tr("Select")}, {hui::ui::Button::circle, tr("Close")}};
    if (!state.have_library || !library.ready())
        return {{hui::ui::Button::cross, tr("Refresh library")}};
    std::vector<hui::ui::Hint> hints{{hui::ui::Button::cross, tr("Select")}};
    if (cinema_collection() < 4)
    {
        hints.push_back({hui::ui::Button::square, tr("Details")});
        hints.push_back({hui::ui::Button::triangle, tr("Refresh library")});
        hints.push_back({hui::ui::Button::options, tr("Search your library")});
    }
    else
        hints.push_back({hui::ui::Button::triangle, tr("Refresh drives")});
    hints.push_back({hui::ui::Button::l1, tr("Collections"), hui::ui::Button::r1});
    return hints;
}

// ---------------------------------------------------------------- Panorama

const LibraryGame *LibraryScreen::panorama_game() const
{
    // The game chosen last in the games group, else the first one shown.
    const LibrarySnapshot &library = ctx_.store.state().library;
    for (const LibraryGame &g : library.games)
        if (game_key(g) == pn_game_)
            return &g;
    return rows_.empty() ? nullptr : &library.games[static_cast<std::size_t>(rows_.front())];
}

std::vector<std::pair<char, std::string>> LibraryScreen::panorama_actions(const LibraryGame &g) const
{
    // A game's own group: mount or unmount, copy, move, Details, Refresh library;
    // an item ShadowMount doesn't manage has Details only.
    const LibrarySnapshot &library = ctx_.store.state().library;
    std::vector<std::pair<char, std::string>> out;
    const bool transfer = g.managed && g.can_manage_source && g.on_drive;
    if (g.managed && library.can(g.mounted ? "unmount_game" : "mount_game"))
        out.emplace_back('M', g.mounted ? tr("Unmount game") : tr("Mount game"));
    if (transfer && library.can("copy_game_source") && library.can("storage_job_status"))
        out.emplace_back('C', tr("Copy to drive"));
    if (transfer && library.can("move_game_source") && library.can("storage_job_status"))
        out.emplace_back('V', tr("Move to drive"));
    out.emplace_back('D', tr("Details"));
    if (out.size() > 1)
        out.emplace_back('R', tr("Refresh library"));
    return out;
}

pano::Page LibraryScreen::panorama_page() const
{
    pano::Page page;
    const Snapshot &state = ctx_.store.state();
    const LibrarySnapshot &library = state.library;
    if (!state.have_library || !library.ready() || rows_.empty())
        return page;
    const bool blocked = library.working() || ctx_.store.library_action().busy || library.stale;

    // The games, then Search your library.
    pano::Group games;
    games.title = !query_.empty()    ? "\xE2\x80\x9C" + query_ + "\xE2\x80\x9D"
                  : !location_.empty() ? pano::lower(i18n::place(location_))
                                       : pano::lower(tr("All games"));
    games.count = i18n::count(static_cast<long>(rows_.size()));
    games.pattern = pano::Pattern::lead;
    games.size = static_cast<int>(rows_.size()) + 1;
    games.fill = [this](int i, pano::Tile &tile)
    {
        const Snapshot &now = ctx_.store.state();
        if (i >= static_cast<int>(rows_.size()))
        {
            tile.key = "search";
            tile.has_icon = true;
            tile.icon = icons::Icon::search;
            tile.label = tr("Search your library");
            return;
        }
        const LibraryGame &g = now.library.games[static_cast<std::size_t>(rows_[static_cast<std::size_t>(i)])];
        tile.key = game_key(g);
        tile.game = art_of(now, g.title_id);
        tile.name_on_focus = tile.game != nullptr;
        tile.label = g.title;
        tile.sub = status_of(g) + " \xC2\xB7 " + (g.on_drive ? i18n::place(g.location) : std::string(tr("Drive disconnected")));
        if (tile.game == nullptr)
        {
            // An item without catalogue art: a warm tile with its box.
            tile.has_bg = true;
            tile.bg = ui::Color::rgb(0x3a3326);
            tile.has_icon = true;
            tile.icon = icons::Icon::box;
        }
        tile.off = !g.on_drive;
        if (i == 0)
            tile.label_size = 30.0f;
    };
    page.groups.push_back(std::move(games));

    // The selected game's own group, or the copy or move under way.
    const LibraryGame *game = panorama_game();
    if (library.has_storage_job && library.storage_job.id > 0)
    {
        const StorageJob &j = library.storage_job;
        const LibraryGame *of = nullptr;
        for (const LibraryGame &g : library.games)
            if (g.title_id == j.title_id)
                of = &g;
        pano::Group job;
        job.title = pano::lower(of != nullptr ? of->title : j.title_id);
        job.pattern = pano::Pattern::custom;
        job.places = {{0, 0, 2, 2}, {0, 2, 1, 1}, {1, 2, 1, 1}};
        job.size = of != nullptr ? 3 : 2;
        const bool can_cancel = j.active && j.cancellable && !j.cancel_requested && library.can("storage_job_cancel") &&
                                !ctx_.store.library_action().busy;
        job.fill = [this, can_cancel](int i, pano::Tile &tile)
        {
            const Snapshot &now = ctx_.store.state();
            const StorageJob &jj = now.library.storage_job;
            if (i == 1 || i == 2)
            {
                tile.key = i == 1 ? "job:cancel" : "job:details";
                tile.has_icon = true;
                tile.icon = i == 1 ? icons::Icon::stop : icons::Icon::info;
                tile.label = i == 1 ? tr("Cancel operation") : tr("Details");
                if (i == 1)
                {
                    tile.has_bg = true;
                    tile.bg = look::accent_color();
                    tile.off = !can_cancel;
                }
                return;
            }
            tile.key = "job";
            tile.game = art_of(now, jj.title_id);
            tile.dim = 0.3f;
            tile.label_size = 26.0f;
            tile.kicker = std::string(jj.operation == "move" ? tr("Move") : tr("Copy")) +
                          (jj.destination.empty() ? "" : " \xC2\xB7 " + tr("To {path}", {{"path", jj.destination}}));
            for (const LibraryGame &g : now.library.games)
                if (g.title_id == jj.title_id)
                    tile.label = g.title;
            const float progress = jj.state == "completed" ? 1.0f : jj.total > 0.0 ? static_cast<float>(std::min(1.0, jj.processed / jj.total)) : 0.0f;
            tile.number = i18n::whole_percent(progress * 100.0f);
            tile.number_size = 110.0f;
            tile.sub = (jj.total > 0.0 ? tr("{done} of {total}", {{"done", format_bytes(jj.processed)}, {"total", format_bytes(jj.total)}}) : std::string()) +
                       (jj.cancel_requested ? " \xC2\xB7 " + std::string(tr("Cancelling\xE2\x80\xA6")) : std::string()) +
                       (jj.error.empty() ? std::string() : " \xC2\xB7 " + std::string(tr(std::string_view(jj.error))));
            tile.bar = progress;
            tile.bar_bad = !jj.error.empty();
        };
        page.groups.push_back(std::move(job));
    }
    else if (game != nullptr)
    {
        const std::vector<std::pair<char, std::string>> actions = panorama_actions(*game);
        pano::Group g;
        g.title = pano::lower(game->title);
        g.pattern = pano::Pattern::custom;
        // The first action wide in the accent; the rest under it, two by two.
        g.places = {{0, 0, 2, 1}, {0, 1, 1, 1}, {1, 1, 1, 1}, {0, 2, 1, 1}, {1, 2, 1, 1}};
        // With details open, only the game's own actions stay.
        int shown = static_cast<int>(actions.size());
        if (pn_details_)
            shown = static_cast<int>(std::count_if(actions.begin(), actions.end(), [](const auto &a) { return a.first != 'D' && a.first != 'R'; }));
        g.size = std::max(1, std::min(shown, 5));
        const std::string status = (game->platform == "ps5" ? std::string("PS5") : game->platform == "ps4" ? std::string("PS4") : std::string(tr("Platform unknown"))) +
                                   " \xC2\xB7 " + status_of(*game) + " \xC2\xB7 " + game->title_id;
        const std::string line = status_of_line(*game);
        const bool mounted = game->mounted, on_drive = game->on_drive, managed = game->managed;
        const bool stale = library.stale;
        g.fill = [actions, status, line, blocked, mounted, on_drive, stale](int i, pano::Tile &tile)
        {
            if (i >= static_cast<int>(actions.size()))
                return;
            const char kind = actions[static_cast<std::size_t>(i)].first;
            tile.key = std::string("act:") + kind;
            tile.has_icon = true;
            tile.label = actions[static_cast<std::size_t>(i)].second;
            tile.icon = kind == 'M'   ? (mounted ? icons::Icon::eject : icons::Icon::play)
                        : kind == 'C' ? icons::Icon::copy
                        : kind == 'V' ? icons::Icon::move
                        : kind == 'D' ? icons::Icon::info
                                      : icons::Icon::refresh;
            if (i == 0)
            {
                // The first: what it does to this game, and where the game is.
                tile.sub = stale ? std::string(tr("Showing the last successful result. Game and drive status may have changed."))
                           : kind == 'D' ? status
                                         : line;
                tile.has_bg = !stale;
                tile.bg = look::accent_color();
            }
            if (kind == 'R' && stale)
            {
                tile.has_bg = true;
                tile.bg = look::accent_color();
            }
            tile.off = (kind == 'M' && (blocked || !on_drive)) || ((kind == 'C' || kind == 'V') && (blocked || mounted));
        };
        if (!managed)
        {
            g.below_rows = 2;
            g.cols = 4;
            g.below = [this](hui::gfx::DrawList &list, float x, float top, float width)
            {
                pano::draw_text(ctx_, &list, x, top, width, tr("This item supports viewing details only. ShadowMount does not manage its source."),
                                19.0f, ui::Color::rgb(0xffffff, 0.8f), pano::Face::body, 1.55f, 4);
            };
        }
        page.groups.push_back(std::move(g));
    }

    if (pn_details_ && game != nullptr)
    {
        // The statuses, where the game lives (the one being copied or moved,
        // while that runs), its path and what the words mean.
        const LibraryGame copy = *game;
        pano::Group details;
        details.title = pano::lower(tr("Library game details"));
        details.cols = 4;
        details.below_rows = pano::kRows;
        details.below = [this, copy](hui::gfx::DrawList &list, float x, float top, float width)
        {
            const LibraryGame &gg = copy;
            const std::vector<std::pair<std::string, std::string>> status = {
                {tr("Installation"), gg.installed ? tr("Registered on the console") : tr("Not installed on the console")},
                {tr("Mount status"), gg.mounted ? tr("Mounted") : tr("Not mounted")},
                {tr("Source"), gg.on_drive ? tr("Available on drive") : tr("Unavailable; the drive or source may be missing")},
                {tr("Format"), gg.format}};
            const std::vector<std::pair<std::string, std::string>> where = {
                {tr("Location"), i18n::place(gg.location)},
                {tr("Source size"), gg.size >= 0.0 ? format_bytes(gg.size)
                                    : gg.size_status == "unavailable" ? std::string(tr("Unavailable"))
                                                                      : std::string(tr("Use Measure game sizes in Storage"))},
                {tr("Size reported by console"), gg.installed_size < 0.0 ? std::string(tr("Not reported")) : format_bytes(gg.installed_size)}};
            float y = top - 10.0f;
            y += pano::draw_caps(ctx_, &list, x, y, tr("Status"));
            y += pano::draw_pairs(ctx_, &list, x, y - 16.0f, width, status, 4) - 16.0f + 26.0f;
            y += pano::draw_caps(ctx_, &list, x, y, tr("Where it lives"));
            y += pano::draw_pairs(ctx_, &list, x, y - 16.0f, width, where, 4) - 16.0f + 20.0f;
            y += pano::draw_text(ctx_, &list, x, y, width, tr("Source path"), 15.0f, pano::kFaint);
            y += 4.0f + pano::draw_text(ctx_, &list, x, y + 4.0f, width, gg.path.empty() ? std::string(tr("Not reported")) : gg.path, 20.0f,
                                        pano::kInk, pano::Face::medium, 1.6f, 2);
            pano::draw_text(ctx_, &list, x, y + 18.0f, width,
                            tr("Installed means registered on the console. Mounted means its source is currently mounted. On drive means "
                               "the source is available. These statuses do not confirm that a game will launch."),
                            17.0f, pano::kFaint, pano::Face::body, 1.5f, 4);
        };
        page.groups.push_back(std::move(details));
    }

    // Storage: the drives with their space, the scan and the sizes.
    pano::Group storage;
    storage.title = pano::lower(tr("Storage"));
    storage.pattern = pano::Pattern::custom;
    const std::vector<LibraryDrive> &drives = state.library_storage.drives;
    std::vector<int> tools; // 0 scan, 1 measure
    if (library.can("rescan"))
        tools.push_back(0);
    tools.push_back(1);
    const int nd = static_cast<int>(drives.size());
    for (int d = 0; d < nd; ++d)
        storage.places.push_back({2 * (d / 2), d % 2, 2, 1});
    for (int t = 0; t < static_cast<int>(tools.size()); ++t)
        storage.places.push_back({t, 2, 1, 1});
    storage.size = nd + static_cast<int>(tools.size());
    storage.fill = [this, tools, nd](int i, pano::Tile &tile)
    {
        const Snapshot &now = ctx_.store.state();
        const LibrarySnapshot &lib = now.library;
        if (i < nd)
        {
            const LibraryDrive &d = now.library_storage.drives[static_cast<std::size_t>(i)];
            tile.key = "drive:" + d.id;
            tile.has_icon = true;
            tile.icon = icons::Icon::drive;
            tile.label = i18n::place(d.label);
            tile.sub = tr("{size} free", {{"size", format_bytes(d.free)}}) + " \xC2\xB7 " +
                       tr("{used} used of {total}", {{"used", format_bytes(d.used)}, {"total", format_bytes(d.total)}});
            tile.bar = d.total > 0.0 ? static_cast<float>(std::clamp(d.used / d.total, 0.0, 1.0)) : 0.0f;
            return;
        }
        const int tool = tools[static_cast<std::size_t>(i - nd)];
        tile.has_icon = true;
        if (tool == 0)
        {
            tile.key = "scan";
            tile.icon = icons::Icon::scan;
            tile.label = tr("Scan for games");
            tile.off = lib.working();
        }
        else
        {
            const bool measuring = lib.has_action && lib.action.action == "measure" && lib.working();
            tile.key = "measure";
            tile.icon = icons::Icon::layers;
            tile.label = measuring ? tr("Measuring\xE2\x80\xA6") : tr("Measure game sizes");
            tile.off = !lib.can("list_games") || lib.working() || nd == 0;
        }
    };
    if (nd == 0)
    {
        // The drives not read yet, or none reported.
        storage.cols = 4;
        storage.below_rows = pano::kRows;
        storage.below = [this](hui::gfx::DrawList &list, float x, float top, float width)
        {
            const Snapshot &now = ctx_.store.state();
            const bool checking = !now.have_library_storage || now.library_storage.busy;
            const std::string title = !now.library_storage.error.empty() ? std::string(tr(std::string_view(now.library_storage.error)))
                                      : checking                         ? std::string(tr("Checking your drives\xE2\x80\xA6"))
                                                                         : std::string(tr("No drives reported."));
            float y = top - 10.0f;
            y += pano::draw_text(ctx_, &list, x, y, width, title, 64.0f, pano::kInk, pano::Face::xlight, 1.2f, 2);
            if (checking)
            {
                pano::draw_dots(ctx_, list, x, y + 20.0f + 5.0f, 10.0f);
                y += 30.0f;
            }
            pano::draw_text(ctx_, &list, x, y + 20.0f, std::min(width, 820.0f), tr("Capacity as ShadowMount reports it. Select a drive to see its games."), 21.0f,
                            pano::kDim, pano::Face::body, 1.55f, 3);
        };
    }
    page.groups.push_back(std::move(storage));
    if (pn_drive_ && nd > 0)
    {
        // The focused drive's games and their sizes, measured on request.
        pano::Group sizes;
        sizes.title = pano::lower(tr("Game storage"));
        sizes.cols = 3;
        sizes.below_rows = pano::kRows;
        sizes.below = [this](hui::gfx::DrawList &list, float x, float top, float width)
        {
            float y = top - 10.0f;
            int n = 0;
            for (const LibraryGame *g : drive_games())
            {
                if (++n > 5)
                    break;
                const std::string size = g->size >= 0.0                     ? format_bytes(g->size)
                                         : g->size_status == "unavailable" ? std::string(tr("Unavailable"))
                                                                           : std::string(tr("Not measured"));
                y += pano::draw_text(ctx_, &list, x, y, width, g->title + " (" + size + ")", 28.0f, pano::kInk, pano::Face::light, 1.45f, 1);
            }
            pano::draw_text(ctx_, &list, x, y + 14.0f, width,
                            tr("Sizes are measured on request. Shared source files can appear under more than one title, so these sizes are not a drive total."),
                            19.0f, pano::kDim, pano::Face::body, 1.55f, 4);
        };
        page.groups.push_back(std::move(sizes));
    }
    return page;
}

void LibraryScreen::panorama_run(char action, const LibraryGame *game, Feedback &feedback, const InputFrame &input)
{
    // What a tile (or the app bar) does, as the other interfaces do it.
    const Snapshot &state = ctx_.store.state();
    const LibrarySnapshot &library = state.library;
    const bool blocked = library.working() || ctx_.store.library_action().busy || library.stale;
    switch (action)
    {
    case 'M':
        if (game == nullptr || blocked || !game->on_drive)
            refuse(feedback, input);
        else if (!game->mounted)
            run_library("mount", false, game, {}, 0, feedback);
        else
        {
            show_game(game->title_id, game->source_key);
            mode_ = 1;
            xb_band_ = 0;
            feedback.play(hui::audio::Cue::modal_open);
        }
        break;
    case 'C':
    case 'V':
        if (game == nullptr || blocked || game->mounted)
            refuse(feedback, input);
        else
        {
            show_game(game->title_id, game->source_key);
            mode_ = action == 'C' ? 2 : 3;
            destination_id_.clear();
            xb_band_ = 0;
            ctx_.store.want_library_storage(true, true);
            feedback.play(hui::audio::Cue::modal_open);
        }
        break;
    case 'D':
        // The statuses open beside the game's own actions, the first one focused.
        pn_details_ = true;
        pano_.focus = {1, 0};
        feedback.play(hui::audio::Cue::open);
        break;
    case 'R':
        if (!state.have_library || library.busy)
            refuse(feedback, input);
        else
        {
            ctx_.store.refresh_library();
            feedback.play(hui::audio::Cue::select);
        }
        break;
    case 'S':
        pn_picker_ = 0;
        feedback.play(hui::audio::Cue::modal_open);
        break;
    case 'c':
        if (library.working())
            refuse(feedback, input);
        else
        {
            scan_open_ = true;
            xb_band_ = 0;
            error_.clear();
            feedback.play(hui::audio::Cue::modal_open);
        }
        break;
    case 'm':
        if (library.working() || ctx_.store.library_action().busy || !library.can("list_games") || state.library_storage.drives.empty())
            refuse(feedback, input);
        else
            run_library("measure", false, nullptr, {}, 0, feedback);
        break;
    case 'r':
        ctx_.store.want_library_storage(true, true);
        feedback.play(hui::audio::Cue::select);
        break;
    case 'x':
    {
        const StorageJob &j = library.storage_job;
        if (!j.active || !j.cancellable || j.cancel_requested || !library.can("storage_job_cancel") || ctx_.store.library_action().busy)
            refuse(feedback, input);
        else
            run_library("cancel", false, nullptr, {}, j.id, feedback);
        break;
    }
    default:
        refuse(feedback, input);
        break;
    }
}

pano::AppBar LibraryScreen::panorama_bar(const pano::Page &page) const
{
    // Everything for the focused place: a game's actions, a drive's, the
    // copy under way; then search, the scan and a refresh.
    const Snapshot &state = ctx_.store.state();
    const LibrarySnapshot &library = state.library;
    const std::string key = pano_.focused_key(page);
    pano::AppBar bar;
    const auto add = [&](char kind, std::string title, bool off = false, std::string sub = {})
    {
        pano::Item item;
        item.title = std::move(title);
        item.sub = std::move(sub);
        item.off = off;
        item.tag = kind;
        bar.items.push_back(std::move(item));
    };
    if (key.rfind("drive:", 0) == 0)
    {
        bar.head = tr("Storage");
        add('m', tr("Measure game sizes"), !library.can("list_games") || library.working());
        add('r', tr("Refresh drives"));
    }
    else if (key.rfind("job", 0) == 0)
    {
        bar.head = tr("Library");
        add('x', tr("Cancel operation"));
    }
    else if (const LibraryGame *g = panorama_game())
    {
        bar.head = g->title;
        for (const auto &a : panorama_actions(*g))
            if (a.first != 'R')
                add(a.first, a.second);
    }
    add('S', tr("Search your library"), false, query_.empty() ? std::string() : "\xE2\x80\x9C" + query_ + "\xE2\x80\x9D");
    if (library.can("rescan"))
        add('c', tr("Scan for games"), library.working());
    add('R', library.busy ? tr("Refreshing\xE2\x80\xA6") : tr("Refresh library"), library.busy);
    bar.focus = pn_bar_focus_;
    return bar;
}

Intent LibraryScreen::update_panorama(const InputFrame &input, float dt, Feedback &feedback, bool focused)
{
    Intent intent;
    const Snapshot &state = ctx_.store.state();
    const LibrarySnapshot &library = state.library;
    pano::keyboard_look(keyboard_.style);
    keyboard_.set_bounds(pano::keyboard_keys());
    // The page shows Storage too: keep its drives coming while it's here.
    ctx_.store.want_library_storage(true);
    const bool band = scan_open_ || (!selected_.empty() && mode_ != 0);
    if (!band && !selected_.empty())
        selected_.clear();
    pano::Page page = panorama_page();
    pano_.clamp(page);
    const std::string key = pano_.focused_key(page);
    const LibraryGame *game = panorama_game();
    if (focused && keyboard_open_)
        update_keyboard(input, feedback);
    else if (focused && band)
    {
        // A question: destination tiles across, its buttons under them.
        const xb::Band question = crossbar_band();
        const int options = static_cast<int>(question.options.size());
        InputFrame step = input;
        if (input.nav != Direction::none)
        {
            int to = xb_band_;
            if (xb_band_ < options)
                to = input.nav == Direction::left ? xb_band_ - 1 : input.nav == Direction::right ? xb_band_ + 1 : input.nav == Direction::down ? options : -1;
            else
                to = input.nav == Direction::up ? (options > 0 ? 0 : -1)
                     : input.nav == Direction::left ? (xb_band_ > options ? xb_band_ - 1 : -1)
                     : input.nav == Direction::right ? xb_band_ + 1
                                                     : -1;
            if (to >= 0 && to < options + static_cast<int>(question.buttons.size()) && (xb_band_ >= options || to < options || input.nav == Direction::down))
            {
                xb_band_ = to;
                feedback.play(hui::audio::Cue::focus);
            }
            else
                refuse(feedback, input);
            step.nav = Direction::none;
        }
        update_band(step, feedback);
    }
    else if (focused && pn_picker_ >= 0)
    {
        // Search your library: the field, then every location.
        std::vector<std::string> values;
        for (const LibraryGame &g : library.games)
            if (!g.location.empty() && std::find(values.begin(), values.end(), g.location) == values.end())
                values.push_back(g.location);
        std::sort(values.begin(), values.end());
        const int count = 2 + static_cast<int>(values.size());
        if (input.nav == Direction::up || input.nav == Direction::down)
        {
            const int to = pn_picker_ + (input.nav == Direction::down ? 1 : -1);
            if (to >= 0 && to < count)
            {
                pn_picker_ = to;
                feedback.play(hui::audio::Cue::focus);
            }
            else
                refuse(feedback, input);
        }
        else if (input.is_pressed(Action::back))
        {
            pn_picker_ = -1;
            feedback.play(hui::audio::Cue::modal_close);
        }
        else if (input.is_pressed(Action::confirm))
        {
            if (pn_picker_ == 0)
            {
                keyboard_open_ = true;
                keyboard_.enter();
                keyboard_.set_length(static_cast<int>(query_.size()));
                feedback.play(hui::audio::Cue::modal_open);
            }
            else
            {
                location_ = pn_picker_ > 1 ? values[static_cast<std::size_t>(pn_picker_ - 2)] : std::string();
                refilter();
                pano_.focus = {};
                pn_picker_ = -1;
                feedback.play(hui::audio::Cue::select);
            }
        }
    }
    else if (focused && pn_bar_open_)
    {
        pano::AppBar bar = panorama_bar(page);
        const int chosen = pano::update_appbar(bar, input, feedback);
        pn_bar_focus_ = bar.focus;
        if (chosen != -1)
            pn_bar_open_ = false;
        if (chosen >= 0)
        {
            const char kind = static_cast<char>(bar.items[static_cast<std::size_t>(chosen)].tag);
            panorama_run(kind, game, feedback, input);
        }
    }
    else if (focused)
    {
        if (!state.have_library || !library.ready() || rows_.empty())
        {
            // Not answering: Cross refreshes; nothing matches: Cross resets the filters.
            if (input.is_pressed(Action::confirm))
            {
                if (state.have_library && library.ready() && !library.games.empty())
                {
                    view_ = 0;
                    query_.clear();
                    location_.clear();
                    format_.clear();
                    refilter();
                    feedback.play(hui::audio::Cue::toggle);
                }
                else if (library.busy || (state.have_library && library.ready()))
                    refuse(feedback, input);
                else
                {
                    ctx_.store.refresh_library();
                    feedback.play(hui::audio::Cue::select);
                }
            }
        }
        else if (input.nav != Direction::none)
        {
            pano_.move(page, input, feedback);
        }
        else if (input.is_pressed(Action::back) && pn_details_)
        {
            pn_details_ = false;
            feedback.play(hui::audio::Cue::back);
        }
        else if (input.is_pressed(Action::north))
            panorama_run(key.rfind("drive:", 0) == 0 ? 'r' : 'R', game, feedback, input);
        else if (input.is_pressed(Action::west))
            panorama_run('S', game, feedback, input);
        else if (input.is_pressed(Action::menu))
        {
            pn_bar_open_ = true;
            pn_bar_focus_ = 0;
            feedback.play(hui::audio::Cue::modal_open);
        }
        else if (input.is_pressed(Action::confirm))
        {
            if (key.rfind("g:", 0) == 0)
            {
                // A game: Cross does its first action (mount or unmount, else Details).
                const std::vector<std::pair<char, std::string>> actions = game != nullptr ? panorama_actions(*game)
                                                                                          : std::vector<std::pair<char, std::string>>{};
                if (!actions.empty())
                    panorama_run(actions.front().first, game, feedback, input);
            }
            else if (key.rfind("act:", 0) == 0)
                panorama_run(key[4], game, feedback, input);
            else if (key == "search")
                panorama_run('S', game, feedback, input);
            else if (key == "scan")
                panorama_run('c', game, feedback, input);
            else if (key == "measure")
                panorama_run('m', game, feedback, input);
            else if (key == "job:cancel")
                panorama_run('x', game, feedback, input);
            else if (key == "job:details" || key == "job")
            {
                // The game being copied or moved: its statuses.
                for (const LibraryGame &g : library.games)
                    if (g.title_id == library.storage_job.title_id)
                        pn_game_ = game_key(g);
                pn_details_ = true;
                feedback.play(hui::audio::Cue::open);
            }
            else
                refuse(feedback, input);
        }
    }
    // The selected game follows the focus in the games group; details close
    // when the focus leaves the game's own group.
    page = panorama_page();
    const std::string now_key = pano_.focused_key(page);
    if (now_key.rfind("g:", 0) == 0 && now_key != pn_game_)
    {
        pn_game_ = now_key;
        pn_details_ = false;
    }
    else if (pn_details_ && now_key.rfind("act:", 0) != 0 && now_key.rfind("job", 0) != 0 && !band && !pn_bar_open_)
        pn_details_ = false;
    pn_drive_ = now_key.rfind("drive:", 0) == 0;
    if (pn_drive_)
        drive_id_ = now_key.substr(6);
    page = panorama_page();
    if (!now_key.empty() && pano_.focused_key(page) != now_key)
        pano_.focus_key(page, now_key);
    pano_.show_group(pn_details_ ? 2 : -1);
    pano_.animate(page, dt);
    keyboard_.set_active(keyboard_open_);
    keyboard_.update(dt);
    keyboard_in_.target = keyboard_open_ ? 1.0f : 0.0f;
    keyboard_in_.update(dt, 14.0f);
    xb_band_in_.target = scan_open_ || (!selected_.empty() && mode_ != 0) ? 1.0f : 0.0f;
    xb_band_in_.update(dt, 16.0f);
    pn_picker_in_.target = pn_picker_ >= 0 ? 1.0f : 0.0f;
    pn_picker_in_.update(dt, look::prefs().reduced_motion ? 40.0f : 16.0f);
    pn_bar_in_.target = pn_bar_open_ ? 1.0f : 0.0f;
    pn_bar_in_.update(dt, look::prefs().reduced_motion ? 40.0f : 16.0f);
    return intent;
}

void LibraryScreen::draw_panorama(Frame &frame) const
{
    hui::gfx::DrawList &list = frame.scene;
    const Snapshot &state = ctx_.store.state();
    const LibrarySnapshot &library = state.library;
    if (keyboard_open_)
    {
        pano::draw_keyboard(ctx_, frame, keyboard_, tr("Search your library"), query_, tr("type a name"), false);
        return;
    }
    const pano::Page page = panorama_page();
    pano::draw_header(ctx_, list, pano::lower(tr("Library")), pano_.scroll() * 0.35f);
    if (!state.have_library || !library.ready())
    {
        const bool busy = !state.have_library || library.busy;
        if (busy)
            pano::draw_empty(ctx_, list, tr("Library"), tr("Finding your games"),
                             tr("Reading installed titles and drive availability from ShadowMount."), {}, true);
        else
            pano::draw_empty(ctx_, list, tr("ShadowMount isn\xE2\x80\x99t answering"), tr("Connect your library"),
                             std::string(tr("Library uses ShadowMount\xE2\x80\x99s local games API on your PS5. Start a version with that API "
                                            "enabled, then refresh here.")) +
                                 " " + tr("Games are not inferred from download history."),
                             {pano::button(tr("Refresh library"), icons::Icon::refresh, true)});
    }
    else if (rows_.empty())
    {
        if (library.games.empty())
            pano::draw_empty(ctx_, list, tr("Library"), tr("No games reported yet"),
                             tr("ShadowMount has not reported any installed or on-drive games. Refresh after its inventory updates."), {});
        else
            pano::draw_empty(ctx_, list, query_.empty() ? std::string(tr("Library")) : "\xE2\x80\x9C" + query_ + "\xE2\x80\x9D",
                             tr("No matching games"), tr("Try a different title, status or location."),
                             {pano::button(tr("Reset library filters"), icons::Icon::refresh, true)});
    }
    else
    {
        // Under the title: the selected game's status, or the snapshot's age when it's old.
        const LibraryGame *game = panorama_game();
        std::string line;
        if (library.stale)
            line = last_refreshed(library) + " \xC2\xB7 " + tr("Saved snapshot. Refresh the library before relying on these statuses.");
        else if (game != nullptr)
            line = status_of(*game) + " \xC2\xB7 " + (game->on_drive ? i18n::place(game->location) : std::string(tr("Drive disconnected")));
        if (!line.empty())
            ui::text_fit(list, ctx_.type.regular, line, pano::kLeft, pano::baseline(256.0f, 22.0f), 22.0f, 1720.0f,
                         library.stale ? pano::kAmber : pano::kDim);
        pano_.draw(ctx_, list, page, pn_picker_ < 0 && !pn_bar_open_);
        std::vector<std::string> notes;
        if (!state.library_error.empty())
            notes.push_back(std::string(tr(std::string_view(state.library_error))));
        if (!library.message.empty())
            notes.push_back(std::string(tr(std::string_view(library.message))));
        if (!error_.empty() && mode_ == 0)
            notes.push_back(std::string(tr(std::string_view(error_))));
        if (!notes.empty())
            ui::text_fit(list, ctx_.type.regular, notes.front(), pano::kLeft, 1080.0f - 30.0f - 15.0f + 19.0f * 0.364f, 19.0f, 900.0f, pano::kAmber);
    }
    hui::gfx::DrawList &over = frame.overlay;
    if (pn_picker_in_.value > 0.01f)
    {
        // Search and Location as one list.
        pano::Picker picker;
        picker.head = tr("Search your library");
        picker.field = true;
        picker.field_value = query_;
        picker.field_placeholder = tr("Search your library");
        picker.field_focus = pn_picker_ == 0;
        picker.note_above = tr("Location");
        picker.size = 40.0f;
        std::vector<std::string> values;
        for (const LibraryGame &g : library.games)
            if (!g.location.empty() && std::find(values.begin(), values.end(), g.location) == values.end())
                values.push_back(g.location);
        std::sort(values.begin(), values.end());
        pano::Item all;
        all.title = tr("All locations");
        all.chosen = location_.empty();
        picker.items.push_back(all);
        for (const std::string &v : values)
        {
            pano::Item item;
            item.title = i18n::place(v);
            item.sub = trn(std::count_if(library.games.begin(), library.games.end(), [&](const LibraryGame &g) { return g.location == v; }),
                           "{count} game", "{count} games");
            item.chosen = v == location_;
            picker.items.push_back(std::move(item));
        }
        picker.focus = pn_picker_ - 1;
        pano::draw_picker(ctx_, over, picker, pn_picker_in_.value);
    }
    if (pn_bar_in_.value > 0.01f)
        pano::draw_appbar(ctx_, over, panorama_bar(page), pn_bar_in_.value);
    if (xb_band_in_.value > 0.01f)
    {
        // A question: Store's words in a message band, destinations as tiles.
        const xb::Band question = crossbar_band();
        pano::Band band;
        band.kicker = question.kicker;
        band.title = question.title;
        band.body = question.body + (question.note.empty() ? std::string() : " " + question.note);
        band.error = question.error;
        band.busy = question.busy;
        if (!question.options.empty())
        {
            band.tiles_label = tr("Destination drive");
            const std::vector<const LibraryDrive *> targets = destinations();
            for (std::size_t i = 0; i < targets.size() && i < question.options.size(); ++i)
            {
                pano::Tile tile;
                tile.has_icon = true;
                tile.icon = icons::Icon::drive;
                tile.label = i18n::place(targets[i]->label);
                tile.sub = tr("{size} free", {{"size", format_bytes(targets[i]->free)}});
                tile.check = tile.selected = question.options[i].checked;
                band.tiles.push_back(std::move(tile));
            }
        }
        for (const std::string &b : question.buttons)
            band.buttons.push_back(pano::button(b));
        band.focus = xb_band_;
        pano::draw_band(ctx_, over, band, xb_band_in_.value);
    }
}

std::vector<hui::ui::Hint> LibraryScreen::panorama_hints() const
{
    const Snapshot &state = ctx_.store.state();
    const LibrarySnapshot &library = state.library;
    if (keyboard_open_)
        return {{hui::ui::Button::square, tr("Delete")}, {hui::ui::Button::triangle, tr("Space")}, {hui::ui::Button::circle, tr("Close")}};
    if (scan_open_ || (!selected_.empty() && mode_ != 0))
        return {};
    if (pn_picker_ >= 0 || pn_bar_open_)
        return {{hui::ui::Button::cross, tr("Choose")}, {hui::ui::Button::circle, tr("Close")}};
    if (!state.have_library || !library.ready())
        return library.busy || !state.have_library ? std::vector<hui::ui::Hint>{{hui::ui::Button::l1, tr("Sections"), hui::ui::Button::r1}}
                                                    : std::vector<hui::ui::Hint>{{hui::ui::Button::cross, tr("Refresh library")}};
    if (rows_.empty())
        return library.games.empty() ? std::vector<hui::ui::Hint>{{hui::ui::Button::l1, tr("Sections"), hui::ui::Button::r1}}
                                     : std::vector<hui::ui::Hint>{{hui::ui::Button::cross, tr("Reset library filters")}};
    std::vector<hui::ui::Hint> hints{{hui::ui::Button::cross, tr("Select")}, {hui::ui::Button::options, tr("Actions")}};
    hints.push_back({hui::ui::Button::triangle, pn_drive_ ? tr("Refresh drives") : tr("Refresh library")});
    if (pn_details_)
        hints.push_back({hui::ui::Button::circle, tr("Back")});
    else
        hints.push_back({hui::ui::Button::l1, tr("Sections"), hui::ui::Button::r1});
    return hints;
}

} // namespace orbit
