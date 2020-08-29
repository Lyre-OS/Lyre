module lib.messages;

import logging.kmessage;
import logging.terminal;
import main;
import system.cpu;
import lib.string;

private immutable CONVERSION_TABLE = "0123456789abcdef";
private __gshared size_t    bufferIndex;
private __gshared char[256] buffer;
private __gshared KMessage kmsg;

void log(T...)(T form) {
    format(form);
    sync(KMessage.Priority.Log);
}

void warn(T...)(T form) {
    format(form);
    sync(KMessage.Priority.Warn);
}

void error(T...)(T form) {
    format(form);
    sync(KMessage.Priority.Error);
}

void panic(T...)(T form) {
    addToBuffer("Panic: ");
    format(form);
    addToBuffer("\nThe system will now proceed to die");
    sync(KMessage.Priority.Error);

    while (true) {
        asm {
            cli;
            hlt;
        }
    }
}

void addLogSink(string name, KMessage.Priority minPrio, void function(string msg) write) {
    log("messages: Added sink \"", name, "\"");
    if (kmsg.addLogSink(minPrio, write) == false) {
        warn("messages: No sinks left");
    }
}


private void format(T...)(T items) {
    foreach (i; items) {
        addToBuffer(i);
    }
}

private void addToBuffer(ubyte add) {
    addToBuffer(cast(size_t)add);
}

private void addToBuffer(char add) {
    buffer[bufferIndex++] = add;
}

private void addToBuffer(string add) {
    foreach (c; add) {
        addToBuffer(c);
    }
}

private void addToBuffer(void* addr) {
    addToBuffer(cast(size_t)addr);
}

private void addToBuffer(size_t x) {
    int i;
    char[17] buf;

    buf[16] = 0;

    if (!x) {
        addToBuffer("0x0");
        return;
    }

    for (i = 15; x; i--) {
        buf[i] = CONVERSION_TABLE[x % 16];
        x /= 16;
    }

    i++;
    addToBuffer("0x");
    addToBuffer(fromCString(&buf[i]));
}

private void sync(KMessage.Priority priority) {
    buffer[bufferIndex] = '\0';
    bufferIndex = 0;

    kmsg.debugPrint(priority, fromCString(buffer.ptr));
}
