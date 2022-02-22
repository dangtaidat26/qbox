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

#ifndef _GREENSOCS_BASE_COMPONENTS_ROUTER_H
#define _GREENSOCS_BASE_COMPONENTS_ROUTER_H

#include <cinttypes>
#include <vector>

#define THREAD_SAFE true
#if THREAD_SAFE == true
#include <mutex>
#endif

#include <cci_configuration>
#include <systemc>
#include <tlm>

#include <tlm_utils/multi_passthrough_initiator_socket.h>
#include <tlm_utils/multi_passthrough_target_socket.h>

#include <greensocs/base-components/pathid_extension.h>

namespace gs {
namespace router {
    static const char* log_enabled = std::getenv("GS_LOG");
}

template <unsigned int BUSWIDTH = 32>
class Router : public sc_core::sc_module {
    using TargetSocket = tlm::tlm_base_target_socket_b<BUSWIDTH,
        tlm::tlm_fw_transport_if<>,
        tlm::tlm_bw_transport_if<>>;
    using InitiatorSocket = tlm::tlm_base_initiator_socket_b<BUSWIDTH,
        tlm::tlm_fw_transport_if<>,
        tlm::tlm_bw_transport_if<>>;

private:
    template <typename MOD>
    class multi_passthrough_initiator_socket_spying
        : public tlm_utils::multi_passthrough_initiator_socket<MOD, BUSWIDTH> {
        using typename tlm_utils::multi_passthrough_initiator_socket<
            MOD, BUSWIDTH>::base_target_socket_type;

        const std::function<void(std::string)> register_cb;

    public:
        multi_passthrough_initiator_socket_spying(const char* name,
            const std::function<void(std::string)>& f)
            : tlm_utils::multi_passthrough_initiator_socket<
                MOD, BUSWIDTH>::multi_passthrough_initiator_socket(name)
            , register_cb(f)
        {
        }

        void bind(base_target_socket_type& socket)
        {
            tlm_utils::multi_passthrough_initiator_socket<MOD>::bind(socket);
            register_cb(socket.get_base_export().name());
        }
    };

    struct target_info {
        size_t index;
        std::string name;
        sc_dt::uint64 address;
        sc_dt::uint64 size;
        bool mask_addr;
    };

    /* NB use the EXPORT name, so as not to be hassled by the _port_0*/
    std::string nameFromSocket(std::string s)
    {
        return s;
    }
    void register_boundto(std::string s)
    {
        s = nameFromSocket(s);
        struct target_info ti = { 0 };
        ti.name = s;
        ti.index = targets.size();
        targets.push_back(ti);
    }

public:
// make the sockets public for binding
    typedef multi_passthrough_initiator_socket_spying<Router<BUSWIDTH>>
        initiator_socket_type;
    initiator_socket_type initiator_socket;
    tlm_utils::multi_passthrough_target_socket<Router<BUSWIDTH>> target_socket;

private:
    std::vector<target_info> targets;
    std::vector<PathIDExtension*> m_pathIDPool; // at most one per thread!
#if THREAD_SAFE == true
    std::mutex m_pool_mutex;
#endif
    void stamp_txn(int id, tlm::tlm_generic_payload& txn)
    {
        PathIDExtension* ext;
        txn.get_extension(ext);
        if (ext == nullptr) {
#if THREAD_SAFE == true
            std::lock_guard<std::mutex> l(m_pool_mutex);
#endif
            if (m_pathIDPool.size() == 0) {
                ext = new PathIDExtension();
            } else {
                ext = m_pathIDPool.back();
                m_pathIDPool.pop_back();
            }
            txn.set_extension(ext);
        }
        ext->push_back(id);
    }
    void unstamp_txn(int id, tlm::tlm_generic_payload& txn)
    {
        PathIDExtension* ext;
        txn.get_extension(ext);
        assert(ext);
        assert(ext->back() == id);
        ext->pop_back();
        if (ext->size() == 0) {
#if THREAD_SAFE == true
            std::lock_guard<std::mutex> l(m_pool_mutex);
#endif
            txn.clear_extension(ext);
            m_pathIDPool.push_back(ext);
        }
    }

    void b_transport(int id, tlm::tlm_generic_payload& trans,
        sc_core::sc_time& delay)
    {
        sc_dt::uint64 addr = trans.get_address();
        auto ti = decode_address(addr);
        if (!ti) {
            const char* cmd = "unknown";
            switch (trans.get_command()) {
            case tlm::TLM_IGNORE_COMMAND:
                cmd = "ignore";
                break;
            case tlm::TLM_WRITE_COMMAND:
                cmd = "write";
                break;
            case tlm::TLM_READ_COMMAND:
                cmd = "read";
                break;
            }

            std::stringstream info;
            if (gs::router::log_enabled) {
                info << "Warning: " << cmd << " access to unmapped address "
                     << " address "
                     << "0x" << std::hex << trans.get_address()
                     << "in module " << name();
                SC_REPORT_INFO("Router", info.str().c_str());
            }

            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }

        stamp_txn(id, trans);

        if (ti->mask_addr)
            trans.set_address(addr - ti->address);
        initiator_socket[ti->index]->b_transport(trans, delay);
        if (ti->mask_addr)
            trans.set_address(addr);

        unstamp_txn(id, trans);
    }

    unsigned int transport_dbg(int id, tlm::tlm_generic_payload& trans)
    {
        sc_dt::uint64 addr = trans.get_address();
        auto ti = decode_address(addr);
        if (!ti) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return 0;
        }

        if (ti->mask_addr)
            trans.set_address(addr - ti->address);
        unsigned int ret = initiator_socket[ti->index]->transport_dbg(trans);
        if (ti->mask_addr)
            trans.set_address(addr);
        return ret;
    }

    bool get_direct_mem_ptr(int id, tlm::tlm_generic_payload& trans,
        tlm::tlm_dmi& dmi_data)
    {
        sc_dt::uint64 addr = trans.get_address();
        auto ti = decode_address(addr);
        if (!ti) {
            return false;
        }

        if (ti->mask_addr)
            trans.set_address(addr - ti->address);

        bool status = initiator_socket[ti->index]->get_direct_mem_ptr(trans, dmi_data);
        if (ti->mask_addr) {
            dmi_data.set_start_address(
                compose_address(ti->index, dmi_data.get_start_address()));
            dmi_data.set_end_address(
                compose_address(ti->index, dmi_data.get_end_address()));
            trans.set_address(addr);
        }
        return status;
    }

    void invalidate_direct_mem_ptr(int id, sc_dt::uint64 start,
        sc_dt::uint64 end)
    {
        sc_dt::uint64 bw_start_range = start;
        sc_dt::uint64 bw_end_range = end;
        if (targets[id].mask_addr) {
            bw_start_range = compose_address(id, start);
            bw_end_range = compose_address(id, end);
        }

        for (int i = 0; i < target_socket.size(); i++) {
            target_socket[i]->invalidate_direct_mem_ptr(bw_start_range, bw_end_range);
        }
    }

    struct target_info* decode_address(sc_dt::uint64 addr)
    {
        for (unsigned int i = 0; i < targets.size(); i++) {
            struct target_info& ti = targets.at(i);
            if (addr >= ti.address && addr < (ti.address + ti.size)) {
                return &ti;
            }
        }
        return nullptr;
    }

    inline sc_dt::uint64 compose_address(unsigned int index,
        sc_dt::uint64 address)
    {
        assert(address < targets[index].size);
        return targets[index].address + address;
    }

protected:
    virtual void before_end_of_elaboration()
    {
        target_socket.register_b_transport(this, &Router::b_transport);
        target_socket.register_transport_dbg(this, &Router::transport_dbg);
        target_socket.register_get_direct_mem_ptr(this,
            &Router::get_direct_mem_ptr);
        initiator_socket.register_invalidate_direct_mem_ptr(this, &Router::invalidate_direct_mem_ptr);

        for (auto& ti : targets) {
            if (!m_broker.has_preset_value(ti.name + ".address")) {
                SC_REPORT_ERROR("Router",
                    ("Can't find " + ti.name + ".address").c_str());
            }
            uint64_t address = m_broker.get_preset_cci_value(ti.name + ".address").get_uint64();
            m_broker.lock_preset_value(ti.name + ".address");
            m_broker.ignore_unconsumed_preset_values(
                [ti](const std::pair<std::string, cci::cci_value>& iv) -> bool { return iv.first==(ti.name + ".address"); });
            if (!m_broker.has_preset_value(ti.name + ".size")) {
                SC_REPORT_ERROR("Router",
                    ("Can't find " + ti.name + ".size").c_str());
            }
            uint64_t size = m_broker.get_preset_cci_value(ti.name + ".size").get_uint64();
            m_broker.lock_preset_value(ti.name + ".size");
            m_broker.ignore_unconsumed_preset_values(
                [ti](const std::pair<std::string, cci::cci_value>& iv) -> bool { return iv.first==(ti.name + ".size"); });

            bool mask = true;
            if (m_broker.has_preset_value(ti.name + ".relative_addresses")) {
                mask = m_broker.get_preset_cci_value(ti.name + ".relative_addresses").get_bool();
                m_broker.lock_preset_value(ti.name + ".relative_addresses");
                m_broker.ignore_unconsumed_preset_values(
                    [ti](const std::pair<std::string, cci::cci_value>& iv) -> bool { return iv.first==(ti.name + ".relative_addresses"); });
            }

            std::stringstream info;
            info << "Address map " << ti.name + " at"
                 << " address "
                 << "0x" << std::hex << address
                 << " size "
                 << "0x" << std::hex << size
                 << (mask ? " (with relative address) " : "");
            SC_REPORT_INFO("Router", info.str().c_str());
            ti.address = address;
            ti.size = size;
            ti.mask_addr = mask;
        }
    }

public:
    cci::cci_broker_handle m_broker;
    cci::cci_param<bool> thread_safe;

    explicit Router(const sc_core::sc_module_name& nm)
        : sc_core::sc_module(nm)
        , initiator_socket("initiator_socket", [&](std::string s) -> void { register_boundto(s); })
        , target_socket("target_socket")
        , m_broker(cci::cci_get_broker())
        , thread_safe("thread_safe", THREAD_SAFE, "Is this model thread safe")
    {
        // nothing to do
    }

    Router() = delete;

    Router(const Router&) = delete;

    ~Router()
    {
        while (m_pathIDPool.size()) {
            delete (m_pathIDPool.back());
            m_pathIDPool.pop_back();
        }
    }

    void add_target(TargetSocket& t, const uint64_t address,
        uint64_t size, bool masked = true)
    {
        std::string s = nameFromSocket(t.get_base_export().name());
        if (!m_broker.has_preset_value(s + ".address")) {
            m_broker.set_preset_cci_value(s + ".address",
                cci::cci_value(address));
        }
        if (!m_broker.has_preset_value(s + ".size")) {
            m_broker.set_preset_cci_value(s + ".size",
                cci::cci_value(size));
        }
        if (!m_broker.has_preset_value(s + ".relative_addresses")) {
            m_broker.set_preset_cci_value(s + ".relative_addresses",
                cci::cci_value(masked));
        }
        initiator_socket.bind(t);
    }

    virtual void add_initiator(InitiatorSocket& i)
    {
        // hand bind the port/exports as we are using base classes
        (i.get_base_port())(target_socket.get_base_interface());
        (target_socket.get_base_port())(i.get_base_interface());
    }
};
}
#endif
