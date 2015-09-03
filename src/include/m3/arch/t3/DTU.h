/*
 * Copyright (C) 2013, Nils Asmussen <nils@os.inf.tu-dresden.de>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * This file is part of M3 (Microkernel for Minimalist Manycores).
 *
 * M3 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * M3 is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details.
 */

#pragma once

#include <m3/Common.h>
#include <m3/Config.h>
#include <m3/stream/OStream.h>
#include <m3/util/Util.h>
#include <assert.h>

#define DTU_PKG_SIZE        (static_cast<size_t>(8))

namespace m3 {

class DTU {
    static const uintptr_t DRAM_START       = 0x8000;

    explicit DTU() {
    }

public:
    struct Header {
        word_t label;
        word_t length : 16,
               : 15,
               has_replycap : 1;
        word_t replylabel;
        word_t slot : 3,
               reply_credits : 13,
               modid : 8,
               chipid : 8;
    } PACKED;

    static const size_t HEADER_SIZE         = sizeof(Header);
    static const size_t PACKET_SIZE         = 8;

    // TODO not yet supported
    static const int FLAG_NO_RINGBUF        = 0;
    static const int FLAG_NO_HEADER         = 0;

    enum Operation {
        WRITE   = 0x2,      // write from local to remote
        READ    = 0x4,      // read from remote to local
    };

    static DTU &get() {
        return inst;
    }

    void configure(int slot, label_t label, int coreid, int chanid, word_t credits) {
        config_remote(slot, coreid, chanid, credits, 0);
        config_label(slot, label);
    }

    void configure_recv(int chan, uintptr_t buf, uint order, uint msgorder, int flags);

    void send(int slot, const void *msg, size_t size, label_t reply_lbl = label_t(), int reply_slot = 0);
    void send_credits(int slot, uchar dst, int dst_slot, uint credits);
    void reply(int slot, const void *msg, size_t size, size_t msgidx);
    void read(int chan, void *msg, size_t size, size_t off);
    void write(int chan, const void *msg, size_t size, size_t off);
    void cmpxchg(UNUSED int chan, UNUSED const void *msg, UNUSED size_t msgsize, UNUSED size_t off, UNUSED size_t size) {
    }
    void sendcrd(UNUSED int chan, UNUSED int crdchan, UNUSED size_t size) {
    }

    uintptr_t recvbuf(int slot) {
        word_t *ptr = get_cmd_addr(slot, LOCAL_CFG_ADDRESS_FIFO_CMD);
        return read_from(ptr + 0);
    }
    size_t msgsize(int slot) {
        word_t *ptr = get_cmd_addr(slot, LOCAL_CFG_ADDRESS_FIFO_CMD);
        return (read_from(ptr + 1) >> 16) * PACKET_SIZE;
    }
    uint element_count(int slot) {
        word_t *ptr = get_cmd_addr(slot, IDMA_SLOT_FIFO_ELEM_CNT);
        return read_from(ptr);
    }
    uintptr_t element_ptr(int slot) {
        word_t *ptr = get_cmd_addr(slot, IDMA_SLOT_FIFO_GET_ELEM);
        return read_from(ptr);
    }
    void ack_message(int slot) {
        word_t *ptr = get_cmd_addr(slot, IDMA_SLOT_FIFO_RELEASE_ELEM);
        store_to(ptr, 1);
    }

    bool wait() {
        return true;
    }

    void wait_until_ready(int chan) {
        word_t *status = get_cmd_addr(chan,IDMA_OVERALL_SLOT_STATUS);
        while(read_from(status) != 0)
            ;
    }
    bool wait_for_mem_cmd() {
        // we've already waited
        return true;
    }

    void config_local(int slot, uintptr_t addr, size_t fifo_size, size_t token_size) {
        // both have to be packet-size aligned
        assert((addr & (PACKET_SIZE - 1)) == 0);
        assert((fifo_size & (PACKET_SIZE - 1)) == 0);
        assert((token_size & (PACKET_SIZE - 1)) == 0);
        word_t *ptr = get_cmd_addr(slot, LOCAL_CFG_ADDRESS_FIFO_CMD);
        store_to(ptr + 0, addr);
        store_to(ptr + 1, ((token_size / PACKET_SIZE) << 16) | (fifo_size / PACKET_SIZE));
    }

    void config_label(int slot, word_t label) {
        word_t *ptr = get_cmd_addr(slot, OVERALL_SLOT_CFG);
        config_label(ptr, label);
    }
    void config_label(word_t *ptr, word_t label) {
        store_to(ptr, label);
    }

    void config_perm(int slot, word_t perm) {
        word_t *ptr = get_cmd_addr(slot, OVERALL_SLOT_CFG);
        config_perm(ptr, perm);
    }
    void config_perm(word_t *ptr, word_t perm) {
        store_to(ptr + 1, perm);
    }

    void config_remote(int slot, uchar dst, int dst_slot, uint credits, size_t addr_inc) {
        config_remote_mem(slot, dst, get_slot_addr(dst_slot), credits, addr_inc);
    }
    void config_remote_mem(int slot, uchar dst, uintptr_t addr, uint credits, size_t addr_inc) {
        word_t *ptr = get_cmd_addr(slot, EXTERN_CFG_ADDRESS_MODULE_CHIP_CTA_INC_CMD);
        config_remote_mem(ptr, dst, addr, credits, addr_inc);
    }
    void config_remote_mem(word_t *ptr, uchar dst, uintptr_t addr, uint credits, size_t addr_inc) {
        store_to(ptr + 0, addr);

        const int activate = 1;
        const int dstchipid = 0;
        word_t data = 0;
        data = (activate & 0xFF) << 8;
        data = (data | (0xFF & addr_inc)) << 16;
        data = (data | ((dstchipid << 8) | dst));
        store_to(ptr + 1, data);

        store_to(ptr + 2, 0x0); //NOT IN USE
        store_to(ptr + 3, credits);
    }

    void config_transfer_size(int slot, size_t size) {
        word_t *ptr = get_cmd_addr(slot, TRANSFER_CFG_SIZE_STRIDE_REPEAT_CMD);
        // size is specified in packets of 8 bytes
        size /= PACKET_SIZE;
        store_to(ptr + 0, size);
    }

    void config_header(int slot, bool enabled, uint reply_crd, int reply_slot) {
        word_t *ptr = get_cmd_addr(slot, HEADER_CFG_REPLY_LABEL_SLOT_ENABLE_ADDR);
        config_header(ptr, enabled, reply_crd, reply_slot);
    }
    void config_header(word_t *ptr, bool enabled, uint reply_crd, int reply_slot) {
        const unsigned reply_enable = enabled && reply_crd > 0;
        reply_crd /= PACKET_SIZE;
        assert((reply_crd & ~IDMA_HEADER_REPLY_CREDITS_MASK) == 0);

        store_to(ptr + 1, (reply_crd << 16) | (enabled << 9) | (reply_enable << 8) | reply_slot);
    }

    void fire(int slot, Operation op, uint execute) {
        word_t *ptr = get_cmd_addr(slot, FIRE_CMD);
        store_to(ptr, op | execute);
    }

    void debug_msg(uint msg) {
        word_t *ptr = get_cmd_addr(IDMA_DEBUG_SLOT_ID0, DEBUG_CMD);
        store_to(ptr, msg);
    }

    uintptr_t get_slot_addr(int slot) {
        return IDMA_DATA_PORT_ADDR | (slot << IDMA_SLOT_POS);
    }
    uintptr_t get_external_cmd_addr(int slot, unsigned cmd) {
        return IDMA_CONFIG_ADDR + (cmd << PE_IDMA_CMD_POS) + (slot << IDMA_SLOT_POS);
    }

private:
    word_t *get_cmd_addr(int slot,unsigned cmd) {
        return (word_t*)((uintptr_t)PE_IDMA_CONFIG_ADDRESS + (cmd << PE_IDMA_CMD_POS) + (slot << PE_IDMA_SLOT_POS));
    }

    void store_to(word_t *addr,word_t val) {
        // don't flag the address with volatile in order to prevent memory-barriers
        asm volatile ("s32i.n %1,%0,0" : : "a"(addr), "a"(val));
    }

    word_t read_from(word_t *addr) {
        word_t res;
        asm volatile ("l32i.n %0,%1,0" : "=a"(res) : "a"(addr));
        return res;
    }

    static DTU inst;
};

}
