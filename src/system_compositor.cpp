/*
 * Copyright © 2013 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Robert Ancell <robert.ancell@canonical.com>
 */


#include "system_compositor.h"
#include "screen_state_handler.h"
#include "powerkey_handler.h"

// Qt headers will introduce a #define of "signals"
// but some mir headers use "signals" as a variable name in
// method declarations
#undef signals

#include <mir/run_mir.h>
#include <mir/abnormal_exit.h>
#include <mir/compositor/compositor_id.h>
#include <mir/compositor/scene.h>
#include <mir/compositor/scene_element.h>
#include <mir/default_server_configuration.h>
#include <mir/options/default_configuration.h>
#include <mir/frontend/shell.h>
#include <mir/scene/surface.h>
#include <mir/scene/surface_coordinator.h>
#include <mir/scene/session.h>
#include <mir/server_status_listener.h>
#include <mir/shell/focus_controller.h>
#include <mir/input/cursor_listener.h>
#include <mir/input/composite_event_filter.h>
#include <mir/main_loop.h>

#include <cerrno>
#include <iostream>
#include <sys/stat.h>
#include <thread>
#include <regex.h>
#include <GLES2/gl2.h>
#include <boost/algorithm/string.hpp>
#include <QCoreApplication>

namespace geom = mir::geometry;
namespace mc = mir::compositor;
namespace msh = mir::shell;
namespace msc = mir::scene;
namespace mf = mir::frontend;
namespace mg = mir::graphics;
namespace mi = mir::input;
namespace mo = mir::options;
namespace po = boost::program_options;

class SystemCompositorSurface;

class SystemCompositorSession : public msc::Session
{
public:
    SystemCompositorSession(std::shared_ptr<msc::Session> const& self,
                            SystemCompositorShell *shell)
        : self{self}, shell{shell}, ready{false} {}

    // These are defined below, since they reference methods defined in other classes
    void mark_ready();
    void raise(std::shared_ptr<msc::SurfaceCoordinator> const& coordinator);
    std::shared_ptr<mf::Surface> get_surface(mf::SurfaceId surface) const override;
    mf::SurfaceId create_surface(msc::SurfaceCreationParameters const& params) override;

    bool is_ready() const
    {
        return ready;
    }

    std::shared_ptr<msc::Session> get_orig()
    {
        return self;
    }

    void destroy_surface(mf::SurfaceId surface) override
    {
        surfaces.erase(surface);
        self->destroy_surface(surface);
    }

    // This is just for convience of USC
    std::vector<std::shared_ptr<SystemCompositorSurface>> get_surfaces()
    {
        std::vector<std::shared_ptr<SystemCompositorSurface>> vector;
        for(auto surface: surfaces)
            vector.push_back(surface.second);
        return vector;
    }

    std::string name() const override {return self->name();}
    void hide() override {self->hide();}
    void show() override {self->show();}
    void send_display_config(mg::DisplayConfiguration const&config) override {self->send_display_config(config);}
    pid_t process_id() const override {return self->process_id();}
    void force_requests_to_complete() override {self->force_requests_to_complete();}
    void take_snapshot(msc::SnapshotCallback const& snapshot_taken) override {self->take_snapshot(snapshot_taken);}
    std::shared_ptr<msc::Surface> default_surface() const override {return self->default_surface();}
    void set_lifecycle_state(MirLifecycleState state) override {self->set_lifecycle_state(state);}

    void start_prompt_session() override { self->start_prompt_session(); }
    void stop_prompt_session() override { self->stop_prompt_session(); }

private:
    std::shared_ptr<msc::Session> const self;
    SystemCompositorShell *shell;
    std::map<mf::SurfaceId, std::shared_ptr<SystemCompositorSurface>> surfaces;
    bool ready;
};

class SystemCompositorSurface : public msc::Surface
{
public:
    SystemCompositorSurface(std::shared_ptr<msc::Surface> const& self,
                            SystemCompositorSession *session)
        : self{self}, session{session}, buffer_count{0} {}

    std::shared_ptr<msc::Surface> get_orig()
    {
        return self;
    }

    void swap_buffers(mg::Buffer* old_buffer, std::function<void(mg::Buffer* new_buffer)> complete) override
    {
        self->swap_buffers(old_buffer, complete);
        // Mark it available only after some content has been rendered
        if (old_buffer != NULL && !session->is_ready() && buffer_count++ == 1)
            session->mark_ready();
    }

    // mf::Surface methods
    void force_requests_to_complete() override {self->force_requests_to_complete();}
    geom::Size size() const override {return self->size();}
    MirPixelFormat pixel_format() const override {return self->pixel_format();}
    bool supports_input() const override {return self->supports_input();}
    int client_input_fd() const override {return self->client_input_fd();}
    int configure(MirSurfaceAttrib attrib, int value) override {return self->configure(attrib, value);}

    // msc::Surface methods
    std::string name() const override {return self->name();}
    geom::Size client_size() const override { return self->client_size(); };
    geom::Rectangle input_bounds() const override { return self->input_bounds(); };

    MirSurfaceType type() const override {return self->type();}
    MirSurfaceState state() const override {return self->state();}
    void hide() override {self->hide();}
    void show() override {self->show();}
    void move_to(geom::Point const& top_left) override {self->move_to(top_left);}
    geom::Point top_left() const override {return self->top_left();}
    void take_input_focus(std::shared_ptr<msh::InputTargeter> const& targeter) override {self->take_input_focus(targeter);}
    void set_input_region(std::vector<geom::Rectangle> const& region) override {self->set_input_region(region);}
    void allow_framedropping(bool allow) override {self->allow_framedropping(allow);}
    void resize(geom::Size const& size) override {self->resize(size);}
    void set_transformation(glm::mat4 const& t) override {self->set_transformation(t);}
    float alpha() const override {return self->alpha();}
    void set_alpha(float alpha) override {self->set_alpha(alpha);}
    void with_most_recent_buffer_do(std::function<void(mg::Buffer&)> const& exec) override {self->with_most_recent_buffer_do(exec);}

    // msc::Surface methods
    std::shared_ptr<mi::InputChannel> input_channel() const override {return self->input_channel();}
    void set_reception_mode(mi::InputReceptionMode mode) override { self->set_reception_mode(mode); }
    void add_observer(std::shared_ptr<msc::SurfaceObserver> const& observer) override {self->add_observer(observer);}
    void remove_observer(std::weak_ptr<msc::SurfaceObserver> const& observer) override {self->remove_observer(observer);}
    void consume(MirEvent const& event) override {self->consume(event);}
    void set_orientation(MirOrientation orientation) override {self->set_orientation(orientation);}

    // mi::Surface methods
    bool input_area_contains(geom::Point const& point) const override {return self->input_area_contains(point);}
    mi::InputReceptionMode reception_mode() const override { return self->reception_mode(); }

    // mg::Renderable methods
    std::unique_ptr<mg::Renderable> compositor_snapshot(void const* compositor_id) const override { return self->compositor_snapshot(compositor_id); }

    void set_cursor_image(std::shared_ptr<mg::CursorImage> const& image) override { self->set_cursor_image(image); }
    std::shared_ptr<mg::CursorImage> cursor_image() const override { return self->cursor_image(); }

private:
    std::shared_ptr<msc::Surface> const self;
    SystemCompositorSession *session;
    int buffer_count;
};

class SystemCompositorShell : public mf::Shell
{
public:
    SystemCompositorShell(SystemCompositor *compositor,
                          std::shared_ptr<mf::Shell> const& self,
                          std::shared_ptr<msh::FocusController> const& focus_controller,
                          std::shared_ptr<msc::SurfaceCoordinator> const& surface_coordinator)
        : compositor{compositor}, self(self), focus_controller{focus_controller}, surface_coordinator{surface_coordinator}, active_ever_used{false} {}

    std::shared_ptr<mf::Session> session_named(std::string const& name)
    {
        return sessions[name];
    }

    void set_active_session(std::string const& name)
    {
        active_name = name;
        update_session_focus();
    }

    void set_next_session(std::string const& name)
    {
        next_name = name;
        update_session_focus();
    }

    std::shared_ptr<SystemCompositorSession> get_active_session()
    {
        return active_session;
    }

    std::shared_ptr<SystemCompositorSession> get_next_session()
    {
        return next_session;
    }

    void update_session_focus()
    {
        auto spinner = sessions[spinner_name];
        auto next = sessions[next_name];
        auto active = sessions[active_name];
        bool need_spinner = false;

        if (spinner)
            spinner->hide();

        if (next && next->is_ready())
        {
            std::cerr << "Setting next focus to session " << next_name;
            next->hide();
            next->raise(surface_coordinator);
            next_session = next;
        }
        else if (!next_name.empty() && spinner)
        {
            std::cerr << "Setting next focus to spinner";
            spinner->raise(surface_coordinator);
            next_session = spinner;
            need_spinner = true;
        }
        else
        {
            need_spinner = !next_name.empty();
            std::cerr << "Setting no next focus";
            next_session.reset();
        }

        // If we are booting, we want to wait for next session to be ready to
        // go (it's a smoother experience if user is able to immediately swipe
        // greeter out of way -- enough that it's worth the tiny wait).  So
        // check here to see if next is all ready for us (or we've already
        // focused the active before in which case we're not booting anymore).
        bool next_all_set = next_name.empty() || (next && next->is_ready());
        if (active && active->is_ready() && (next_all_set || active_ever_used))
        {
            std::cerr << "; active focus to session " << active_name;
            focus_controller->set_focus_to(active); // raises and focuses
            active_ever_used = true;
            active_session = active;
            if (active_session == next_session)
                next_session.reset();
        }
        else if (!active_name.empty() && spinner)
        {
            std::cerr << "; active focus to spinner";
            focus_controller->set_focus_to(spinner); // raises and focuses
            active_session = spinner;
            next_session.reset();
            need_spinner = true;
        }
        else
        {
            need_spinner = need_spinner || !active_name.empty();
            std::cerr << "; no active focus";
            active_session.reset();
            next_session.reset();
        }

        if (active_session)
            active_session->show();
        if (next_session)
            next_session->show();

        if (need_spinner)
            compositor->ensure_spinner();
        else
            compositor->kill_spinner();

        std::cerr << std::endl;
    }

    std::shared_ptr<mf::PromptSession> start_prompt_session_for(
        std::shared_ptr<mf::Session> const& session,
        msc::PromptSessionCreationParameters const& params) override
    {
        return self->start_prompt_session_for(session, params);
    }

    void add_prompt_provider_for(
        std::shared_ptr<mf::PromptSession> const& prompt_session,
        std::shared_ptr<mf::Session> const& session) override
    {
        self->add_prompt_provider_for(prompt_session, session);
    }

    void stop_prompt_session(std::shared_ptr<mf::PromptSession> const& prompt_session) override
    {
        self->stop_prompt_session(prompt_session);
    }

private:
    std::shared_ptr<mf::Session> open_session(
        pid_t client_pid,
        std::string const& name,
        std::shared_ptr<mf::EventSink> const& sink) override
    {
        std::cerr << "Opening session " << name << std::endl;

        // We need msc::Session objects because that is what the focus controller
        // works with.  But the mf::Shell interface deals with mf::Session objects.
        // So we cast here since in practice, these objects are also msc::Sessions.
        auto orig = std::dynamic_pointer_cast<msc::Session>(self->open_session(client_pid, name, sink));
        if (!orig)
        {
            std::cerr << "Unexpected non-shell session" << std::endl;
            return std::shared_ptr<mf::Session>();
        }

        auto result = std::make_shared<SystemCompositorSession>(orig, this);
        sessions[name] = result;

        if (client_pid == compositor->get_spinner_pid())
            spinner_name = name;

        return result;
    }

    void close_session(std::shared_ptr<mf::Session> const& session_in) override
    {
        std::cerr << "Closing session " << session_in->name() << std::endl;

        auto session = std::dynamic_pointer_cast<SystemCompositorSession>(session_in);
        if (!session)
            return; // shouldn't happen

        if (session->name() == spinner_name)
            spinner_name = "";

        self->close_session(session->get_orig());
        sessions.erase(session->name());
    }

    mf::SurfaceId create_surface_for(
        std::shared_ptr<mf::Session> const& session,
        msc::SurfaceCreationParameters const& params) override
    {
        return self->create_surface_for(session, params);
    }

    void handle_surface_created(std::shared_ptr<mf::Session> const& session) override
    {
        self->handle_surface_created(session);

        // Opening a new surface will steal focus from our active surface, so
        // restore the focus if needed.
        update_session_focus();
    }

    SystemCompositor *compositor;
    std::shared_ptr<mf::Shell> const self;
    std::shared_ptr<msh::FocusController> const focus_controller;
    std::shared_ptr<msc::SurfaceCoordinator> const surface_coordinator;
    std::map<std::string, std::shared_ptr<SystemCompositorSession>> sessions;
    std::string active_name;
    std::string next_name;
    std::string spinner_name;
    std::shared_ptr<SystemCompositorSession> active_session;
    std::shared_ptr<SystemCompositorSession> next_session;
    bool active_ever_used;
};

class SurfaceSceneElement : public mc::SceneElement
{
public:
    SurfaceSceneElement(std::shared_ptr<mg::Renderable> renderable)
        : renderable_{renderable}
    {
    }

    std::shared_ptr<mg::Renderable> renderable() const override
    {
        return renderable_;
    }

    void rendered_in(mc::CompositorID) override {}
    void occluded_in(mc::CompositorID) override {}

private:
    std::shared_ptr<mg::Renderable> const renderable_;
};

class SystemCompositorScene : public mc::Scene
{
public:
    SystemCompositorScene(std::shared_ptr<mc::Scene> const& self)
        : self{self}, shell{nullptr} {}

    void set_shell(std::shared_ptr<SystemCompositorShell> const& the_shell)
    {
        shell = the_shell;
    }

    mc::SceneElementSequence scene_elements_for(mc::CompositorID id) override
    {
        if (shell == nullptr)
            return self->scene_elements_for(id);

        mc::SceneElementSequence elements;
        std::shared_ptr<SystemCompositorSession> session;

        session = shell->get_next_session();
        if (session)
        {
            for (auto const& surface : session->get_surfaces()) {
                // An open question is whether this memory allocation overhead (as this method is
                // called for every rendered frame) is perceivable/significant at all to warrant
                // an optimization here.
                auto element = std::make_shared<SurfaceSceneElement>(surface->compositor_snapshot(id));
                elements.emplace_back(element);
            }
        }

        session = shell->get_active_session();
        if (session)
        {
            for (auto const& surface : session->get_surfaces()) {
                // see comment previous comment
                auto element = std::make_shared<SurfaceSceneElement>(surface->compositor_snapshot(id));
                elements.emplace_back(element);
            }
        }

        return elements;
    }

    void add_observer(std::shared_ptr<msc::Observer> const& observer) override { self->add_observer(observer); }
    void remove_observer(std::weak_ptr<msc::Observer> const& observer) override { self->remove_observer(observer); }

    void register_compositor(mc::CompositorID id) override { self->register_compositor(id); }
    void unregister_compositor(mc::CompositorID id) override { self->unregister_compositor(id); }


private:
    std::shared_ptr<mc::Scene> const self;
    std::shared_ptr<SystemCompositorShell> shell;
};


void SystemCompositorSession::mark_ready()
{
    if (!ready)
    {
        ready = true;
        shell->update_session_focus();
    }
}

void SystemCompositorSession::raise(std::shared_ptr<msc::SurfaceCoordinator> const& coordinator)
{
    std::map<mf::SurfaceId, std::shared_ptr<SystemCompositorSurface>>::iterator iter;
    for (iter = surfaces.begin(); iter != surfaces.end(); ++iter)
    {
        // This will iterate by creation order, which is fine.  New surfaces on top
        coordinator->raise(iter->second->get_orig());
    }
}

std::shared_ptr<mf::Surface> SystemCompositorSession::get_surface(mf::SurfaceId surface) const
{
    return surfaces.at(surface);
}

mf::SurfaceId SystemCompositorSession::create_surface(msc::SurfaceCreationParameters const& params)
{
    mf::SurfaceId id = self->create_surface(params);
    std::shared_ptr<mf::Surface> mf_surface = self->get_surface(id);

    auto surface = std::dynamic_pointer_cast<msc::Surface>(mf_surface);
    if (!surface)
    {
        std::cerr << "Unexpected non-scene surface" << std::endl;
        self->destroy_surface(id);
        return mf::SurfaceId(0);
    }

    surfaces[id] = std::make_shared<SystemCompositorSurface>(surface, this);
    return id;
}

class SystemCompositorServerConfiguration : public mir::DefaultServerConfiguration
{
public:
    SystemCompositorServerConfiguration(SystemCompositor *compositor, std::shared_ptr<mo::Configuration> options)
        : mir::DefaultServerConfiguration(options), compositor{compositor}
    {
    }

    int from_dm_fd()
    {
        return the_options()->get("from-dm-fd", -1);
    }

    int to_dm_fd()
    {
        return the_options()->get("to-dm-fd", -1);
    }

    bool show_version()
    {
        return the_options()->is_set("version");
    }

    int inactivity_display_off_timeout()
    {
       return the_options()->get("inactivity-display-off-timeout", 60);
    }

    int inactivity_display_dim_timeout()
    {
       return the_options()->get("inactivity-display-dim-timeout", 45);
    }

    int shutdown_timeout()
    {
       return the_options()->get("shutdown-timeout", 5000);
    }

    int power_key_ignore_timeout()
    {
       return the_options()->get("power-key-ignore-timeout", 1500);
    }

    bool enable_hardware_cursor()
    {
        return the_options()->get("enable-hardware-cursor", false);
    }

    bool disable_inactivity_policy()
    {
        return the_options()->get("disable-inactivity-policy", false);
    }

    std::string blacklist()
    {
        auto x = the_options()->get("blacklist", "");
        boost::trim(x);
        return x;
    }

    std::string spinner()
    {
        // TODO: once our default spinner is ready for use everywhere, replace
        // default value with DEFAULT_SPINNER instead of the empty string.
        auto x = the_options()->get("spinner", "");
        boost::trim(x);
        return x;
    }

    bool public_socket()
    {
        return !the_options()->is_set("no-file") && the_options()->get("public-socket", true);
    }

    std::shared_ptr<mi::CursorListener> the_cursor_listener() override
    {
        struct NullCursorListener : public mi::CursorListener
        {
            void cursor_moved_to(float, float) override
            {
            }
        };

        // This is a workaround for u8 desktop preview in 14.04 for the lack of client cursor API.
        // We need to disable the cursor for XMir but leave it on for the desktop preview.
        // Luckily as it stands they run inside seperate instances of USC. ~racarr
        if (enable_hardware_cursor())
            return mir::DefaultServerConfiguration::the_cursor_listener();
        else
            return std::make_shared<NullCursorListener>();
    }

    std::shared_ptr<mir::ServerStatusListener> the_server_status_listener() override
    {
        struct ServerStatusListener : public mir::ServerStatusListener
        {
            ServerStatusListener (SystemCompositor *compositor) : compositor{compositor} {}

            void paused() override
            {
                compositor->pause();
            }

            void resumed() override
            {
                compositor->resume();
            }

            void started() override
            {
            }

            SystemCompositor *compositor;
        };
        return std::make_shared<ServerStatusListener>(compositor);
    }

    std::string get_socket_file()
    {
        // the_socket_file is private, so we have to re-implement it here
        return the_options()->get("file", "/tmp/mir_socket");
    }

    std::shared_ptr<SystemCompositorShell> the_system_compositor_shell()
    {
        auto shell =  sc_shell([this]
        {
            return std::make_shared<SystemCompositorShell>(
                compositor,
                mir::DefaultServerConfiguration::the_frontend_shell(),
                the_focus_controller(),
                the_surface_coordinator());
        });

        the_system_compositor_scene()->set_shell(shell);
        return shell;
    }

    std::shared_ptr<SystemCompositorScene> the_system_compositor_scene()
    {
        return sc_scene([this]
        {
            return std::make_shared<SystemCompositorScene>(
                mir::DefaultServerConfiguration::the_scene());
        });
    }

    std::shared_ptr<mc::Scene> the_scene()
    {
        return the_system_compositor_scene();
    }

private:
    mir::CachedPtr<SystemCompositorShell> sc_shell;
    mir::CachedPtr<SystemCompositorScene> sc_scene;

    std::shared_ptr<mf::Shell> the_frontend_shell() override
    {
        return the_system_compositor_shell();
    }

    SystemCompositor *compositor;
};

class SystemCompositorConfigurationOptions : public mo::DefaultConfiguration
{
public:
    SystemCompositorConfigurationOptions(int argc, char const* argv[]) :
        DefaultConfiguration(argc, argv)
    {
        add_options()
            ("from-dm-fd", po::value<int>(),  "File descriptor of read end of pipe from display manager [int]")
            ("to-dm-fd", po::value<int>(),  "File descriptor of write end of pipe to display manager [int]")
            ("blacklist", po::value<std::string>(), "Video blacklist regex to use")
            ("version", "Show version of Unity System Compositor")
            ("spinner", po::value<std::string>(), "Path to spinner executable")
            ("public-socket", po::value<bool>(), "Make the socket file publicly writable")
            ("enable-hardware-cursor", po::value<bool>(), "Enable the hardware cursor (disabled by default)")
            ("inactivity-display-off-timeout", po::value<int>(), "The time in seconds before the screen is turned off when there are no active sessions")
            ("inactivity-display-dim-timeout", po::value<int>(), "The time in seconds before the screen is dimmed when there are no active sessions")
            ("shutdown-timeout", po::value<int>(), "The time in milli-seconds the power key must be held to initiate a clean system shutdown")
            ("power-key-ignore-timeout", po::value<int>(), "The time in milli-seconds the power key must be held to ignore - must be less than shutdown-timeout")
            ("disable-inactivity-policy", po::value<bool>(), "Disables handling user inactivity and power key");
    }

    void parse_config_file(
        boost::program_options::options_description& options_description,
        mo::ProgramOption& options) const override
    {
        options.parse_file(options_description, "unity-system-compositor.conf");
    }
};

bool check_blacklist(std::string blacklist, const char *vendor, const char *renderer, const char *version)
{
    if (blacklist.empty())
        return true;

    std::cerr << "Using blacklist \"" << blacklist << "\"" << std::endl;

    regex_t re;
    auto result = regcomp (&re, blacklist.c_str(), REG_EXTENDED);
    if (result == 0)
    {
        char driver_string[1024];
        snprintf (driver_string, 1024, "%s\n%s\n%s",
                  vendor ? vendor : "",
                  renderer ? renderer : "",
                  version ? version : "");

        auto result = regexec (&re, driver_string, 0, NULL, 0);
        regfree (&re);

        if (result == 0)
            return false;
    }
    else
    {
        char error_string[1024];
        regerror (result, &re, error_string, 1024);
        std::cerr << "Failed to compile blacklist regex: " << error_string << std::endl;
    }

    return true;
}

void SystemCompositor::run(int argc, char **argv)
{
    auto const options = std::make_shared<SystemCompositorConfigurationOptions>(argc, const_cast<char const **>(argv));
    config = std::make_shared<SystemCompositorServerConfiguration>(this, options);

    if (config->show_version())
    {
        std::cerr << "unity-system-compositor " << USC_VERSION << std::endl;
        return;
    }

    dm_connection = std::make_shared<DMConnection>(io_service, config->from_dm_fd(), config->to_dm_fd());

    struct ScopeGuard
    {
        explicit ScopeGuard(boost::asio::io_service& io_service) : io_service(io_service) {}
        ~ScopeGuard()
        {
            io_service.stop();
            if (io_thread.joinable())
                io_thread.join();
            if (qt_thread.joinable())
                qt_thread.join();
        }

        boost::asio::io_service& io_service;
        std::thread io_thread;
        std::thread qt_thread;
    } guard(io_service);

    mir::run_mir(*config, [&](mir::DisplayServer&)
        {
            auto vendor = (char *) glGetString(GL_VENDOR);
            auto renderer = (char *) glGetString (GL_RENDERER);
            auto version = (char *) glGetString (GL_VERSION);
            std::cerr << "GL_VENDOR = " << vendor << std::endl;
            std::cerr << "GL_RENDERER = " << renderer << std::endl;
            std::cerr << "GL_VERSION = " << version << std::endl;

            if (!check_blacklist(config->blacklist(), vendor, renderer, version))
                throw mir::AbnormalExit ("Video driver is blacklisted, exiting");

            shell = config->the_system_compositor_shell();
            guard.io_thread = std::thread(&SystemCompositor::main, this);
            guard.qt_thread = std::thread(&SystemCompositor::qt_main, this, argc, argv);
        });
}

void SystemCompositor::pause()
{
    std::cerr << "pause" << std::endl;

    if (auto active_session = config->the_focus_controller()->focussed_application().lock())
        active_session->set_lifecycle_state(mir_lifecycle_state_will_suspend);
}

void SystemCompositor::resume()
{
    std::cerr << "resume" << std::endl;

    if (auto active_session = config->the_focus_controller()->focussed_application().lock())
        active_session->set_lifecycle_state(mir_lifecycle_state_resumed);
}

pid_t SystemCompositor::get_spinner_pid() const
{
    return spinner_process.pid();
}

void SystemCompositor::set_active_session(std::string client_name)
{
    std::cerr << "set_active_session" << std::endl;
    shell->set_active_session(client_name);
}

void SystemCompositor::set_next_session(std::string client_name)
{
    std::cerr << "set_next_session" << std::endl;
    shell->set_next_session(client_name);
}

void SystemCompositor::main()
{
    // Make socket world-writable, since users need to talk to us.  No worries
    // about race condition, since we are adding permissions, not restricting
    // them.
    if (config->public_socket() && chmod(config->get_socket_file().c_str(), 0777) == -1)
        std::cerr << "Unable to chmod socket file " << config->get_socket_file() << ": " << strerror(errno) << std::endl;

    dm_connection->set_handler(this);
    dm_connection->start();
    dm_connection->send_ready();

    io_service.run();
}

void SystemCompositor::ensure_spinner()
{
    if (config->spinner().empty() || spinner_process.state() != QProcess::NotRunning)
        return;

    // Launch spinner process to provide default background when a session isn't ready
    QStringList env = QProcess::systemEnvironment();
    env << "MIR_SOCKET=" + QString(config->get_socket_file().c_str());
    spinner_process.setEnvironment(env);
    spinner_process.start(config->spinner().c_str());
}

void SystemCompositor::kill_spinner()
{
    spinner_process.close();
}

void SystemCompositor::qt_main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    if (!config->disable_inactivity_policy())
    {
        std::chrono::seconds inactivity_display_off_timeout{config->inactivity_display_off_timeout()};
        std::chrono::seconds inactivity_display_dim_timeout{config->inactivity_display_dim_timeout()};
        std::chrono::milliseconds power_key_ignore_timeout{config->power_key_ignore_timeout()};
        std::chrono::milliseconds shutdown_timeout{config->shutdown_timeout()};

        screen_state_handler = std::make_shared<ScreenStateHandler>(config,
            std::chrono::duration_cast<std::chrono::milliseconds>(inactivity_display_off_timeout),
            std::chrono::duration_cast<std::chrono::milliseconds>(inactivity_display_dim_timeout));

        power_key_handler = std::make_shared<PowerKeyHandler>(*(config->the_main_loop()),
            power_key_ignore_timeout,
            shutdown_timeout,
            *screen_state_handler);

        auto composite_filter = config->the_composite_event_filter();
        composite_filter->append(screen_state_handler);
        composite_filter->append(power_key_handler);
    }

    ensure_spinner();
    app.exec();
}
