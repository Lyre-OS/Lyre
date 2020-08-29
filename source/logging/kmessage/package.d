module logging.kmessage;

import lib.bus;
import lib.messages;
import logging.terminal;
import system.cpu;

private immutable colorCyan    = "\033[36m";
private immutable colorMagenta = "\033[35m";
private immutable colorRed     = "\033[31m";
private immutable colorReset   = "\033[0m";



struct KMessage {
    public enum Priority {
        Log,
        Warn,
        Error
    }

    private struct Sink {
        void function(string msg) write;
        Priority minPrio;
    }

    public void debugPrint(Priority prio, string msg) {
        string color;
        final switch (prio) {
            case Priority.Log:
                color = colorCyan;
                break;
            case Priority.Warn:
                color = colorMagenta;
                break;
            case Priority.Error:
                color = colorRed;
                break;
        }
        foreach (sink; sinks[0..firstFreeSink]) {
            if (prio >= sink.minPrio) {
                sink.write(color);
                sink.write(">> ");
                if (prio <= Priority.Log) sink.write(colorReset); // If not a logging message, color all the message
                sink.write(msg);
                sink.write(colorReset);
                sink.write("\n");
            }
        }
    }


    public bool addLogSink(Priority minPrio, void function(string msg) write) {
        if (firstFreeSink > numSinks) return false;
        sinks[firstFreeSink].minPrio = minPrio;
        sinks[firstFreeSink].write = write;
        firstFreeSink++;
        return true;
    }

    private immutable numSinks = 8;
    private Sink[numSinks] sinks;
    private uint firstFreeSink = 0; // sinks are contiguous, use swap-and-pop to remove one
}
