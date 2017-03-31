/*
 * Copyright © 2015 Canonical Ltd.
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
 */

#ifndef USC_TEST_MOCK_DISPLAY_H
#define USC_TEST_MOCK_DISPLAY_H

#include "usc/test/stub_display_configuration.h"

#include <mir/graphics/display.h>
#include <mir/graphics/virtual_output.h>
#include <mir/version.h>
#include <gmock/gmock.h>

namespace usc
{
namespace test
{
struct MockDisplay : mir::graphics::Display, mir::graphics::NativeDisplay
{
    void for_each_display_sync_group(std::function<void(mir::graphics::DisplaySyncGroup&)> const& f) override
    {
    }

    std::unique_ptr<mir::graphics::DisplayConfiguration> configuration() const override
    { return std::make_unique<usc::test::StubDisplayConfiguration>(); }

    MOCK_METHOD1(configure, void(mir::graphics::DisplayConfiguration const& conf));

    void register_configuration_change_handler(
    mir::graphics::EventHandlerRegister& ,
    mir::graphics::DisplayConfigurationChangeHandler const& ) override {};

    void register_pause_resume_handlers(
        mir::graphics::EventHandlerRegister&,
        mir::graphics::DisplayPauseHandler const&,
        mir::graphics::DisplayResumeHandler const&) override
    {
    }

    void pause() override {};

    void resume() override {};

#if MIR_SERVER_VERSION < MIR_VERSION_NUMBER(0, 27, 0)
    std::shared_ptr<mir::graphics::Cursor> create_hardware_cursor(
        std::shared_ptr<mir::graphics::CursorImage> const&) override
    {
        return {};
    };
#else
    std::shared_ptr<mir::graphics::Cursor> create_hardware_cursor() override { return {}; }
#endif

    std::unique_ptr<mir::graphics::VirtualOutput> create_virtual_output(int, int) override
    { return std::unique_ptr<mir::graphics::VirtualOutput>{}; }

    mir::graphics::NativeDisplay* native_display() override
    {
        return this;
    }

    bool apply_if_configuration_preserves_display_buffers(mir::graphics::DisplayConfiguration const& conf) override
    {
        return false;
    }

    mir::graphics::Frame last_frame_on(unsigned output_id) const override
    {
        return {};
    }

};
}
}

#endif
