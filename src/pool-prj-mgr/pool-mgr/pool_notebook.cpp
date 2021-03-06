#include "canvas/canvas.hpp"
#include "pool_notebook.hpp"
#include "widgets/pool_browser.hpp"
#include "dialogs/pool_browser_dialog.hpp"
#include "widgets/pool_browser_parametric.hpp"
#include "util/util.hpp"
#include "pool-update/pool-update.hpp"
#include "pool-prj-mgr/pool-prj-mgr-app_win.hpp"
#include "duplicate/duplicate_window.hpp"
#include "common/object_descr.hpp"
#include "pool_remote_box.hpp"
#include "pool_merge_dialog.hpp"
#include <git2.h>
#include "util/autofree_ptr.hpp"
#include "pool-prj-mgr/pool-prj-mgr-app.hpp"
#include "pool_update_error_dialog.hpp"
#include "pool_settings_box.hpp"
#include "pool/pool_manager.hpp"
#include "pool/pool_parametric.hpp"
#include "part_wizard/part_wizard.hpp"
#include <thread>
#include "nlohmann/json.hpp"

#ifdef G_OS_WIN32
#undef ERROR
#endif

namespace horizon {

void PoolNotebook::pool_updated(bool success)
{
    pool_updating = false;
    if (pool_update_error_queue.size()) {
        auto top = dynamic_cast<Gtk::Window *>(get_ancestor(GTK_TYPE_WINDOW));
        PoolUpdateErrorDialog dia(top, pool_update_error_queue);
        dia.run();
    }
    appwin->set_pool_updating(false, success);
    pool.clear();
    for (auto &br : browsers) {
        br.second->search();
    }
    for (auto &br : browsers_parametric) {
        br.second->search();
    }
    auto procs = appwin->get_processes();
    for (auto &it : procs) {
        it.second->reload();
    }
    if (success && pool_update_done_cb) {
        pool_update_done_cb();
        pool_update_done_cb = nullptr;
    }
    if (settings_box)
        settings_box->pool_updated();
    if (part_wizard)
        part_wizard->reload();
}

PoolNotebook::~PoolNotebook()
{
    appwin->pool = nullptr;
    appwin->pool_parametric = nullptr;
}

Gtk::Button *PoolNotebook::add_action_button(const std::string &label, Gtk::Box *bbox, sigc::slot0<void> cb)
{
    auto bu = Gtk::manage(new Gtk::Button(label));
    bbox->pack_start(*bu, false, false, 0);
    bu->signal_clicked().connect(cb);
    return bu;
}

Gtk::Button *PoolNotebook::add_action_button(const std::string &label, Gtk::Box *bbox, class PoolBrowser *br,
                                             sigc::slot1<void, UUID> cb)
{
    auto bu = Gtk::manage(new Gtk::Button(label));
    bbox->pack_start(*bu, false, false, 0);
    bu->signal_clicked().connect([br, cb] { cb(br->get_selected()); });
    br->signal_selected().connect([bu, br] { bu->set_sensitive(br->get_selected()); });
    bu->set_sensitive(br->get_selected());
    return bu;
}

void PoolNotebook::add_preview_stack_switcher(Gtk::Box *obox, Gtk::Stack *stack)
{
    auto bbox = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL));
    bbox->get_style_context()->add_class("linked");
    auto rb_graphical = Gtk::manage(new Gtk::RadioButton("Preview"));
    rb_graphical->set_mode(false);
    auto rb_text = Gtk::manage(new Gtk::RadioButton("Info"));
    rb_text->set_mode(false);
    rb_text->join_group(*rb_graphical);
    rb_graphical->set_active(true);
    rb_graphical->signal_toggled().connect([this, rb_graphical, stack] {
        if (rb_graphical->get_active()) {
            stack->set_visible_child("preview");
        }
        else {
            stack->set_visible_child("info");
        }
    });
    bbox->pack_start(*rb_graphical, true, true, 0);
    bbox->pack_start(*rb_text, true, true, 0);
    bbox->set_margin_start(4);
    bbox->show_all();
    obox->pack_end(*bbox, false, false, 0);
}

class SetReset {
public:
    SetReset(bool &v) : value(v)
    {
        value = true;
    }

    ~SetReset()
    {
        value = false;
    }

private:
    bool &value;
};

PoolNotebook::PoolNotebook(const std::string &bp, class PoolProjectManagerAppWindow *aw)
    : Gtk::Notebook(), base_path(bp), pool(bp), pool_parametric(bp), appwin(aw)
{
    appwin->pool = &pool;
    appwin->pool_parametric = &pool_parametric;
    appwin->signal_process_exited().connect(sigc::track_obj(
            [this](std::string filename, int status, bool modified) {
                if (modified)
                    pool_update();
            },
            *this));

    {

        pool_update_dispatcher.connect([this] {
            if (in_pool_update_handler)
                return;
            SetReset rst(in_pool_update_handler);
            std::lock_guard<std::mutex> guard(pool_update_status_queue_mutex);
            while (pool_update_status_queue.size()) {
                std::string last_filename;
                std::string last_msg;
                PoolUpdateStatus last_status;

                std::tie(last_status, last_filename, last_msg) = pool_update_status_queue.front();

                appwin->set_pool_update_status_text(last_filename);
                if (last_status == PoolUpdateStatus::DONE) {
                    pool_updated(true);
                    pool_update_n_files_last = pool_update_n_files;
                }
                else if (last_status == PoolUpdateStatus::FILE) {
                    pool_update_last_file = last_filename;
                    pool_update_n_files++;
                    if (pool_update_n_files_last) {
                        appwin->set_pool_update_progress((float)pool_update_n_files / pool_update_n_files_last);
                    }
                    else {
                        appwin->set_pool_update_progress(-1);
                    }
                }
                else if (last_status == PoolUpdateStatus::FILE_ERROR) {
                    pool_update_error_queue.emplace_back(last_status, last_filename, last_msg);
                }
                else if (last_status == PoolUpdateStatus::ERROR) {
                    appwin->set_pool_update_status_text(last_msg + " Last file: " + pool_update_last_file);
                    pool_updated(false);
                }
                pool_update_status_queue.pop_front();
            }
        });
    }
    remote_repo = Glib::build_filename(base_path, ".remote");
    if (!Glib::file_test(remote_repo, Glib::FILE_TEST_IS_DIR)) {
        remote_repo = "";
    }

    construct_units();
    construct_symbols();
    construct_entities();
    construct_padstacks();
    construct_packages();
    construct_parts();
    construct_frames();

    {
        if (PoolManager::get().get_pools().count(pool.get_base_path())) {
            settings_box = PoolSettingsBox::create(this, pool.get_base_path());
            pool_uuid = PoolManager::get().get_pools().at(pool.get_base_path()).uuid;

            settings_box->show();
            append_page(*settings_box, "Settings");
            settings_box->unreference();
        }
    }

    if (remote_repo.size()) {
        remote_box = PoolRemoteBox::create(this);

        remote_box->show();
        append_page(*remote_box, "Remote");
        remote_box->unreference();

        signal_switch_page().connect([this](Gtk::Widget *page, int page_num) {
            if (page == remote_box && !remote_box->prs_refreshed_once) {
                remote_box->handle_refresh_prs();
                remote_box->prs_refreshed_once = true;
            }
        });
    }

    for (const auto &it_tab : pool_parametric.get_tables()) {
        auto br = Gtk::manage(new PoolBrowserParametric(&pool, &pool_parametric, it_tab.first));
        br->show();
        add_context_menu(br);
        br->signal_activated().connect([this, br] { go_to(ObjectType::PART, br->get_selected()); });
        append_page(*br, "Param: " + it_tab.second.display_name);
        browsers_parametric.emplace(it_tab.first, br);
    }


    for (auto br : browsers) {
        add_context_menu(br.second);
    }
}

void PoolNotebook::add_context_menu(PoolBrowser *br)
{
    ObjectType ty = br->get_type();
    br->add_context_menu_item("Delete", [this, ty](const UUID &uu) { handle_delete(ty, uu); });
    br->add_context_menu_item("Copy path", [this, ty](const UUID &uu) { handle_copy_path(ty, uu); });
}

void rmdir_recursive(const std::string &dirname)
{
    Glib::Dir dir(dirname);
    std::list<std::string> entries(dir.begin(), dir.end());
    for (const auto &it : entries) {
        auto filename = Glib::build_filename(dirname, it);
        if (Glib::file_test(filename, Glib::FILE_TEST_IS_DIR)) {
            rmdir_recursive(filename);
        }
        else {
            Gio::File::create_for_path(filename)->remove();
        }
    }
    Gio::File::create_for_path(dirname)->remove();
}

void PoolNotebook::handle_delete(ObjectType ty, const UUID &uu)
{
    auto top = dynamic_cast<Gtk::Window *>(get_ancestor(GTK_TYPE_WINDOW));
    Gtk::MessageDialog md(*top, "Permanently delete " + object_descriptions.at(ty).name + "?", false /* use_markup */,
                          Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_NONE);
    md.add_button("Cancel", Gtk::RESPONSE_CANCEL);
    md.add_button("Delete", Gtk::RESPONSE_OK)->get_style_context()->add_class("destructive-action");
    if (md.run() == Gtk::RESPONSE_OK) {
        auto filename = pool.get_filename(ty, uu);
        if (ty == ObjectType::PACKAGE) {
            auto dir = Glib::path_get_dirname(filename);
            rmdir_recursive(dir);
        }
        else {
            Gio::File::create_for_path(filename)->remove();
        }
        pool_update();
    }
}

void PoolNotebook::handle_copy_path(ObjectType ty, const UUID &uu)
{
    auto filename = pool.get_filename(ty, uu);
    auto clip = Gtk::Clipboard::get();
    clip->set_text(filename);
}

void PoolNotebook::go_to(ObjectType type, const UUID &uu)
{
    browsers.at(type)->go_to(uu);
    static const std::map<ObjectType, int> pages = {
            {ObjectType::UNIT, 0},     {ObjectType::SYMBOL, 1},  {ObjectType::ENTITY, 2},
            {ObjectType::PADSTACK, 3}, {ObjectType::PACKAGE, 4}, {ObjectType::PART, 5},
    };
    set_current_page(pages.at(type));
}

void PoolNotebook::show_duplicate_window(ObjectType ty, const UUID &uu)
{
    if (!uu)
        return;
    if (!duplicate_window) {
        duplicate_window = new DuplicateWindow(&pool, ty, uu);
        duplicate_window->present();
        duplicate_window->signal_hide().connect([this] {
            if (duplicate_window->get_duplicated()) {
                pool_update();
            }
            delete duplicate_window;
            duplicate_window = nullptr;
        });
    }
    else {
        duplicate_window->present();
    }
}

bool PoolNotebook::get_close_prohibited() const
{
    return part_wizard || pool_updating || duplicate_window;
}

void PoolNotebook::prepare_close()
{
    if (remote_box)
        remote_box->prs_refreshed_once = true;
    closing = true;
}

bool PoolNotebook::get_needs_save() const
{
    if (settings_box)
        return settings_box->get_needs_save();
    else
        return false;
}

void PoolNotebook::save()
{
    if (settings_box)
        settings_box->save();
}

void PoolNotebook::pool_update_thread()
{
    std::cout << "hello from thread" << std::endl;

    try {
        horizon::pool_update(pool.get_base_path(),
                             [this](PoolUpdateStatus st, std::string filename, std::string msg) {
                                 {
                                     std::lock_guard<std::mutex> guard(pool_update_status_queue_mutex);
                                     pool_update_status_queue.emplace_back(st, filename, msg);
                                 }
                                 pool_update_dispatcher.emit();
                             },
                             true);
    }
    catch (const std::runtime_error &e) {
        {
            std::lock_guard<std::mutex> guard(pool_update_status_queue_mutex);
            pool_update_status_queue.emplace_back(PoolUpdateStatus::ERROR, "",
                                                  std::string("runtime exception: ") + e.what());
        }
        pool_update_dispatcher.emit();
    }
    catch (const std::exception &e) {
        {
            std::lock_guard<std::mutex> guard(pool_update_status_queue_mutex);
            pool_update_status_queue.emplace_back(PoolUpdateStatus::ERROR, "",
                                                  std::string("generic exception: ") + e.what());
        }
        pool_update_dispatcher.emit();
    }
    catch (const Glib::FileError &e) {
        {
            std::lock_guard<std::mutex> guard(pool_update_status_queue_mutex);
            pool_update_status_queue.emplace_back(PoolUpdateStatus::ERROR, "",
                                                  std::string("file exception: ") + e.what());
        }
        pool_update_dispatcher.emit();
    }
    catch (...) {
        {
            std::lock_guard<std::mutex> guard(pool_update_status_queue_mutex);
            pool_update_status_queue.emplace_back(PoolUpdateStatus::ERROR, "", "unknown exception");
        }
        pool_update_dispatcher.emit();
    }
}

void PoolNotebook::pool_update(std::function<void()> cb)
{
    if (closing)
        return;

    if (pool_updating)
        return;

    appwin->set_pool_updating(true, true);
    pool_update_n_files = 0;
    pool_updating = true;
    pool_update_done_cb = cb;
    pool_update_status_queue.clear();
    pool_update_error_queue.clear();
    std::thread thr(&PoolNotebook::pool_update_thread, this);
    thr.detach();
}
} // namespace horizon
