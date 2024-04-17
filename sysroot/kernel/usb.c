#include "kernel/usb.h"
#include "kernel/xhcd-ring.h"
#include "kernel/xhcd-event-ring.h"
#include "kernel/usb-messages.h"
#include "stdlib.h"
#include "stdio.h"

#define DESCRIPTOR_TYPE_DEVICE 1
#define DESCRIPTOR_TYPE_CONFIGURATION 2
#define DESCRIPTOR_TYPE_INTERFACE 4
#define DESCRIPTOR_TYPE_ENDPOINT 5
#define DESCRIPTOR_TYPE_SUPER_SPEED_ENDPOINT 0x30

#define REQUEST_SET_CONFIGURATION 9 
#define REQUEST_GET_DESCRIPTOR 6
#define REQUEST_GET_CONFIGURATION 8
#define REQUEST_GET_STATUS 0
#define DESCRIPTOR_TYPE_DEVICE 1 

static UsbDevice initUsbDevice(Usb *usb, UsbControllerDevice device);

static UsbStatus getDeviceDescriptor(UsbDevice* device); 
static UsbStatus getConfiguration(const UsbDevice *device, int configuration, UsbConfiguration **result); 
static UsbConfiguration *parseConfiguration(uint8_t *configBuffer);
static void freeConfiguration(UsbConfiguration *config);
static void freeInterface(UsbInterface *interface);


UsbStatus usb_init(PciGeneralDeviceHeader *pci, Usb *result){
   if(pci->pciHeader.classCode != PCI_CLASS_SERIAL_BUS_CONTROLLER){
      return StatusError;
   }
   if(pci->pciHeader.subclass != PCI_SUBCLASS_USB_CONTROLLER){
      return StatusError;
   }
   if(pci->pciHeader.progIf == PCI_PROG_IF_XHCI){
      Xhci *xhci = malloc(sizeof(Xhci));
      printf("xhci %X\n", xhci);
      if(xhcd_init(pci, xhci) != XhcOk){
         free(xhci);
         return StatusError;
      }
      *result  = (Usb){.type = UsbControllerXhci, {.xhci = xhci}};
      return StatusSuccess;
   }
   printf("USB controller not yet implemented\n");
   return StatusError;
}

int usb_getNewlyAttachedDevices(Usb *usb, UsbDevice *resultBuffer, int bufferSize){
   if(usb->type != UsbControllerXhci){
      printf("USB controller not yet implemented");
      return -1;
   }

   XhcDevice *deviceBuffer = malloc(bufferSize * sizeof(XhcDevice));
   int attachedPortsCount = xhcd_getDevices(usb->xhci, deviceBuffer, bufferSize);
   for(int i = 0; i < attachedPortsCount; i++){
      UsbControllerDevice device = {.type = UsbControllerXhci, {.xhcDevice = &deviceBuffer[i]}};
      resultBuffer[i] = initUsbDevice(usb, device);
   }
   free(deviceBuffer);

   return attachedPortsCount;
}

UsbStatus usb_setConfiguration(UsbDevice *device, UsbConfiguration *configuration){
   if(device->usb->type != UsbControllerXhci){
//      printf("USB controller not yet implemented");
      return StatusError;
   }

   if(xhcd_setConfiguration(device->controllerDevice.xhcDevice, configuration) != XhcOk){
      return StatusError;
   }
   return StatusSuccess;
}
UsbStatus usb_getConfiguration(UsbDevice *device, uint8_t *result){
   if(device->usb->type != UsbControllerXhci){
//      printf("USB controller not yet implemented");
      return StatusError;
   }
   UsbRequestMessage request;
   request.bmRequestType = 0x80;
   request.bRequest = REQUEST_GET_CONFIGURATION;
   request.wValue = 0;
   request.wIndex = 0;
   request.wLength = 1;
   request.dataBuffer = result;
   if(xhcd_sendRequest(device->controllerDevice.xhcDevice, request) != XhcOk){
      return StatusError;
   }
   return StatusSuccess;
}
UsbStatus usb_getStatus(
      UsbDevice *device,
      Recipient recipient,
      StatusType statusType,
      uint16_t index,
      uint8_t *result
){

   if(device->usb->type != UsbControllerXhci){
//      printf("USB controller not yet implemented");
      return StatusError;
   }
   UsbRequestMessage request;
   request.bmRequestType = 0x80 | recipient;
   request.bRequest = REQUEST_GET_STATUS;
   request.wValue = statusType;
   request.wIndex = index;
   request.wLength = statusType == StatusTypeStandard ? 2 : 4;
   request.dataBuffer = result;

   if(xhcd_sendRequest(device->controllerDevice.xhcDevice, request) != XhcOk){
      return StatusError;
   }
   return StatusSuccess;
}
UsbStatus usb_configureDevice(UsbDevice *device, UsbRequestMessage message){
   if(device->usb->type != UsbControllerXhci){
//      printf("USB controller not yet implemented");
      return StatusError;
   }

   if(xhcd_sendRequest(device->controllerDevice.xhcDevice, message) != XhcOk){
      return StatusError;
   }
   return StatusSuccess;
}
UsbDeviceDescriptor usb_getDeviceDescriptor(UsbDevice *device){
   return device->deviceDescriptor;
}
UsbStatus usb_readData(UsbDevice *device, UsbEndpointDescriptor endpoint, void *dataBuffer, int dataBufferSize){
   if(device->usb->type != UsbControllerXhci){
//       printf("USB controller not yet implemented");
      return StatusError;
   }

   if(xhcd_readData(device->controllerDevice.xhcDevice, endpoint, dataBuffer, dataBufferSize) != XhcOk){
      return StatusError;
   }
   return StatusSuccess;

}
UsbStatus usb_writeData(UsbDevice *device, UsbEndpointDescriptor endpoint, void *dataBuffer, int dataBufferSize){
   if(device->usb->type != UsbControllerXhci){
//      printf("USB controller not yet implemented");
      return StatusError;
   }
   if(xhcd_writeData(device->controllerDevice.xhcDevice, endpoint, dataBuffer, dataBufferSize) != XhcOk){
      return StatusError;
   }
   return StatusSuccess;
}
static UsbDevice initUsbDevice(Usb *usb, UsbControllerDevice device){
   XhcDevice *xhcDevice = malloc(sizeof(XhcDevice));
   *xhcDevice = *device.xhcDevice;
   device.xhcDevice = xhcDevice;
   UsbDevice usbDevice = {.controllerDevice = device, .usb = usb};

   getDeviceDescriptor(&usbDevice);

   int configCount = usbDevice.deviceDescriptor.bNumConfigurations;
   UsbConfiguration *configurations = malloc(sizeof(UsbConfiguration) * configCount);
   for(int j = 0; j < configCount; j++){
      UsbConfiguration *config;
      getConfiguration(&usbDevice, j, &config);
      configurations[j] = *config;
      free(config);
   }
   usbDevice.configuration = configurations;
   usbDevice.configurationCount = configCount;

   return usbDevice;
}
static UsbStatus getDeviceDescriptor(UsbDevice *device){
   if(device->usb->type != UsbControllerXhci){
//      printf("USB controller not yet implemented");
      return StatusError;
   }
   UsbDeviceDescriptor result __attribute__((aligned (8))); //Why does not e.g. 64 work?

   UsbRequestMessage request;
   request.bmRequestType = 0x80;
   request.bRequest = REQUEST_GET_DESCRIPTOR;
   request.wValue = DESCRIPTOR_TYPE_DEVICE << 8;
   request.wIndex = 0;
   request.wLength = sizeof(UsbDeviceDescriptor);
   request.dataBuffer = &result; 

   if(xhcd_sendRequest(device->controllerDevice.xhcDevice, request) != XhcOk){
      return StatusError;
   }
   device->deviceDescriptor = result;
   return StatusSuccess;
}
static UsbStatus getConfiguration(const UsbDevice *device, int configuration, UsbConfiguration **result){
   if(device->usb->type != UsbControllerXhci){
//      printf("USB controller not yet implemented");
      return StatusError;
   }

   const int bufferSize =
      sizeof(UsbConfigurationDescriptor) +
      sizeof(UsbInterfaceDescriptor) * 32 +
      (sizeof(UsbEndpointDescriptor) - sizeof(uintptr_t)) * 32 * 15;
   uint8_t buffer[bufferSize];

   UsbRequestMessage request;
   request.bmRequestType = 0x80;
   request.bRequest = REQUEST_GET_DESCRIPTOR;
   request.wValue = DESCRIPTOR_TYPE_CONFIGURATION << 8 | configuration;
   request.wIndex = 0;
   request.wLength = sizeof(buffer);
   request.dataBuffer = buffer;
   if(xhcd_sendRequest(device->controllerDevice.xhcDevice, request) != XhcOk){
      return StatusError;
   }
   *result = parseConfiguration(buffer);
   return StatusSuccess;
}
static UsbConfiguration *parseConfiguration(uint8_t *configBuffer){
   uint8_t *pos = configBuffer;
   UsbConfiguration *config = malloc(sizeof(UsbConfiguration));
   UsbConfigurationDescriptor *configDescriptor = (UsbConfigurationDescriptor*)configBuffer;
   config->descriptor = *configDescriptor;
   config->interfaces = malloc(sizeof(UsbInterface) * configDescriptor->bNumInterfaces);
   pos += sizeof(UsbConfigurationDescriptor);

   for(int i = 0; i < configDescriptor->bNumInterfaces; i++){
      UsbInterfaceDescriptor *interfaceDescriptor = (UsbInterfaceDescriptor*)pos;
      UsbInterface *interface = &config->interfaces[i];
      interface->descriptor = *interfaceDescriptor;
      interface->endpoints = malloc(sizeof(UsbEndpointDescriptor) * interfaceDescriptor->bNumEndpoints);
      pos += sizeof(UsbInterfaceDescriptor);

      for(int j = 0; j < interfaceDescriptor->bNumEndpoints; j++){
         UsbEndpointDescriptor *endpointDescriptor = (UsbEndpointDescriptor*)pos;
         UsbEndpointDescriptor *endpoint = &interface->endpoints[j];

         if(endpointDescriptor->bDescriptorType != DESCRIPTOR_TYPE_ENDPOINT){ //FIXME: kind of a hack to ignore HID descriptors
            printf("ignoring endpoint descriptor type: %X\n", endpointDescriptor->bDescriptorType);
            j--;
            pos += endpointDescriptor->bLength;
            continue;
         }

         *endpoint = *endpointDescriptor;
         pos += endpointDescriptor->bLength;

         //FIXME: What happens if we read outside the buffer?
         UsbSuperSpeedEndpointDescriptor *superSpeedDescriptor = (UsbSuperSpeedEndpointDescriptor*)pos;
         if(superSpeedDescriptor->bDescriptorType == DESCRIPTOR_TYPE_SUPER_SPEED_ENDPOINT){
            printf("UsbSuperSpeedDescriptor found! This is not really implemented!\n"); //FIXME
            endpoint->superSpeedDescriptor = malloc(sizeof(UsbSuperSpeedEndpointDescriptor));
            *endpoint->superSpeedDescriptor = *superSpeedDescriptor;
            pos += superSpeedDescriptor->bLength;
         }else{
            endpoint->superSpeedDescriptor = 0;
         }
      }
   }
   printf("Parsed");
   return config;
}
static void freeConfiguration(UsbConfiguration *config){
   for(int i = 0; i < config->descriptor.bNumInterfaces; i++){
      freeInterface((void*)&config->interfaces[i]);
   }
   free(config);
}
static void freeInterface(UsbInterface *interface){
   for(int i = 0; i < interface->descriptor.bNumEndpoints; i++){
      free((void*)&interface->endpoints[i]);
   }
   free(interface);
}
