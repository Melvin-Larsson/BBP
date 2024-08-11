#include "stdio.h"
#include "stdlib.h"
#include "kernel/interrupt.h"
#include "kernel/apic.h"
#include "kernel/pci.h"
#include "kernel/usb.h"
#include "kernel/keyboard.h"
#include "string.h"
#include "kernel/descriptors.h"
#include "kernel/paging.h"
#include "kernel/physpage.h"
#include "kernel/allocator.h"
#include "kernel/serial.h"

#include "kernel/logging.h"

#define ASSERTS_ENABLED
#include "utils/assert.h"

#include "kernel/usb-mass-storage.h"

static void printPciDevices(PciDescriptor *descriptors, int count){
    return;
    loggInfo("%d Devices detected:", count);
    for(int i = 0; i < count; i++){
        PciHeader *header = &(descriptors[i].pciHeader);
        char className[50];
        char subclassName[50];
        char progIfName[50];
        
        pci_getClassName(header, className);
        pci_getSubclassName(header, subclassName);
        pci_getProgIfName(header, progIfName);
        loggInfo("%d: %s(%X) - %s(%X) - %s(%X)", i,
                className,header->classCode,
                subclassName, header->subclass,
                progIfName, header->progIf);
    }

}
static PciDescriptor* getXhcdDevice(PciDescriptor* descriptors, int count){
    for(int i = 0; i < count; i++){
        PciHeader *header = &(descriptors[i].pciHeader);

        if(header->classCode == PCI_CLASS_SERIAL_BUS_CONTROLLER
            && header->subclass == PCI_SUBCLASS_USB_CONTROLLER
            && header->progIf == PCI_PROG_IF_XHCI ){

            return &(descriptors[i]);
        }
    }
    return 0;
}
static void initXhci(PciDescriptor pci){
    Usb usb;
    if(usb_init(pci, &usb) != StatusSuccess){
        loggError("Failed to initialize USB");
        return;
    }
    while(1){
        UsbDevice device;
        loggDebug("Wait for attach");
        while(usb_getNewlyAttachedDevices(&usb, &device, 1) == 0);
        loggInfo("Device attached");
//         UsbMassStorageDevice res;
//         UsbMassStorageStatus status = usbMassStorage_init(&device, &res);
//         if(status == UsbMassStorageSuccess){
//             printf("Found deice!\n");
//             uint32_t buffer[64];
//             usbMassStorage_read(&res, 0, buffer, sizeof(buffer));
//             printf("buffer: ");
//             for(int i = 0; i < sizeof(buffer); i++){
//                 printf("%X ", buffer[i]);
//             }
//             printf("\n");
//             while(1);
//         }else{
//             printf("Failed to init: %X\n", status);
//             while(1);
//         }
        KeyboardStatus status = keyboard_init(&device);
        char buffer[100];
        keyboard_getStatusCode(status, buffer);
        loggInfo("Keyboard status code: %s", buffer);
    }
}
static int overlaps(uint8_t *p1, int s1, uint8_t *p2, int s2){
    return p1 + s1 > p2 && p1 < p2 + s2;

}
static void testMemory(){
    uint8_t *m1 = malloc(10);
    uint8_t *m2 = malloc(10);
    uint8_t *m3 = malloc(10);
    if(overlaps(m1, 10, m2, 10) || overlaps(m2, 10, m3, 10) || overlaps(m1, 10, m3, 10)){
        printf("Memory check failed, memories overlap!\n");
        while(1);
    }
    free(m2);
    uint8_t *m4 = malloc(10);
    if(m2 != m4){
        printf("Memory check failed, failed to reuse memory(1)!\n");
        while(1);
    }
    free(m4);
    uint8_t *m5 = malloc(1);
    uint8_t *m6 = malloc(1);
    if(!overlaps(m4, 10, m5, 1) || !overlaps(m4, 10, m6, 1)){
        printf("Memory check failed, failed to reuse memory(2)!\n");
        while(1);
    }
    free(m1);
    free(m3);
    free(m5);
    free(m6);
    uint8_t *m7 = malloc(10);
    if(m7 != m1){
        printf("Memory check failed, failed to reuse memory(3)!\n");
        while(1);
    }
    free(m7);
    printf("Passed memory check!\n");
}
static void testMemoryConstrained(){
    uint8_t *m1 = mallocco(10, 0x100, 0);
    if((uintptr_t)m1 % 0x100 != 0){
        printf("Constrained memory check failed, failed to align!\n");
        while(1);
    }
    uint8_t *m2 = malloc(1);
    if(m2 > m1){
        printf("Constrained memory check failed, failed to use alignment space\n");
        while(1);
    }
    uint8_t *m3 = mallocco(0x1000, 1, 0x1000);
    if(!m3 || (uintptr_t)m3 / 0x1000 != ((uint64_t)m3 + 0x1000-1) / 0x1000){
        printf("Constrained memory check failed, failed to avoid boundary %X\n", m3);
        while(1);
    }
    free(m1);
    free(m2);
    free(m3);
    uint8_t *m4 = mallocco(10, 2, 10);
    uint8_t *m5 = mallocco(7, 3, 10);
    uint8_t *m6 = mallocco(10, 3, 10);
    if(!m4 || !m5 || m6){
        printf("Constrained memory check failed, failed to avoid follow constraints\n");
        while(1);
    }
    free(m4);
    free(m5);
    free(m6);
    printf("Passed constrained memory check");
}
void assert_little_endian(){
    uint32_t i = 0x12345678;
    uint8_t *ptr = (uint8_t*)&i;
    assert(ptr[0] == 0x78);
    assert(ptr[1] == 0x56);
    assert(ptr[2] == 0x34);
    assert(ptr[3] == 0x12);
}

void serial_writer(const char *str){
    serial_write(COM1, str);
    serial_write(COM1, "\n\r");
}
void console_writer(LoggContext context, LoggLevel level, const char *format, va_list args){
    StdioColor prevColor = stdio_getColor();
    StdioColor newColor;
    switch(level){
        case LoggLevelDebug:
            newColor = StdioColorGray;
            break;
        case LoggLevelInfo:
            newColor = StdioColorWhite;
            break;
        case LoggLevelWarning:
            newColor = StdioColorYellow;
            break;
        case LoggLevelError:
            newColor = StdioColorRed;
            break;
        default:
            newColor = prevColor;
            break;
    }
    stdio_setColor(newColor);
    char *buffer = malloc(4096); //FIXME: Could lead to array out of bounds
    vsprintf(buffer, format, args);
    printf(buffer);
    printf("\n");
    stdio_setColor(prevColor);
}

void kernel_main(){
    stdioinit();
    stdlib_init();
//     testMemory();
//     testMemoryConstrained();
    printf("Kernel started\n");
    interruptDescriptorTableInit(); 
    assert_little_endian();

    physpage_init();

    SerialPortConfig serialConfig = serial_defaultConfig();
    serial_initPort(COM1, serialConfig);

    LoggWriter consoleWriter = logging_getCustomWriter(console_writer);
    LoggWriter serialWriter = logging_getDefaultWriter(serial_writer);
    serialWriter.loggLevel = LoggLevelDebug;
    consoleWriter.loggLevel = LoggLevelNone,
    logging_init();
    logging_addWriter(consoleWriter);
    logging_addWriter(serialWriter);

    PagingConfig32Bit config = {
        .use4MBytePages = 1,
    };
    PagingContext *context = paging_init32Bit(config, physpage_getPage4KB());
    assert(config.use4MBytePages == context->config.use4MBytePages);
    paging_setContext(context);

    for(uint32_t i = 0; i < 1; i++){
        PagingTableEntry entry = {
            .physicalAddress = i * 4 * 1024 * 1024,
            .readWrite = 1,
            .pageWriteThrough = 1,
            .pageCahceDisable = 1,
            .Use4MBPageSize = 1
        };
        
        physpage_markPagesAsUsed4MB(i, 1);

        PagingStatus status = paging_addEntry(entry, i * 4 * 1024 * 1024);
        assert(status == PagingOk);
    }

    paging_start();

//     printf("APIC present: %b\n", apic_isPresent());

    PciDescriptor devices[20];
    int count = pci_getDevices(devices, 10);
    loggInfo("here\n");
    printPciDevices(devices, count);
    PciDescriptor *xhcDevice = getXhcdDevice(devices, count);

    if(!xhcDevice){
        loggError("Error: Could not find xhc device!");
    }else{
        loggInfo("Found a xhc device!");
        initXhci(*xhcDevice);
    }

    loggInfo("end");
    while(1);
}
