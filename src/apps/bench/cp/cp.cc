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

#include <m3/Syscalls.h>
#include <m3/stream/IStringStream.h>
#include <m3/service/M3FS.h>
#include <m3/vfs/VFS.h>
#include <m3/vfs/FileRef.h>
#include <m3/pipe/PipeFS.h>
#include <m3/util/Profile.h>
#include <m3/Log.h>

using namespace m3;

int main(int argc, char **argv) {
    if(argc < 3) {
        Serial::get() << "Usage: " << argv[0] << " <in> <out>\n";
        return 1;
    }

    cycles_t start = Profile::start(0);
    // TODO temporary fix to support different use-cases without complicating debugging.
    // Because if we require that the mountspace is configured by the parent (which is the goal),
    // we can only use it via VPE::exec, but this would not allow to debug it in a convenient way.
    if(VFS::mount("/", new M3FS("m3fs")) < 0) {
        if(Errors::last != Errors::EXISTS)
            PANIC("Mounting root-fs failed");
    }
    else
        VFS::mount("/pipe/", new PipeFS());

    {
        FileRef input(argv[1], FILE_R);
        if(Errors::occurred())
            PANIC("open of " << argv[1] << " failed (" << Errors::last << ")");

        FileRef output(argv[2], FILE_W | FILE_TRUNC | FILE_CREATE);
        if(Errors::occurred())
            PANIC("open of " << argv[2] << " failed (" << Errors::last << ")");
        cycles_t end1 = Profile::stop(0);
        Serial::get() << "Setup time: " << (end1 - start) << "\n";

        // leave a bit of space for m3 abstractions
        size_t bufsize = 4096;//Heap::contiguous_mem() - 128;
        char *buffer = (char*)Heap::alloc(bufsize);
        Serial::get() << "Using buffer with " << bufsize << " bytes\n";

        ssize_t count;
        cycles_t start = Profile::start(1);
        while((count = input->read(buffer, bufsize)) == (ssize_t)bufsize)
            output->write(buffer, count);
        if(count > 0) {
            memset(buffer + count, 0, DTU_PKG_SIZE - (count % DTU_PKG_SIZE));
            output->write(buffer, (count + DTU_PKG_SIZE - 1) & ~(DTU_PKG_SIZE - 1));
        }
        cycles_t end2 = Profile::stop(1);
        Serial::get() << "Copy: " << (end2 - start) << "\n";
    }
    return 0;
}
