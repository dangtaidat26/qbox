/*
* Copyright (c) 2022 GreenSocs
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 2
* of the License, or (at your option) any later version, or under the
* Apache License, Version 2.0 (the "License”) at your discretion.
*
* SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
* You may obtain a copy of the Apache License at
* http://www.apache.org/licenses/LICENSE-2.0
*/

#ifndef _GREENSOCS_BASE_COMPONENTS_MEMORY_H
#define _GREENSOCS_BASE_COMPONENTS_MEMORY_H

#include <fstream>
#include <memory>

#include <cci_configuration>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <scp/report.h>

#include "loader.h"

#ifndef _WIN32
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#endif

namespace gs {

/**
 * @class Memory
 *
 * @brief A memory component that can add memory to a virtual platform project
 *
 * @details This component models a memory. It has a simple target socket so any other component with an initiator socket can connect to this component. It behaves as follows:
 *    - The memory does not manage time in any way
 *    - It is only an LT model, and does not handle AT transactions
 *    - It does not manage exclusive accesses
 *    - You can manage the size of the memory during the initialization of the component
 *    - Memory does not allocate individual "pages" but a single large block
 *    - It supports DMI requests with the method `get_direct_mem_ptr`
 *    - DMI invalidates are not issued.
 */

template <unsigned int BUSWIDTH = 32>
class Memory : public sc_core::sc_module {
    uint64_t m_size = 0;
    uint64_t m_address;
    bool m_address_valid = false;
    bool m_relative_addresses;
    
    uint8_t* ptr = nullptr;

// Teplated on the power of 2 to use to divide the blocks up
    template <unsigned int N = 2>
    class SubBlock {
      uint64_t m_len;      // size of the bloc
      uint64_t m_address;  // this is an absolute address and this is the
                           // beginning of the bloc/Memory
      uint64_t m_max_size;
      uint64_t m_min_size;

      std::string m_filename;

      uint8_t* m_ptr = nullptr;
      std::array<std::unique_ptr<SubBlock>, (1<<N)> m_sub_blocks;
      bool m_use_sub_blocks = false;

      bool m_mapped = false;

      /**
       * @brief This function maps a host file system file into the memory,
       * such that the results of the memory will be maintained between runs.
       * This can be useful for emulating a flash ram for instance NB the only
       * way of having this called is via the configuration paramter mapfile
       * @param filename Name of the file
       */
      bool map(std::string filename) {
#ifndef _WIN32
          int fd = open(filename.c_str(), O_RDWR);
          if (fd < 0) {
              SC_REPORT_FATAL("Memory", "Unable to find backing file\n");
          }
          m_ptr = (uint8_t*)mmap(NULL, m_len, PROT_READ | PROT_WRITE,
                                 MAP_SHARED, fd, m_address);
          close(fd);
          if (m_ptr == MAP_FAILED) {
              SC_REPORT_WARNING("Memory", "Unable to map backing file\n");
              m_ptr = nullptr;
              return false;
          }
          m_mapped = true;
          return true;
#else
          SC_REPORT_FATAL("Memory",
                          "Backing files only supported on UNIX platforms\n");
#endif
      }

  public:
      SubBlock(uint64_t address, uint64_t len, uint64_t max_size,
               uint64_t min_size, std::string filename="")
          : m_address(address),
            m_len(len),
            m_max_size(max_size),
            m_min_size(min_size),
            m_filename(filename) {}

      SubBlock& access(uint64_t address) {
        // address is the address of where we want to write/read
        // len is the size of the data

        if (m_ptr && !m_use_sub_blocks) {
          assert(address >= m_address);
          return *this;
        }
        if (m_len > m_max_size) {
          m_use_sub_blocks = true;
        }

        if (!m_use_sub_blocks) {
            if (!m_filename.empty()) {
                if (map(m_filename)) {
                    return *this;
                }
            } else {
                m_ptr = static_cast<uint8_t*>(aligned_alloc(0x1000, m_len));
                if (m_ptr) {
                    return *this;
                }
                SC_REPORT_INFO(
                    "Memory", "Aligned allocation failed, using normal allocation");
                m_ptr = (uint8_t*)malloc(m_len);
                if (m_ptr) {
                    return *this;
                }
            }
            m_use_sub_blocks = true;
        }

        if (m_len < m_min_size) {
          SC_REPORT_FATAL("Memory",
                          "Unable to allocate memory!");  // out of memory!
        }

        // return a index of sub_bloc
        assert ((m_len & ~(-1llu << N)) == 0);
        uint64_t m_sub_size = m_len >> N;
        int i = (address - m_address) / (m_sub_size);

        if (!m_sub_blocks[i]) {
          m_sub_blocks[i] = std::make_unique<SubBlock>(
              (i * m_sub_size) + m_address, m_sub_size, m_max_size, m_min_size, m_filename);
        }
        return m_sub_blocks[i]->access(address);
      }

      uint64_t read_sub_blocks(uint8_t* data, uint64_t offset, uint64_t len) {
        uint64_t bloc_offset = offset - m_address;
        uint64_t bloc_len = m_len - bloc_offset;
        uint64_t remain_len = (len < bloc_len) ? len : bloc_len;

        memcpy(data, &m_ptr[bloc_offset], remain_len);

        return remain_len;
      }

      uint64_t write_sub_blocks(const uint8_t* data, uint64_t offset,
                                uint64_t len) {
        uint64_t bloc_offset = offset - m_address;
        uint64_t bloc_len = m_len - bloc_offset;
        uint64_t remain_len = (len < bloc_len) ? len : bloc_len;

        memcpy(&m_ptr[bloc_offset], data, remain_len);

        return remain_len;
      }

      uint8_t* get_ptr() { return m_ptr; }

      uint64_t get_len() { return m_len; }

      uint64_t get_address() { return m_address; }

      ~SubBlock() {
        if (m_mapped) { 
            munmap(m_ptr, m_len );
        } else {
            if (m_ptr) free(m_ptr);
        }
      }
    };

private:
    std::unique_ptr<Memory<BUSWIDTH>::SubBlock<>> m_sub_block;

protected:

    virtual bool get_direct_mem_ptr(tlm::tlm_generic_payload& txn, tlm::tlm_dmi& dmi_data)
    {
        if (!p_dmi)
            return false;
        sc_dt::uint64 addr = txn.get_address();

        if (!m_relative_addresses) {
            if (addr < m_address) {
                txn.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                return false;
            }
            addr -= m_address;
        }
        if (addr >= m_size) {
            txn.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return false;
        }
        if (p_verbose) {
            std::stringstream info;
            info << name() << " : DMI access to address "
                 << "0x" << std::hex << addr;
            SC_REPORT_INFO("Memory", info.str().c_str());
        }

        if (p_rom)
            dmi_data.allow_read();
        else
            dmi_data.allow_read_write();
        
        SubBlock<>& blk = m_sub_block->access(addr);

        uint8_t* ptr = blk.get_ptr();
        uint64_t size= blk.get_len();
        uint64_t bloc_address = blk.get_address();

        dmi_data.set_dmi_ptr(reinterpret_cast<unsigned char*>(ptr));
        if (!m_relative_addresses) {
            dmi_data.set_start_address(bloc_address + m_address);
            dmi_data.set_end_address((bloc_address + m_address + size) - 1);
        } else {
            dmi_data.set_start_address(bloc_address);
            dmi_data.set_end_address((bloc_address + size) - 1);
        }
        dmi_data.set_read_latency(p_latency);
        dmi_data.set_write_latency(p_latency);

        return true;
    }

    virtual void b_transport(tlm::tlm_generic_payload& txn, sc_core::sc_time& delay)
    {
        unsigned int len = txn.get_data_length();
        unsigned char* ptr = txn.get_data_ptr();
        sc_dt::uint64 addr = txn.get_address();
        unsigned char* byt = txn.get_byte_enable_ptr();
        unsigned int bel = txn.get_byte_enable_length();

        if (txn.get_streaming_width() < len) {
            SC_REPORT_WARNING("Memory", "not supported.\n");
        }

        if (p_verbose) {
            const char* cmd = "unknown";
            switch (txn.get_command()) {
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

            info << name() << " : " << cmd << " access to address "
                 << "0x" << std::hex << addr;
            SC_REPORT_INFO("Memory", info.str().c_str());
        }
        if (!m_relative_addresses) {
            if (addr < m_address) {
                txn.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                return;
            }
            addr -= m_address;
        }
        if ((addr + len) > m_size) {
            txn.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }

        switch (txn.get_command()) {
        case tlm::TLM_READ_COMMAND:
            if (byt) {
                for (unsigned int i = 0; i < len; i++)
                    if (byt[i % bel] == TLM_BYTE_ENABLED)
                        read(&(ptr[i]), (addr + i), 1);
            } else {
                read(ptr, addr, len);
            }
            break;
        case tlm::TLM_WRITE_COMMAND:
            if (p_rom) {
                txn.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            if (byt) {
                for (unsigned int i = 0; i < len; i++)
                    if (byt[i % bel] == TLM_BYTE_ENABLED)
                        write(&(ptr[i]), (addr + i), 1);
            } else {
                write(ptr, addr, len);
            }
            break;
        default:
            SC_REPORT_FATAL("Memory", "TLM command not supported\n");
            break;
        }

        txn.set_response_status(tlm::TLM_OK_RESPONSE);

        if (p_dmi)
            txn.set_dmi_allowed(true);
    }

    virtual unsigned int transport_dbg(tlm::tlm_generic_payload& txn)
    {
        unsigned int len = txn.get_data_length();
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        b_transport(txn, delay);
        if (txn.get_response_status() == tlm::TLM_OK_RESPONSE)
            return len;
        else
            return 0;
    }

    void cci_ignore(std::string name)
    {
        auto m_broker = cci::cci_get_broker();
        std::string fullname = std::string(sc_module::name()) + "." + name;
        m_broker.ignore_unconsumed_preset_values(
            [fullname](const std::pair<std::string, cci::cci_value>& iv) -> bool { return iv.first == fullname; });
    }

    void read(uint8_t* data, uint64_t offset, uint64_t len)
    {
        // force end of elaboration to ensure we fix the sizes
        // this may happen if another model descides to load data into memory as
        // part of it's initialization during before_end_of_elaboration.

        if (!m_sub_block)
            before_end_of_elaboration();

        uint64_t remain_len = 0;
        uint64_t data_ptr_offset = 0;

        sc_assert(offset + len < m_size);

        while (len > 0){
            SubBlock<>& blk = m_sub_block->access(offset + data_ptr_offset);

            remain_len = blk.read_sub_blocks(&data[data_ptr_offset], offset + data_ptr_offset, len);

            data_ptr_offset += remain_len;
            len -= remain_len;
        }

    }
    void write(const uint8_t* data, uint64_t offset, uint64_t len)
    {
        if (!m_sub_block)
            before_end_of_elaboration();

        uint64_t remain_len = 0;
        uint64_t data_ptr_offset = 0;

        sc_assert(offset + len < m_size);

        while (len > 0){
            SubBlock<>& blk = m_sub_block->access(offset + data_ptr_offset);

            remain_len = blk.write_sub_blocks(&data[data_ptr_offset], offset + data_ptr_offset, len);

            data_ptr_offset += remain_len;
            len -= remain_len;
        }

    }

public:
    gs::Loader<> load;

    tlm_utils::simple_target_socket<Memory, BUSWIDTH> socket;
    cci::cci_param<bool> p_rom;
    cci::cci_param<bool> p_dmi;
    cci::cci_param<bool> p_verbose;
    cci::cci_param<sc_core::sc_time> p_latency;
    cci::cci_param<std::string> p_mapfile;
    cci::cci_param<uint64_t> p_max_bloc_size;
    cci::cci_param<uint64_t> p_min_bloc_size;

    // NB
    // A size given by a config will always take precedence
    // A size set on the constructor will take precedence over
    // the size given on an e.g. 'add_target' in the router

    Memory(sc_core::sc_module_name name, uint64_t _size = 0)
        : socket("target_socket")
        , p_rom("read_only", false, "Read Only Memory (default false)")
        , p_dmi("dmi_allow", true, "DMI allowed (default true)")
        , p_verbose("verbose", false, "Switch on verbose logging")
        , p_latency("latency", sc_core::sc_time(10, sc_core::SC_NS), "Latency reported for DMI access")
        , p_mapfile("map_file", "", "(optional) file to map this memory")
        , p_max_bloc_size("max_bloc_size", 0x100000000, "Maximum size of the sub bloc")
        , p_min_bloc_size("min_bloc_size", sysconf(_SC_PAGE_SIZE), "Minimum size of the sub bloc")
        , load(
              "load",
              [&](const uint8_t* data, uint64_t offset, uint64_t len) -> void { write(data, offset, len); })
        , m_sub_block(nullptr)
    {
        if (_size) {
            auto m_broker = cci::cci_get_broker();
            std::string ts_name = std::string(sc_module::name()) + ".target_socket";
            if (!m_broker.has_preset_value(ts_name + ".size")) {
                m_broker.set_preset_cci_value(ts_name + ".size",
                    cci::cci_value(_size));
            }
        }

        cci_ignore("address");
        cci_ignore("size");
        cci_ignore("relative_addresses");

        socket.register_b_transport(this, &Memory::b_transport);
        socket.register_transport_dbg(this, &Memory::transport_dbg);
        socket.register_get_direct_mem_ptr(this, &Memory::get_direct_mem_ptr);
    }

    void before_end_of_elaboration()
    {
        if (m_sub_block){
            return;
        }

        auto m_broker = cci::cci_get_broker();
        std::string ts_name = std::string(sc_module::name()) + ".target_socket";

        m_address = base();
        m_size = size();

        m_sub_block = std::make_unique <Memory<BUSWIDTH>::SubBlock<>>(0, m_size, p_max_bloc_size, p_min_bloc_size, p_mapfile);

        SCP_INFO(SCMOD) <<"m_address: "<< m_address;
        SCP_INFO(SCMOD) <<"m_size: "<< m_size;

        m_relative_addresses = true;
        if (m_broker.has_preset_value(ts_name + ".relative_addresses")) {
            m_relative_addresses = m_broker.get_preset_cci_value(ts_name + ".relative_addresses").get_bool();
            m_broker.lock_preset_value(ts_name + ".relative_addresses");
        }
    }

    Memory() = delete;
    Memory(const Memory&) = delete;

    ~Memory()
    {
    }

    /**
     * @brief this function returns the size of the memory
     *
     * @return the size of the memory of type uint64_t
     */
    uint64_t size()
    {
        if (!m_size) {
            auto m_broker = cci::cci_get_broker();
            std::string ts_name = std::string(sc_module::name()) + ".target_socket";
            if (!m_broker.has_preset_value(ts_name + ".size")) {
                SC_REPORT_FATAL("Memory",
                    ("Can't find " + ts_name + ".size").c_str());
            }
            m_size = m_broker.get_preset_cci_value(ts_name + ".size").get_uint64();
            m_broker.lock_preset_value(ts_name + ".size");
        }
        return m_size;
    }

    /**
     * @brief this function returns the base address of the memory
     *
     * @return the address of the memory of type uint64_t
     */
    uint64_t base()
    {
        if (!m_address_valid) {
            auto m_broker = cci::cci_get_broker();
            std::string ts_name = std::string(sc_module::name()) + ".target_socket";
            if (!m_broker.has_preset_value(ts_name + ".address")) {
                m_address = 0; // fine for relative addressing
                SC_REPORT_WARNING("Memory",
                    ("Can't find " + ts_name + ".address").c_str());
            } else {
                m_address = m_broker.get_preset_cci_value(ts_name + ".address").get_uint64();
            }
            m_broker.lock_preset_value(ts_name + ".address");
            m_address_valid = true;
        }
        return m_address;
    }
};
}
#endif
