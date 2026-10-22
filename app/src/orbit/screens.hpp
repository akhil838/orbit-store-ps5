// Orbit Store TV app - The storefront's screens and what they share.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Screens are immediate mode, like the kit: update() owns state and
// animation, draw() is const and only records shapes. A screen never talks to
// the network; it reads the Store's snapshot, asks the ArtCache for images
// and returns an Intent when the app should navigate.

#pragma once

#include "orbit/browser_handoff.hpp"
#include "core/input.hpp"
#include "gfx/backdrop_spec.hpp"
#include "gfx/draw_list.hpp"
#include "orbit/art.hpp"
#include "orbit/cn.hpp"
#include "orbit/look.hpp"
#include "orbit/pano.hpp"
#include "orbit/xb.hpp"
#include "orbit/store.hpp"
#include "orbit/ui.hpp"
#include "ui/components/keyboard.hpp"
#include "ui/feedback.hpp"
#include "ui/glyphs.hpp"
#include "ui/theme.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace orbit
{

using hui::Action;
using hui::Direction;
using hui::InputFrame;
using Feedback = hui::ui::Feedback;

// One frame, back to front: backdrop, scene, [glass capture], overlay.
struct Frame
{
    hui::gfx::BackdropSpec backdrop;
    hui::gfx::DrawList scene;
    hui::gfx::DrawList overlay;
    bool glass = false;
    std::uint32_t glass_texture = 0;

    void reset()
    {
        backdrop = {};
        scene.clear();
        overlay.clear();
        glass = false;
    }
};

// Long-lived services every screen receives.
struct Context
{
    Context(const ui::Type &type_, const hui::ui::Fonts &fonts_, Store &store_, ArtCache &art_)
        : type(type_), fonts(fonts_), store(store_), art(art_)
    {
    }

    const ui::Type &type;
    const hui::ui::Fonts &fonts; // the kit's slots, for glyphs and the keyboard
    Store &store;
    ArtCache &art;
    std::uint32_t mark = 0;        // the planet mark, 128 px
    std::uint32_t loader_mark = 0; // the mark with a faint ring, 256 px
    std::string today;             // YYYY-MM-DD, for Latest releases
    std::string app_version;       // this app's contentVersion
    // Opens the browser version of Orbit; empty where it is not possible.
    std::function<bool()> open_browser;
    // Opens the exact option and destination; the browser still asks to proceed.
    std::function<bool(const BrowserSelection &)> open_download;
    float clock = 0.0f; // seconds since the app opened
};

// The sections Crossbar, Cinema and Panorama move between (L1/R1): Search,
// the four tabs, then App settings.
namespace section
{
constexpr int search = 0;
constexpr int discover = 1;
constexpr int browse = 2;
constexpr int library = 3;
constexpr int downloads = 4;
constexpr int settings = 5;
constexpr int count = 6;
} // namespace section
const char *section_title(int section); // untranslated, for tr()

// The tabs, in the browser storefront's order; it opens on Discover.
namespace tab
{
constexpr int discover = 0;
constexpr int browse = 1;
constexpr int library = 2;
constexpr int downloads = 3;
constexpr int count = 4;
} // namespace tab

struct Intent
{
    enum class Kind : std::uint8_t
    {
        none,
        top_bar,      // the focus left the screen upward
        open_game,    // id: a game
        show_job,     // id: a job (Downloads)
        show_tab,     // tab: one of tab::
        show_library, // id: a title ID, detail: its Library source key
        provider_browser, // native verification panel; service opens Vikingfile
        close,        // the game page is done
    } kind = Kind::none;
    std::string id;
    std::string detail;
    int tab = 0;
    std::string toast; // a short confirmation the app shows
};

class Screen
{
  public:
    explicit Screen(Context &context) : ctx_(context)
    {
    }
    virtual ~Screen() = default;
    // The focus arrives on the screen (from the top bar, or after a dialog).
    virtual void focus()
    {
    }
    // focused is false while the top bar has the focus: animate only.
    virtual Intent update(const InputFrame &input, float dt, Feedback &feedback, bool focused) = 0;
    virtual void draw(Frame &frame, bool focused) const = 0;
    // The game whose art fills the background, if any (Discover only, as on the web).
    virtual std::string backdrop_game() const
    {
        return {};
    }
    virtual std::vector<hui::ui::Hint> hints() const
    {
        return {};
    }
    // True while a dialog or keyboard of the screen takes all input.
    virtual bool modal() const
    {
        return false;
    }
    // L1 or R1 (step -1 or 1), offered to the screen first: Cinema's
    // collections. True when the screen used it; else the section changes.
    virtual bool page(int step)
    {
        (void)step;
        return false;
    }
    // Cinema raises the hints above a keyboard sheet.
    virtual bool hints_high() const
    {
        return false;
    }
    // Options does something here though no hint names it (Cinema's sheets
    // of actions); otherwise, with no Options hint, Options shows Controls.
    virtual bool options_used() const
    {
        return false;
    }

  protected:
    Context &ctx_;
};

// A dropdown's controller state (the browser's Select): Cross opens it, up
// and down move, Cross chooses and Circle closes it unchanged.
struct Picker
{
    bool open = false;
    int active = 0;
    ui::Ease in;

    void show(int selected)
    {
        open = true;
        active = selected < 0 ? 0 : selected;
    }
    // While open: the chosen index once it is chosen, else -1.
    int update(const InputFrame &input, int count, Feedback &feedback);
    void animate(float dt);
};

// ---- shared drawing (views.cpp) ----

// A game's cover, fading in when its art arrives; a placeholder before.
void draw_cover(Context &ctx, hui::gfx::DrawList &list, const Game &game, const ui::Rect &r,
                float radius, float opacity = 1.0f);
// A cover tile as the browser's .game-tile: grows and rings when focused;
// current (0..1) outlines the game the hero shows while the focus is elsewhere.
void draw_tile(Context &ctx, hui::gfx::DrawList &list, const Game &game, const ui::Rect &r,
               float focus, float current = 0.0f, float opacity = 1.0f);
// Under a tile: the name (up to two lines), the download state if any, and
// the release date when asked. Returns the baseline after it.
float draw_tile_caption(Context &ctx, hui::gfx::DrawList &list, const Game &game, const char *state,
                        float x, float cover_bottom, float width, float focus, bool date);
// The art behind everything for one game, at an opacity (crossfades).
void draw_backdrop_art(Context &ctx, hui::gfx::DrawList &list, const Game &game, float opacity,
                       float height = 1080.0f);
// The navy veils over the art (the browser's .backdrop::after).
void draw_veils(hui::gfx::DrawList &list, bool discover = false);
// Refusal at an edge: quiet, and silent on a held direction.
void refuse(Feedback &feedback, const InputFrame &input);
// The kit theme the on-screen keyboard is drawn in.
hui::ui::Theme keyboard_theme();
// A small drawing of an interface, for its card in App settings → Appearance.
void draw_interface_thumb(Context &ctx, hui::gfx::DrawList &list, int interface, const ui::Rect &r);

// Crossbar (discover_looks.cpp): a game as a row, its Actions menu and what a choice there does.
void xb_game_row(const Snapshot &state, const Game &game, xb::Row &row, bool dated);
// Cinema's stage for a game, and a strip round the focused one (at -1: the
// search's results, none focused, under the query).
cn::Hero cinema_hero(Context &ctx, const Game &game, bool dated, bool focus);
void cinema_strip(Context &ctx, hui::gfx::DrawList &out, const std::vector<int> &games, int at, std::string label);
// Cinema's stage over rows: the game's kicker, title and tagline.
cn::Stage cinema_stage(const Game &game, bool dated, float y);
// Cinema's rows (Discover's collections, Browse's results): shelves of games,
// each a strip or a grid five across; the focused line sits at the top.
struct CinemaShelf
{
    std::string label;
    const std::vector<int> *games = nullptr;
    bool grid = false;
    int at = 0;    // the focused game in this shelf (or the last one)
    int start = 0; // a strip's first frame on screen
};
// Moves the focus along a line, or to the line above or below (across
// shelves, in the same column); false at an edge.
bool cinema_rows_move(std::vector<CinemaShelf> &shelves, int &shelf, Direction direction);
// The rows' offset that puts the focused line at the top of an area this tall.
float cinema_rows_offset(const std::vector<CinemaShelf> &shelves, int shelf, float area);
void draw_cinema_rows(Context &ctx, hui::gfx::DrawList &out, const std::vector<CinemaShelf> &shelves, int shelf, float top,
                      float bottom, float offset, bool focused = true);
std::vector<xb::MenuItem> xb_game_actions(const Snapshot &state, const Game &game);
// A game's first action, as its state has it: its download, its Library copy, or its page.
Intent game_primary(const Snapshot &state, const Game &game);
Intent xb_game_action(Context &ctx, const Game &game, int chosen);
// Crossbar's menu for a game: the same without View game (its Download opens
// the page too, as Cross on the row does).
std::vector<xb::MenuItem> xb_crossbar_actions(const Snapshot &state, const Game &game);
Intent xb_crossbar_action(Context &ctx, const Game &game, int chosen);

// ---- the screens ----

class DiscoverScreen final : public Screen
{
  public:
    explicit DiscoverScreen(Context &context);
    void focus() override;
    Intent update(const InputFrame &input, float dt, Feedback &feedback, bool focused) override;
    void draw(Frame &frame, bool focused) const override;
    std::string backdrop_game() const override
    {
        return featured_;
    }
    std::vector<hui::ui::Hint> hints() const override;
    bool modal() const override
    {
        // A game's actions keep L1 and R1 from changing the section.
        return xb_menu_.open || cn_actions_ || pano_bar_open_;
    }
    bool options_used() const override
    {
        return look::is(look::Interface::cinema) && cinema_game() != nullptr;
    }
    bool page(int step) override;
    // The game whose page was just closed, so the focus returns to its tile.
    void return_to(const std::string &game_id);

  private:
    struct Rail
    {
        std::string title;
        std::string note;
        std::vector<int> games; // indices into the snapshot's games
        bool grid = false;
    };
    void rebuild();
    const Game *game_at(int row, int column) const;
    float row_top(int row) const;

    std::vector<Rail> rails_;
    std::uint64_t catalog_serial_ = ~0ull;
    int row_ = 0;              // hero, horizontal rails, then the All games grid
    std::vector<int> columns_; // focused column per rail
    std::string featured_;
    std::string previous_featured_;
    ui::Ease scroll_;
    std::vector<ui::Ease> rail_scroll_;
    ui::Ease hero_in_;
    ui::Ease button_focus_;
    ui::Ease favourite_focus_;
    int hero_action_ = 0;
    std::vector<std::vector<float>> tile_focus_;

    // Panorama (discover_looks.cpp): the same rows as tile groups, then All
    // games A to Z; Options raises the app bar of the focused game's actions.
    pano::Page panorama_page() const;
    const Game *panorama_game(const pano::Page &page) const;
    pano::AppBar panorama_bar(const Game &game) const;
    Intent update_panorama(const InputFrame &input, float dt, Feedback &feedback, bool focused);
    void draw_panorama(Frame &frame, bool focused) const;
    std::vector<hui::ui::Hint> panorama_hints() const;
    pano::View pano_;
    bool pano_bar_open_ = false;
    int pano_bar_focus_ = 0;
    ui::Ease pano_bar_in_;
    // Crossbar (discover_looks.cpp): New on Orbit, then Latest releases and
    // All games as folders; All games runs A to Z with a letter strip.
    xb::List crossbar_list() const;
    std::vector<xb::MenuItem> crossbar_actions(const Game &game) const;
    Intent update_crossbar(const InputFrame &input, float dt, Feedback &feedback, bool focused);
    void draw_crossbar(Frame &frame) const;
    std::vector<hui::ui::Hint> crossbar_hints() const;
    xb::View xb_;
    xb::Menu xb_menu_;
    int xb_folder_ = 0; // 0 the section, 1 Latest releases, 2 All games
    std::vector<int> all_games_; // All games, largest first (Crossbar, Cinema and Panorama)

    // Cinema (discover_looks.cpp): the focused game on the stage, rows under
    // it: New on Orbit, Latest releases and All games.
    const std::vector<int> *cinema_games(int collection) const;
    // The rows, and which collection each is (0 New on Orbit, 1 Latest
    // releases, 2 All games); the focused one's place among them.
    std::vector<CinemaShelf> cinema_shelves(std::vector<int> *ids = nullptr) const;
    int cinema_shelf(const std::vector<int> &ids) const;
    const Game *cinema_game() const;
    Intent update_cinema(const InputFrame &input, float dt, Feedback &feedback, bool focused);
    void draw_cinema(Frame &frame) const;
    std::vector<hui::ui::Hint> cinema_hints() const;
    int cn_collection_ = 0;
    int cn_at_[3] = {};
    int cn_start_[3] = {};
    ui::Ease cn_offset_;
    bool cn_actions_ = false; // Options' sheet of actions
    bool cn_focused_ = true;
    int cn_action_ = 0;
    ui::Ease cn_actions_in_;
};

class BrowseScreen final : public Screen
{
  public:
    explicit BrowseScreen(Context &context);
    void focus() override;
    Intent update(const InputFrame &input, float dt, Feedback &feedback, bool focused) override;
    void draw(Frame &frame, bool focused) const override;
    std::vector<hui::ui::Hint> hints() const override;
    bool modal() const override
    {
        return keyboard_open_ || picker_.open || cn_sheet_ || xb_menu_.open || xb_values_.open || pn_picker_ != 0;
    }
    bool page(int step) override;
    bool hints_high() const override;
    // Panorama shows the focused game's art behind the page.
    std::string backdrop_game() const override;
    // Opens the keyboard on the search field (the top bar's search button).
    void start_search();
    void return_to(const std::string &game_id);
    // The Search section (Crossbar, Cinema, Panorama): Browse led by the query.
    void open_search_section();
    // Previews and tests: type a search without pressing keys.
    void set_query(const std::string &query);
    const BrowseFilters &filters() const
    {
        return filters_;
    }

  private:
    enum class Area : std::uint8_t
    {
        field,
        collection, // All games, Favourites
        filters,    // Source, Format, Region, Download size, Sort by, Reset filters
        grid,
    };
    static constexpr int kFilterControls = 7;
    void refilter();
    ui::Rect tile_rect(int index) const;
    ui::Rect filter_rect(int index) const;
    // The choices a filter offers and which is current.
    std::vector<std::string> filter_options(int filter, int *selected) const;
    void choose(int filter, int option);
    std::string filter_value(int filter) const;

    Area area_ = Area::field;
    int column_ = 0; // within the collection chips or the filters row
    BrowseFilters filters_;
    std::vector<int> results_;
    std::uint64_t catalog_serial_ = ~0ull;
    std::uint64_t favourites_serial_ = ~0ull;
    int index_ = 0;
    bool keyboard_open_ = false;
    hui::ui::Keyboard keyboard_;
    Picker picker_;
    int picking_ = 0; // the filter the open picker belongs to
    ui::Ease scroll_;
    ui::Ease field_focus_;
    ui::Ease keyboard_in_;
    float control_focus_[2 + kFilterControls] = {};
    std::vector<float> tile_focus_;

    // Crossbar (browse_looks.cpp): Filters and sort, Favourites, then the games.
    bool search_mode_ = false; // the Search section rather than Browse
    xb::List crossbar_list() const;
    std::string crossbar_summary() const;

    // Cinema (browse_looks.cpp): the results as a strip with the focused game
    // on the stage; Browse's filters in a sheet, Search's query over the keys.
    Intent update_cinema(const InputFrame &input, float dt, Feedback &feedback, bool focused);
    void draw_cinema(Frame &frame) const;
    std::vector<hui::ui::Hint> cinema_hints() const;
    int cn_at_ = 0;
    ui::Ease cn_offset_;     // the results' rows, the focused line at the top
    CinemaShelf cinema_shelf() const;
    bool cn_sheet_ = false;  // Filters and sort
    bool cn_focused_ = true;
    int cn_row_ = 0;         // its focused row
    bool cn_values_ = false; // a filter's values beside the rows
    int cn_value_ = 0;
    ui::Ease cn_sheet_in_;
    void reset_crossbar(Feedback &feedback);
    std::vector<xb::MenuItem> crossbar_filters() const;
    std::vector<xb::MenuItem> crossbar_values(int filter) const;

    // Panorama (browse_looks.cpp): the results as a group of tiles and the
    // favourites as another, what's applied written under the title; Square
    // opens the filters as a full-screen list, then a filter's values. Search:
    // the query over the keys with a row of results, then a page of them.
    pano::Page panorama_page() const;
    pano::Tile panorama_tile(const pano::Page &page) const;
    Intent update_panorama(const InputFrame &input, float dt, Feedback &feedback, bool focused);
    void draw_panorama(Frame &frame) const;
    std::vector<hui::ui::Hint> panorama_hints() const;
    pano::View pano_;
    int pn_picker_ = 0; // 0 closed, 1 Filters and sort, 2 a filter's values
    int pn_row_ = 0;
    int pn_value_ = 0;
    ui::Ease pn_picker_in_;
    Intent update_crossbar(const InputFrame &input, float dt, Feedback &feedback, bool focused);
    void draw_crossbar(Frame &frame) const;
    std::vector<hui::ui::Hint> crossbar_hints() const;
    void update_keyboard(const InputFrame &input, Feedback &feedback);
    xb::View xb_;
    xb::Menu xb_menu_;   // the actions of a game, or the filters
    xb::Menu xb_values_; // one filter's values
    bool xb_filters_ = false; // xb_menu_ shows the filters, not a game's actions
    int xb_filter_ = 0;       // the filter xb_values_ belongs to
};

// Library: ShadowMount's inventory as Orbit reports it (src/Library.tsx):
// games installed on the console and sources on its drives, their details,
// mount and unmount, copy and move between drives, and drive storage.
class LibraryScreen final : public Screen
{
  public:
    explicit LibraryScreen(Context &context);
    void focus() override;
    void blur();
    Intent update(const InputFrame &input, float dt, Feedback &feedback, bool focused) override;
    void draw(Frame &frame, bool focused) const override;
    std::vector<hui::ui::Hint> hints() const override;
    bool modal() const override
    {
        return keyboard_open_ || picker_.open || !selected_.empty() || scan_open_ || xb_menu_.open || cn_sheet_ ||
               cn_details_ || pn_picker_ >= 0 || pn_bar_open_;
    }
    bool page(int step) override;
    bool hints_high() const override
    {
        return keyboard_open_;
    }
    // Opens a game's details (View in Library from a game page).
    void show_game(const std::string &title_id, const std::string &source_key);
    // Previews and tests.
    bool details_open() const
    {
        return !selected_.empty();
    }

  private:
    enum class Kind : std::uint8_t
    {
        refresh,
        scan,
        section, // index 0 games, 1 storage
        view,    // the status views
        search,
        location,
        format,
        reset, // Reset library filters (no matches)
        card,  // index into rows_
        drive, // index into the storage drives
        measure,
        cancel_job,
        storage_game, // index into the selected drive's games
        // in the details panel
        action, // index: 0 mount/unmount, 1 copy, 2 move
        destination,
        confirm,
        back,
        // in the scan dialog
        scan_start,
        scan_back,
    };
    struct Control
    {
        Kind kind;
        int index = 0;
        int row = 0;
        ui::Rect rect;
    };
    std::vector<Control> page_controls() const;
    std::vector<Control> panel_controls() const;
    void refilter();
    const LibraryGame *selected_game() const;
    std::vector<const LibraryDrive *> destinations() const;
    std::vector<const LibraryGame *> drive_games() const;
    std::vector<std::string> filter_values() const;
    std::vector<std::string> picker_options(int *selected) const;
    float content_height() const;
    void run_library(const char *action, bool confirmed, const LibraryGame *game, const std::string &destination, int job,
                     Feedback &feedback);
    void update_keyboard(const InputFrame &input, Feedback &feedback);

    // Crossbar (library_looks.cpp): the storage operation and notices, search,
    // location and the status views as folders, the games, Storage and the
    // scan; a game opens as a folder of its actions.
    struct XbEntry
    {
        char kind;
        int index;
    };
    std::vector<XbEntry> crossbar_entries() const;
    xb::List crossbar_list() const;
    xb::Band crossbar_band() const;
    std::vector<xb::MenuItem> crossbar_menu() const;
    void leave_crossbar(Feedback &feedback);
    void update_band(const InputFrame &input, Feedback &feedback);
    Intent update_crossbar(const InputFrame &input, float dt, Feedback &feedback, bool focused);
    void draw_crossbar(Frame &frame) const;
    std::vector<hui::ui::Hint> crossbar_hints() const;
    xb::View xb_;
    xb::Menu xb_menu_;
    int xb_menu_kind_ = 0; // 0 Location, 1 the storage operation, 2 a drive
    int xb_band_ = 0;      // the focus in a confirmation: its options, then its buttons
    ui::Ease xb_band_in_;

    // Cinema (library_looks.cpp): the views and Storage as collections; the
    // focused game (or drive) on the stage, its details in a sheet; search,
    // location, scan and refresh in Options' sheet; questions as title cards.
    int cinema_collection() const;
    std::vector<int> cinema_rows() const; // -1: the storage operation, pinned first
    Intent update_cinema(const InputFrame &input, float dt, Feedback &feedback, bool focused);
    void draw_cinema(Frame &frame) const;
    std::vector<hui::ui::Hint> cinema_hints() const;
    int cn_at_[5] = {};
    bool cn_sheet_ = false;   // Options: search, location, scan, refresh
    int cn_row_ = 0;
    bool cn_values_ = false;  // the locations beside the rows
    int cn_value_ = 0;
    bool cn_details_ = false; // the focused game's statuses
    bool cn_buttons_ = false; // Up from the strip, then Left/Right through actions
    bool cn_focused_ = true;
    int cn_button_ = 0;
    std::vector<char> cinema_buttons() const;
    ui::Ease cn_sheet_in_;

    // Panorama (library_looks.cpp): one page of the games (and Search your
    // library), the selected game's own group of actions (a copy or move
    // under way takes its place), then Storage. Details add the game's
    // statuses as a group beside it; Square opens search and Location as a
    // full-screen list; Options raises the app bar; questions are bands.
    pano::Page panorama_page() const;
    const LibraryGame *panorama_game() const; // the game whose group shows
    std::vector<std::pair<char, std::string>> panorama_actions(const LibraryGame &game) const;
    pano::AppBar panorama_bar(const pano::Page &page) const;
    void panorama_run(char action, const LibraryGame *game, Feedback &feedback, const InputFrame &input);
    Intent update_panorama(const InputFrame &input, float dt, Feedback &feedback, bool focused);
    void draw_panorama(Frame &frame) const;
    std::vector<hui::ui::Hint> panorama_hints() const;
    pano::View pano_;
    std::string pn_game_;      // the selected game's key
    bool pn_details_ = false;  // its statuses beside its actions
    bool pn_drive_ = false;    // a drive has the focus: its games' sizes show
    int pn_picker_ = -1;       // search: -1 closed, 0 the field, then the locations
    ui::Ease pn_picker_in_;
    bool pn_bar_open_ = false;
    int pn_bar_focus_ = 0;
    ui::Ease pn_bar_in_;

    // The page.
    int section_ = 0; // 0 games, 1 storage
    int view_ = 0;
    std::string query_;
    std::string location_;
    std::string format_;
    std::vector<int> rows_; // indices into the snapshot's games
    std::uint64_t serial_ = ~0ull;
    Kind focus_kind_ = Kind::refresh;
    int focus_index_ = 0;
    std::string drive_id_; // the drive the Storage section shows
    bool keyboard_open_ = false;
    hui::ui::Keyboard keyboard_;
    ui::Ease keyboard_in_;
    Picker picker_;
    Kind picking_ = Kind::location;
    ui::Ease scroll_;
    std::vector<float> focus_amount_; // parallel to page_controls(), by position
    // The details panel and its confirmation step.
    std::string selected_;     // title ID
    std::string selected_key_; // source key
    int mode_ = 0;             // 0 none, 1 unmount, 2 copy, 3 move
    std::string destination_id_;
    Kind panel_kind_ = Kind::action;
    int panel_index_ = 0;
    ui::Ease panel_in_;
    std::uint64_t action_serial_ = 0;
    std::string error_;
    // The scan dialog.
    bool scan_open_ = false;
    int scan_button_ = 0;
};

class DownloadsScreen final : public Screen
{
  public:
    explicit DownloadsScreen(Context &context);
    void focus() override;
    Intent update(const InputFrame &input, float dt, Feedback &feedback, bool focused) override;
    void draw(Frame &frame, bool focused) const override;
    std::vector<hui::ui::Hint> hints() const override;
    bool modal() const override
    {
        return dialog_open_ || password_open_ || cn_sheet_ || xb_menu_.open || pn_bar_open_;
    }
    // Shows one job: switches to its view and moves the focus to it.
    void show_job(const std::string &job_id);

  private:
    struct RowAction
    {
        const char *label;
        const char *action; // API action, or "dialog"
    };
    std::vector<const Job *> visible() const;
    std::vector<RowAction> actions_for(const Job &job) const;
    int count(JobView view) const;
    Intent update_store(const InputFrame &input, float dt, Feedback &feedback, bool focused);

    JobView view_ = JobView::active;
    bool on_views_ = true; // the focus is on the view switcher
    int row_ = 0;
    int button_ = 0;
    std::string pending_job_; // show_job() before the queue arrived
    bool dialog_open_ = false;
    bool dialog_cancelled_ = false;
    bool dialog_forget_ = false;
    bool dialog_pending_ = false;
    std::string dialog_error_;
    int dialog_button_ = 0;
    std::string dialog_job_;
    bool password_open_ = false;
    std::string password_, password_job_;
    hui::ui::Keyboard password_keyboard_;
    ui::Ease scroll_;
    ui::Ease dialog_in_;

    // Crossbar (downloads_looks.cpp): Active first, then Failed, Finished and
    // Cancelled as folders; Cross opens a download's actions.
    struct XbAction
    {
        icons::Icon icon;
        std::string label;
        std::string action; // API action, "dialog", "move-up", "library", "game"
        std::string sub;
        bool enabled = true;
        bool danger = false;
    };
    xb::List crossbar_list() const;
    std::vector<XbAction> crossbar_actions(const Job &job) const;
    Intent run_action(const Job &job, const std::string &action, Feedback &feedback);
    Intent update_crossbar(const InputFrame &input, float dt, Feedback &feedback, bool focused);
    void draw_crossbar(Frame &frame) const;
    std::vector<hui::ui::Hint> crossbar_hints() const;
    xb::View xb_;
    xb::Menu xb_menu_;
    int xb_folder_ = -1; // -1 the section; else a JobView other than active

    // Cinema (downloads.cpp): the focused download on the stage, the view's
    // strip beside it; L1 and R1 change the view.
    std::vector<const Job *> cinema_jobs() const;
    Intent update_cinema(const InputFrame &input, float dt, Feedback &feedback, bool focused);
    void draw_cinema(Frame &frame) const;
    std::vector<hui::ui::Hint> cinema_hints() const;
    JobView cn_view_ = JobView::active;
    int cn_at_[4] = {};
    bool cn_sheet_ = false; // every action, as cards
    int cn_action_ = 0;
    bool cn_buttons_ = false; // the stage's buttons, above the download strip
    bool cn_focused_ = true;
    ui::Ease cn_sheet_in_;
    mutable std::string cn_hint_; // the first action, for the hint

    // Panorama (downloads.cpp): one page of groups, Active, Failed, Finished
    // and Cancelled; each group's first download large with its actions
    // beside it (the running one shows its percentage instead), the others
    // as wide tiles. Cross runs a download's first action, Options raises the
    // app bar with all of them; L1 and R1 jump between the groups.
    struct PnEntry
    {
        char kind;     // 'j' a download, 'a' an action of the first, 'l' the limit, 'f' free space, 'b' Browse
        int index = 0; // into the group's downloads, or its first one's actions
    };
    pano::Page panorama_page() const;
    const Job *panorama_job(const pano::Page &page, std::string *action) const;
    pano::AppBar panorama_bar(const Job &job) const;
    Intent update_panorama(const InputFrame &input, float dt, Feedback &feedback, bool focused);
    void draw_panorama(Frame &frame) const;
    std::vector<hui::ui::Hint> panorama_hints() const;
    pano::View pano_;
    bool pn_bar_open_ = false;
    int pn_bar_focus_ = 0;
    ui::Ease pn_bar_in_;
    mutable std::string pn_hint_;   // the first action, for the hint
    mutable std::string pn_groups_; // "Active · Failed · Finished · Cancelled"

  public:
    bool page(int step) override;
    bool hints_high() const override
    {
        return password_open_;
    }
    bool options_used() const override
    {
        return look::is(look::Interface::cinema) && !cinema_jobs().empty();
    }
};

class GamePage final : public Screen
{
  public:
    explicit GamePage(Context &context);
    void open(const std::string &game_id);
    // Opens on About this game (from an Actions menu).
    void show_about();
    // The section the game was opened from (Crossbar shows it in the bar).
    void set_section(int section)
    {
        section_ = section;
    }
    const std::string &game_id() const
    {
        return game_id_;
    }
    Intent update(const InputFrame &input, float dt, Feedback &feedback, bool focused) override;
    void draw(Frame &frame, bool focused) const override;
    std::string backdrop_game() const override
    {
        return game_id_;
    }
    std::vector<hui::ui::Hint> hints() const override;

    bool modal() const override { return sheet_ != Sheet::none; }

  private:
    enum class Sheet : std::uint8_t { none, downloads, source, delivery, storage, about };
    enum class Slot : std::uint8_t { primary, favourite, about, source, delivery, drive,
                                     choice, again, related, reading, back, close };
    struct Control
    {
        Slot slot;
        int index;
        ui::Rect rect;
        bool enabled;
        bool fixed = false;
    };
    struct Layout
    {
        std::vector<Control> controls;
        float end = 0.0f;
    };
    struct Offer
    {
        CollectionState collection;
        bool show_download = false;
        const Drive *drive = nullptr;
        SpacePlan plan;
    };
    Offer offer(const Release &release) const;
    bool primary_enabled(const Release &release, const Offer &offer) const;
    Layout compose(hui::gfx::DrawList *list, Sheet sheet, bool focused) const;
    void show_sheet(Sheet sheet, Slot preferred = Slot::primary, int index = 0);
    void back_sheet();
    Intent activate(const Control &control, const Game &game, const Release &release,
                    const Offer &offer, Feedback &feedback);
    const Drive *drive() const;
    std::string delivery_note(const Release &release) const;
    std::string game_id_;
    int option_ = 0;
    int focus_ = 0;
    Sheet sheet_ = Sheet::none;
    std::string drive_id_;
    bool download_again_ = false;
    bool waiting_ = false;
    bool torbox_ = false;
    std::uint64_t action_serial_ = 0;
    std::uint64_t favourite_serial_ = 0;
    std::string error_;
    ui::Ease page_scroll_;
    ui::Ease sheet_scroll_;
    int section_ = 1; // section::discover

    // Crossbar (game_page.cpp): the download and its choices as rows; a choice
    // opens a menu of its values.
    struct XbItem
    {
        Slot slot;
        int index = 0;
        icons::Icon icon = icons::Icon::info;
        std::string title, value, sub, text;
        bool enabled = true;
        bool primary = false;
    };
    std::vector<std::string> situation(const Release &release, const Offer &now) const;
    bool follow(Intent &intent);
    std::vector<XbItem> crossbar_items() const;
    std::vector<xb::MenuItem> crossbar_choices(Slot slot) const;
    Intent update_crossbar(const InputFrame &input, float dt, Feedback &feedback, bool focused);
    void draw_crossbar(Frame &frame) const;
    xb::View xb_;
    xb::Menu xb_menu_;
    Slot crossbar_panel() const;

    // Cinema (game_page.cpp): the title over the art, the game's line, a few
    // lines about it and its buttons. Download raises a sheet with the start
    // button and notes on the left, versions, delivery and drives as cards on
    // the right (as Store's page raises its options). About, the provider's
    // notes and every version are views of the same sheet.
    enum class CnZone : std::uint8_t
    {
        buttons,
        extras,
        versions,
        delivery,
        drives,
    };
    enum class CnView : std::uint8_t
    {
        main,
        about,
        notes,
    };
    struct CnButton
    {
        Slot slot;
        cn::Button look;
    };
    std::vector<CnButton> cinema_buttons(bool extras) const;
    int cinema_count(CnZone zone) const;
    Intent update_cinema(const InputFrame &input, float dt, Feedback &feedback, bool focused);
    void draw_cinema(Frame &frame) const;
    std::vector<hui::ui::Hint> cinema_hints() const;
    CnZone cn_zone_ = CnZone::buttons;
    int cn_at_[5] = {};
    CnView cn_view_ = CnView::main;
    bool cn_sheet_ = false;              // the download's sheet is up
    float cn_scroll_ = 0.0f;             // About or the notes, scrolled by Options
    mutable float cn_scroll_end_ = 0.0f; // how far they can scroll, as last drawn
    Slot xb_choosing_ = Slot::source;
    float xb_scroll_ = 0.0f;             // the About or notes panel, scrolled by Options
    mutable float xb_scroll_end_ = 0.0f; // how far it can scroll, as last drawn

    // Panorama (game_page.cpp): the game's own panorama. The download, how
    // and where it downloads and which version are groups of tiles (the
    // chosen ones with a corner check); the space, the provider's steps,
    // About and the notes are groups of text. About and Source notes slide
    // the page to their group; Options scrolls it.
    enum class PnView : std::uint8_t
    {
        main,
        about,
        notes,
    };
    pano::Page panorama_page() const;
    Intent update_panorama(const InputFrame &input, float dt, Feedback &feedback, bool focused);
    void draw_panorama(Frame &frame) const;
    std::vector<hui::ui::Hint> panorama_hints() const;
    pano::View pn_;
    PnView pn_view_ = PnView::main;
    bool pn_space_ = false; // a drive has the focus: the space group shows
    float pn_scroll_ = 0.0f;
    mutable float pn_scroll_end_ = 0.0f;
};

// Signing in to Archive.org (App settings → Download sources): the email and
// password over the screen, typed with the look's keyboard. The console keeps
// only Archive.org's session; this form forgets both when it closes.
class ArchiveSignIn
{
  public:
    explicit ArchiveSignIn(Context &context);
    void open();
    // Previews and tests: the rows as if typed.
    void fill(std::string email, std::string password)
    {
        email_ = std::move(email);
        password_ = std::move(password);
    }
    bool active() const
    {
        return open_;
    }
    enum class Result : std::uint8_t
    {
        none,
        closed,
        signed_in,
    };
    Result update(const InputFrame &input, float dt, Feedback &feedback);
    void draw(Frame &frame) const;
    std::vector<hui::ui::Hint> hints() const;

  private:
    enum class Field : std::uint8_t
    {
        email,
        password,
        buttons,
    };
    void close();
    Context &ctx_;
    bool open_ = false;
    std::string email_, password_;
    int row_ = 0;
    int button_ = 0; // Sign in, Cancel
    bool typing_ = false;
    Field typing_field_ = Field::email;
    hui::ui::Keyboard keyboard_;
    bool waiting_ = false;
    std::uint64_t serial_ = 0;
    std::string error_;
    ui::Ease in_;
};

// App settings (the browser's Sources, Pairing, Storage, Game catalogue,
// Update / reinstall and TV app panels) as one page: sections on the left,
// the chosen one on the right. Updates keep the two programs apart: this TV
// app, and the download service (Orbit, which keeps running after it closes).
class SettingsScreen final : public Screen
{
  public:
    enum class Section : std::uint8_t
    {
        sources,
        storage,
        pairing,
        catalogue,
        updates,
        zero,
        appearance,
        more,
        count,
    };
    explicit SettingsScreen(Context &context);
    // Opens on a section; content puts the focus inside it at once.
    void open(Section section, bool content = false);
    void close();
    Section section() const
    {
        return section_;
    }
    Intent update(const InputFrame &input, float dt, Feedback &feedback, bool focused) override;
    void draw(Frame &frame, bool focused) const override;
    std::vector<hui::ui::Hint> hints() const override;
    // Previews and tests.
    void confirm_restart()
    {
        confirm_restart_ = true;
    }
    void open_archive_sign_in(std::string email, std::string password)
    {
        archive_sign_in_.open();
        archive_sign_in_.fill(std::move(email), std::move(password));
    }

  private:
    enum class Kind : std::uint8_t
    {
        source,      // index into the source options
        acknowledge, // the download notice
        save_sources,
        drive, // 0 chooses automatically; then one per drive
        refresh_catalogue,
        tv_check,
        tv_install,
        tv_replace, // install over a copy the owner placed, after confirming
        tv_keep,
        service_check,
        service_install,
        service_restart,
        restart_confirm,
        restart_cancel,
        restart_retry,
        open_browser,
        zero_toggle,
        archives_toggle,
        archive_sign_in,
        archive_sign_out, // asks first
        archive_sign_out_confirm,
        archive_sign_out_cancel,
        download_recovery_less,
        download_recovery_more,
        download_limit_less,
        download_limit_more,
        // Appearance (kept on this console only)
        interface_card,
        motion,
        accent,
        ribbons,
        art_behind,
    };
    struct Control
    {
        Kind kind;
        int index = 0;
        int row = 0;
        ui::Rect rect;
        bool enabled = true;
    };
    struct Pane; // lays out a section and, when drawing, draws it
    void build(Pane &pane) const;
    void build_sources(Pane &pane) const;
    void build_storage(Pane &pane) const;
    void build_pairing(Pane &pane) const;
    void build_catalogue(Pane &pane) const;
    void build_updates(Pane &pane) const;
    void build_more(Pane &pane) const;
    void build_zero(Pane &pane) const;
    void build_appearance(Pane &pane) const;
    std::vector<Control> controls(float *height = nullptr) const;
    void activate(const Control &control, Intent &intent, Feedback &feedback);
    void load_draft();
    bool update_available() const;

    // Crossbar (settings_looks.cpp): the sections as folders with their
    // values; inside, a row per setting and a menu for its values.
    struct XbEntry
    {
        char kind;
        int index;
    };
    std::vector<XbEntry> crossbar_entries() const;
    std::vector<XbEntry> section_entries() const;
    std::string crossbar_value(Section section) const;
    xb::List crossbar_list() const;
    xb::List rows_of(const std::vector<XbEntry> &entries) const;
    std::vector<xb::MenuItem> crossbar_menu(xb::MenuFoot *foot = nullptr) const;
    std::string crossbar_menu_title() const;
    bool crossbar_can_check() const;
    xb::Band crossbar_band() const;
    bool crossbar_band_open() const;
    void act(Kind kind, int index, Intent &intent, Feedback &feedback);
    void band_input(const InputFrame &input, Intent &intent, Feedback &feedback);
    void menu_choose(int chosen, Intent &intent, Feedback &feedback);
    void press(const std::string &key, bool off, const InputFrame &input, Intent &intent, Feedback &feedback);
    void open_section(Section section);
    Intent update_crossbar(const InputFrame &input, float dt, Feedback &feedback, bool focused);
    void draw_crossbar(Frame &frame) const;
    std::vector<hui::ui::Hint> crossbar_hints() const;
    xb::View xb_;
    xb::Menu xb_menu_;
    bool xb_open_ = false; // inside a section, rather than on the list of them
    int xb_menu_kind_ = 0; // Default drive, limit, TV app, service, Interface, Ribbons, Motion, Accent, Art behind, Archive.org sign-out
    int xb_band_ = 0;
    ui::Ease xb_band_in_;

    // Cinema (settings_looks.cpp): the sections as a table of contents on
    // the left, the section's rows in a panel; values in a sheet of cards.
    Intent update_cinema(const InputFrame &input, float dt, Feedback &feedback, bool focused);
    void draw_cinema(Frame &frame) const;
    std::vector<hui::ui::Hint> cinema_hints() const;
    bool cn_rows_ = false; // the focus in the panel's rows, not the contents
    int cn_row_ = 0;

    // Panorama (settings_looks.cpp): the sections as lowercase pivots across,
    // the current one's settings as a big list below; a setting's values in a
    // full-screen list (an interface shown behind a narrow one); questions as
    // message bands. L1 and R1 go through the pivots, then on to the next section.
    Intent update_panorama(const InputFrame &input, float dt, Feedback &feedback, bool focused);
    void draw_panorama(Frame &frame) const;
    std::vector<hui::ui::Hint> panorama_hints() const;
    std::vector<pano::Item> panorama_items() const;
    bool pn_rows_ = false; // the focus in the list, not on the pivots
    int pn_row_ = 0;

  public:
    bool modal() const override
    {
        // A setting's values, a question or the sign-in keep L1 and R1 for themselves.
        return xb_menu_.open || crossbar_band_open() || archive_sign_in_.active();
    }
    bool page(int step) override;

  private:

    Section section_ = Section::sources;
    bool in_content_ = false;
    Kind focus_kind_ = Kind::source;
    int focus_index_ = 0;
    int last_row_ = 0; // where the focus was, if its control goes away
    float last_x_ = 0.0f;
    // Sources: the choice being made, until it is saved.
    std::vector<std::string> draft_;
    bool draft_acknowledged_ = false;
    bool first_setup_ = false; // saving finishes setting up Orbit
    // Updates: the confirmation steps.
    bool confirm_restart_ = false;
    bool confirm_replace_ = false;
    std::uint64_t setting_serial_ = 0;
    std::string error_;
    Setting error_setting_ = Setting::none;
    // Download sources: the Archive.org sign-in form, and the sign-out question.
    ArchiveSignIn archive_sign_in_;
    bool archive_sign_out_ = false;
    void draw_look(Frame &frame, bool focused) const;
    float age_ = 0.0f;
    ui::Ease scroll_;
    std::vector<float> focus_amount_; // parallel to controls(), by position
    float nav_focus_[static_cast<int>(Section::count)] = {};
};

// The whole app: top bar, tabs, the game page, status screens and dialogs.
class App
{
  public:
    explicit App(Context &context);
    void update(const InputFrame &input, float dt, Feedback &feedback);
    void draw(Frame &frame) const;
    // Preview and tests: jump straight to a place.
    void show_tab(int tab);
    // Crossbar, Cinema and Panorama: a section (section:: indices) by itself.
    void show_section(int section);
    void open_game(const std::string &game_id);
    void open_settings(SettingsScreen::Section section = SettingsScreen::Section::sources,
                       bool content = false);
    SettingsScreen &settings()
    {
        return settings_;
    }
    BrowseScreen &browse()
    {
        return browse_;
    }
    LibraryScreen &library()
    {
        return library_;
    }
    int current_tab() const
    {
        return tab_;
    }
    int current_section() const { return section(); }
    DownloadsScreen &downloads()
    {
        return downloads_;
    }

  private:
    enum class Area : std::uint8_t
    {
        top,
        content,
    };
    Screen &screen();
    const Screen &screen() const;
    void draw_top_bar(hui::gfx::DrawList &list) const;
    void draw_game_header(hui::gfx::DrawList &list) const;
    bool status_offline(const Snapshot &state) const;
    void draw_status(Frame &frame) const;
    bool update_badge() const;    // an update for the TV app or the download service
    bool content_blocked() const; // setup or an empty catalogue on Discover/Browse
    void draw_blocked(hui::gfx::DrawList &list) const;
    void handle_intent(const Intent &intent, Feedback &feedback);
    void update_provider(const InputFrame &input, Feedback &feedback);
    void draw_provider(Frame &frame) const;
    bool provider_actionable() const;
    // Crossbar, Cinema and Panorama: sections instead of the top bar.
    int section() const;
    int adjacent_section(int step) const;
    bool update_section_nav(const InputFrame &input, float dt, Feedback &feedback);
    bool update_sections(const InputFrame &input, Feedback &feedback);
    void draw_backdrop_look(Frame &frame) const;
    // After switching interfaces: keep it, or go back (App settings → Appearance).
    bool update_keep(const InputFrame &input, Feedback &feedback);
    void draw_keep(Frame &frame) const;
    // The hints the screen shows now; and Controls, every button once, which
    // Options raises in the other interfaces when nothing there uses it.
    std::vector<hui::ui::Hint> current_hints() const;
    void draw_controls(Frame &frame) const;
    void draw_notice(hui::gfx::DrawList &list) const;

    Context &ctx_;
    DiscoverScreen discover_;
    BrowseScreen browse_;
    LibraryScreen library_;
    DownloadsScreen downloads_;
    GamePage game_;
    SettingsScreen settings_;
    int tab_ = tab::discover;
    int last_tab_ = tab::discover; // to tell the Library when it is left
    Area area_ = Area::content;
    int top_index_ = 0; // the tabs, then search and settings
    bool game_open_ = false;
    bool settings_open_ = false;
    bool provider_open_ = false;
    bool provider_start_pending_ = false;
    int provider_focus_ = 1; // Back first, so returning cannot cancel by accident
    std::string provider_seen_;
    bool update_told_ = false; // the update toast shows once per launch
    void show_toast(std::string text, float seconds = 2.8f, bool update = false);
    std::string toast_;
    float toast_time_ = 0.0f;
    float toast_length_ = 2.8f;
    bool toast_update_ = false; // an update notice, not a confirmation
    bool toast_favourite_ = false; // a favourite added or removed (the other interfaces)
    std::uint64_t favourite_serial_ = 0;
    std::size_t favourite_count_ = 0;
    bool favourite_waiting_ = false; // a favourite request is out; favourite_count_ is from before it
    std::string backdrop_;
    std::string previous_backdrop_;
    std::string settling_; // the art Discover wants, while the focus rests
    float settled_for_ = 0.0f;
    ui::Ease backdrop_in_;
    ui::Ease game_in_;
    std::vector<ui::Ease> top_focus_;
    float status_age_ = 0.0f;
    bool search_open_ = false; // the Search section (Browse with its keyboard)
    int keep_button_ = 0; // Keep, then Switch back
    ui::Ease keep_in_;
    bool controls_open_ = false;
    ui::Ease controls_in_;
    float notice_time_ = 0.0f; // Store's one-time notice about Appearance
};

} // namespace orbit
