/*
 *  This file is part of GreenSocs base-components
 *  Copyright (c) 2020-2021 GreenSocs
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef _GREENSOCS_PATHID_EXTENSION_H
#define _GREENSOCS_PATHID_EXTENSION_H

#include <systemc>
#include <tlm>


namespace gs {

/**
 * @class Path recording TLM extension
 *
 * @brief Path recording TLM extension
 *
 * @details Embeds an  ID field in the txn, which is populated as the network
 * is traversed - see README.
 */

class PathIDExtension : public tlm::tlm_extension<PathIDExtension>, public std::vector<int> {
public:
    PathIDExtension() = default;
    PathIDExtension(const PathIDExtension&) = default;

public:
    virtual tlm_extension_base* clone() const override
    {
        return new PathIDExtension(*this);
    }

    virtual void copy_from(const tlm_extension_base& ext) override
    {
        const PathIDExtension& other = static_cast<const PathIDExtension&>(ext);
        *this = other;
    }
};
}
#endif
