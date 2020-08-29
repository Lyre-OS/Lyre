module logging.terminal.framebuffer;

import memory.virtual;
import stivale;
import lib.alloc;

alias Colour = uint;

struct Framebuffer {
    Colour* address;
    size_t  width;
    size_t  height;
    size_t  pitch;

    this(StivaleFramebuffer fb) {
        this.address = cast(Colour*)(fb.address + MEM_PHYS_OFFSET);
        this.width   = fb.width;
        this.height  = fb.height;
        this.pitch   = fb.pitch / Colour.sizeof;
    }

    invariant {
        assert(this.pitch % Colour.sizeof == 0, "Pitch is not a multiple of 4");
    }

    void putPixel(size_t x, size_t y, Colour c) {
        auto position = x + this.pitch * y;
        this.address[position] = c;
    }
}
