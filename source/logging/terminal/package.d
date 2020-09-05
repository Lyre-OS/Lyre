module logging.terminal;

import scheduler.thread;
import stivale;
import lib.alloc;
import lib.bus;
import logging.terminal.tty;

struct TerminalMessage {
    string contents;
}

private __gshared bool isInit;
private __gshared TTY* tty;

void terminalInit(StivaleFramebuffer fb) {
    tty = newObj!TTY(fb);
    tty.clear();
    isInit = true;
}

void terminalPrint(string str) {
    if (isInit) {
        tty.print(str);
    }
}
