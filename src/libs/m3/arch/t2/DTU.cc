/*
 * Copyright (C) 2015, Nils Asmussen <nils@os.inf.tu-dresden.de>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * This file is part of M3 (Microkernel-based SysteM for Heterogeneous Manycores).
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

#include <m3/util/Sync.h>
#include <m3/cap/MemGate.h>
#include <m3/tracing/Tracing.h>
#include <m3/DTU.h>
#include <m3/Log.h>

namespace m3 {

DTU DTU::inst INIT_PRIORITY(106);

size_t DTU::regs[2][6] = {
    // CM
    {
        /* REG_TARGET */    2,
        /* REG_REM_ADDR */  4,
        /* REG_LOC_ADDR */  6,
        /* REG_TYPE */      8,
        /* REG_SIZE */      10,
        /* REG_STATUS */    10,
    },

    // PEs
    {
        /* REG_TARGET */    0,
        /* REG_REM_ADDR */  4,
        /* REG_LOC_ADDR */  6,
        /* REG_TYPE */      2,
        /* REG_SIZE */      8,
        /* REG_STATUS */    10,
    }
};

void DTU::reset() {
    memset((void*)RECV_BUF_LOCAL,0,EP_COUNT * RECV_BUF_MSGSIZE * MAX_CORES);
    memset(_pos, 0, sizeof(_pos));
    memset(_last, 0, sizeof(_last));
}

bool DTU::fetch_msg(int ep) {
    // simple way to achieve fairness here. otherwise we might choose the same client all the time
    int end = MAX_CORES;
retry:
    for(int i = _pos[ep]; i < end; ++i) {
        volatile Message *msg = reinterpret_cast<volatile Message*>(
            RECV_BUF_LOCAL + DTU::get().recvbuf_offset(i, ep));
        if(msg->length != 0) {
            LOG(IPC, "Fetched msg @ " << (void*)msg << " over ep " << ep);
            EVENT_TRACE_MSG_RECV(msg->core, msg->length,
                ((uint)msg - RECV_BUF_GLOBAL) >> TRACE_ADDR2TAG_SHIFT);
            assert(_last[ep] == nullptr);
            _last[ep] = const_cast<Message*>(msg);
            _pos[ep] = i + 1;
            return true;
        }
    }
    if(_pos[ep] != 0) {
        end = _pos[ep];
        _pos[ep] = 0;
        goto retry;
    }
    return false;
}

void DTU::send(int ep, const void *msg, size_t size, label_t replylbl, int reply_ep) {
    EPConf *cfg = conf(ep);
    uintptr_t destaddr = RECV_BUF_GLOBAL + recvbuf_offset(coreid(), cfg->dstep);
    LOG(DTU, "-> " << fmt(size, 4) << "b to " << cfg->dstcore << ":" << cfg->dstep
        << " from " << msg << " with lbl=" << fmt(cfg->label, "#0x", sizeof(label_t) * 2));

    EVENT_TRACE_MSG_SEND(cfg->dstcore, size, ((uint)destaddr - RECV_BUF_GLOBAL) >> TRACE_ADDR2TAG_SHIFT);

    // first send data to ensure that everything has already arrived if the receiver notices
    // an arrival
    set_target(SLOT_NO, cfg->dstcore, destaddr + sizeof(Header));
    fire(SLOT_NO, WRITE, msg, size);
    wait_until_ready(SLOT_NO);

    // now send header
    alignas(DTU_PKG_SIZE) DTU::Header head;
    head.length = size;
    head.label = cfg->label;
    head.replylabel = replylbl;
    head.has_replycap = 1;
    head.core = coreid();
    head.epid = reply_ep;
    set_target(SLOT_NO, cfg->dstcore, destaddr);
    Sync::memory_barrier();
    fire(SLOT_NO, WRITE, &head, sizeof(head));
}

void DTU::reply(int ep, const void *msg, size_t size, size_t msgidx) {
    DTU::Message *orgmsg = message_at(ep, msgidx);
    uintptr_t destaddr = RECV_BUF_GLOBAL + recvbuf_offset(coreid(), orgmsg->epid);
    LOG(DTU, ">> " << fmt(size, 4) << "b to " << orgmsg->core << ":" << orgmsg->epid
        << " from " << msg << " with lbl=" << fmt(orgmsg->replylabel, "#0x", sizeof(label_t) * 2));

    EVENT_TRACE_MSG_SEND(orgmsg->core, size, ((uint)destaddr - RECV_BUF_GLOBAL) >> TRACE_ADDR2TAG_SHIFT);

    // first send data to ensure that everything has already arrived if the receiver notices
    // an arrival
    set_target(SLOT_NO, orgmsg->core, destaddr + sizeof(Header));
    fire(SLOT_NO, WRITE, msg, size);
    wait_until_ready(SLOT_NO);

    // now send header
    alignas(DTU_PKG_SIZE) DTU::Header head;
    head.length = size;
    head.label = orgmsg->replylabel;
    head.has_replycap = 0;
    head.core = coreid();
    set_target(SLOT_NO, orgmsg->core, destaddr);
    Sync::memory_barrier();
    fire(SLOT_NO, WRITE, &head, sizeof(head));
}

void DTU::check_rw_access(uintptr_t base, size_t len, size_t off, size_t size, int perms, int type) {
    uintptr_t srcaddr = base + off;
    if(!(perms & type) || srcaddr < base || srcaddr + size < srcaddr ||  srcaddr + size > base + len) {
        PANIC("No permission to " << (type == MemGate::R ? "read from" : "write to")
                << " " << fmt(srcaddr, "p") << ".." << fmt(srcaddr + size, "p") << "\n"
                << "Allowed is: " << fmt(base, "p") << ".." << fmt(base + len, "p")
                << " with " << fmt(perms, "#x"));
    }
}

void DTU::read(int ep, void *msg, size_t size, size_t off) {
    EPConf *cfg = conf(ep);
    uintptr_t base = cfg->label & ~MemGate::RWX;
    size_t len = cfg->credits;
    uintptr_t srcaddr = base + off;
    LOG(DTU, "Reading " << size << "b from " << cfg->dstcore << " @ " << fmt(srcaddr, "p"));

    EVENT_TRACE_MEM_READ(cfg->dstcore, size);

    // mark the end
    reinterpret_cast<unsigned char*>(msg)[size - 1] = 0xFF;

    check_rw_access(base, len, off, size, cfg->label & MemGate::RWX, MemGate::R);
    set_target(SLOT_NO, cfg->dstcore, srcaddr);
    fire(SLOT_NO, READ, msg, size);

    // wait until the size-register has been decremented to 0
    size_t rem;
    while((rem = get_remaining(ep)) > 0)
        ;

    // TODO how long should we wait?
    for(volatile size_t i = 0; i < 1000; ++i) {
        // stop if the end has been overwritten
        if(reinterpret_cast<unsigned char*>(msg)[size - 1] != 0xFF)
            break;
    }
}

void DTU::write(int ep, const void *msg, size_t size, size_t off) {
    EPConf *cfg = conf(ep);
    uintptr_t base = cfg->label & ~MemGate::RWX;
    size_t len = cfg->credits;
    uintptr_t destaddr = base + off;
    LOG(DTU, "Writing " << size << "b to " << cfg->dstcore << " @ " << fmt(destaddr, "p"));

    EVENT_TRACE_MEM_WRITE(cfg->dstcore, size);

    check_rw_access(base, len, off, size, cfg->label & MemGate::RWX, MemGate::W);
    set_target(SLOT_NO, cfg->dstcore, destaddr);
    fire(SLOT_NO, WRITE, msg, size);

    // wait until the size-register has been decremented to 0
    // TODO why is this required?
    size_t rem;
    while((rem = get_remaining(ep)) > 0)
        ;
}

}
