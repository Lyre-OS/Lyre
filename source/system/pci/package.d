module system.pci;

import lib.list;
import lib.bus;
import lib.messages;
import lib.alloc;
public import system.pci.scan;
public import system.pci.pci;

private __gshared List!(PCIDevice)* devices;

void initPCI() {
    devices = scanPCI();
    printPCI(devices);
}
