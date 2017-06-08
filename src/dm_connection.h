/*
 * Copyright © 2014 Canonical Ltd.
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
 * Authored by: Alexandros Frantzis <alexandros.frantzis@canonical.com>
 */

#ifndef USC_DM_CONNECTION_H_
#define USC_DM_CONNECTION_H_

#include <string>

namespace usc
{

class DMMessageHandler
{
public:
    virtual void set_active_session(std::string const& client_name) = 0;
    virtual void set_next_session(std::string const& client_name) = 0;
};

class DMConnection
{
public:
    virtual ~DMConnection() = default;

    virtual void start() = 0;

protected:
    DMConnection() = default;
    DMConnection(DMConnection const&) = delete;
    DMConnection& operator=(DMConnection const&) = delete;
};

}

#endif /* USC_DM_CONNECTION_H_ */
