/*
 *  This file is part of libgsutils
 *  Copyright (c) 2021 Luc Michel
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

#ifndef _LIBGSUTILS_PORTS_INITIATOR_SIGNAL_SOCKET_H
#define _LIBGSUTILS_PORTS_INITIATOR_SIGNAL_SOCKET_H

#include <systemc>

template <class T>
using InitiatorSignalSocket = sc_core::sc_port<sc_core::sc_signal_inout_if<T>,
                                               1, sc_core::SC_ZERO_OR_MORE_BOUND>;

#endif

