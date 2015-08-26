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


#ifndef USC_MIR_INPUT_CONFIGRATION_H_
#define USC_MIR_INPUT_CONFIGRATION_H_

#include "input_configuration.h"

namespace usc
{

struct MirInputConfiguration : InputConfiguration
{
public:
    void set_mouse_primary_button(int32_t button) override;
    void set_mouse_cursor_speed(double speed) override;
    void set_mouse_scroll_speed(double speed) override;
    void set_touchpad_primary_button(int32_t button) override;
    void set_touchpad_cursor_speed(double speed) override;
    void set_touchpad_scroll_speed(double speed) override;
    void set_two_finger_scroll(bool enable) override;
    void set_tap_to_click(bool enable) override;
    void set_disable_touchpad_while_typing(bool enable) override;
    void set_disable_touchpad_with_mouse(bool enable) override;
private:
    int32_t mouse_primary_button{0};
    double mouse_cursor_speed{0.5};
    double mouse_scroll_speed{0.5};
    int32_t touchpad_primary_button{0};
    double touchpad_cursor_speed{0.5};
    double touchpad_scroll_speed{0.5};
    bool two_finger_scroll{false};
    bool tap_to_click{false};
    bool disable_while_typing{false};
    bool disable_with_mouse{true};
};

}

#endif
