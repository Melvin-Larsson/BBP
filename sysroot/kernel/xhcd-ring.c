#include "kernel/xhcd-ring.h"
#include "stdio.h"
#include "stdlib.h"
#include "stdint.h"

#define DEFAULT_PCS 1

#define CRCR_OFFSET 0x18

#define TRB_TYPE_LINK 6
#define TRB_TYPE_NOOP 23
#define TRB_TYPE_ENABLE_SLOT 9
#define TRB_TYPE_ADDRESS_DEVICE 11
#define TRB_TYPE_EVALUATE_CONTEXT 13
#define TRB_TYPE_CONFIGURE_ENDPOINT 12
#define TRB_TYPE_SETUP 2
#define TRB_TYPE_DATA 3
#define TRB_TYPE_STATUS 4
#define TRB_TYPE_NORMAL 1


#define TRB_SLOT_TYPE_POS 16
#define TRB_SLOT_ID_POS 24
#define TRB_TYPE_POS 10

#define DIRECTION_IN 1
#define DIRECTION_OUT 0

#define ADDRESS_TRB_BSR_POS 9

#define TRANSFER_TYPE_IN 3

#define REQUEST_GET_DESCRIPTOR 6
#define REQUEST_SET_CONFIGURATION 9
#define REQUEST_SET_PROTOCOL 0xB
#define REQUEST_GET_PROTOCOL 3

#define DESCRIPTOR_TYPE_DEVICE 1
#define DESCRIPTOR_TYPE_CONFIGURATION 2

static void initSegment(Segment segment, Segment nextSegment, int isLast);

XhcdRing xhcd_newRing(int trbCount){
   void* ringAddress = callocco(trbCount * sizeof(TRB), 64, 64000);
   Segment segment = {(uintptr_t)ringAddress, trbCount};
   initSegment(segment, segment, 1); //FIXME: isLast = 1

   XhcdRing ring;
   ring.pcs = DEFAULT_PCS;
   ring.dequeue = (TRB *)ringAddress;
   return ring;
}
int xhcd_attachCommandRing(XhciOperation *operation, XhcdRing *ring){
   uintptr_t address = (uintptr_t)ring->dequeue;
   operation->commandRingControll  = address | ring->pcs;
   return 1;
}
void xhcd_putTD(TD td, XhcdRing *ring){
   for(int i = 0; i < td.trbCount; i++){
      xhcd_putTRB(td.trbs[i], ring);
   }
}
void xhcd_putTRB(TRB trb, XhcdRing *ring){
   trb.cycleBit = ring->pcs;
   *ring->dequeue = trb; 
   ring->dequeue++;
   if(ring->dequeue->type == TRB_TYPE_LINK){
      LinkTRB *link = (LinkTRB*)ring->dequeue;
      link->cycleBit = ring->pcs;
      uintptr_t address = link->ringSegment;
      ring->dequeue = (TRB*)address;
      ring->pcs ^= link->toggleCycle;
   }
}
TRB TRB_NOOP(){
   TRB trb = {{{0,0,0,0}}};
   trb.r3 = TRB_TYPE_NOOP << TRB_TYPE_POS;
   return trb;
}
TRB TRB_ENABLE_SLOT(int slotType){
   TRB trb = {{{0,0,0,0}}};
   trb.r3 = TRB_TYPE_ENABLE_SLOT << TRB_TYPE_POS |
            slotType << TRB_SLOT_TYPE_POS;
   return trb;
}
TRB TRB_ADDRESS_DEVICE(uint64_t inputContextAddr, uint32_t slotId, uint32_t bsr){
   TRB trb = {{{0,0,0,0}}};
   trb.r0 = (uint32_t)inputContextAddr;
   trb.r1 = (uint32_t)(inputContextAddr >> 32);
   trb.r3 = bsr << ADDRESS_TRB_BSR_POS |
            TRB_TYPE_ADDRESS_DEVICE << TRB_TYPE_POS |
            slotId << TRB_SLOT_ID_POS;
   return trb;
}
TRB TRB_EVALUATE_CONTEXT(void* inputContext, uint32_t slotId){
   TRB trb = {{{0,0,0,0}}};
   trb.dataBufferPointer = (uintptr_t)inputContext;
   trb.r3 = TRB_TYPE_EVALUATE_CONTEXT << TRB_TYPE_POS |
            slotId << TRB_SLOT_ID_POS;
   return trb;
}
TRB TRB_CONFIGURE_ENDPOINT(void *inputContext, uint32_t slotId){
   TRB trb = {{{0,0,0,0}}};
   trb.dataBufferPointer = (uintptr_t)inputContext;
   trb.r3 = TRB_TYPE_CONFIGURE_ENDPOINT << TRB_TYPE_POS |
            slotId << TRB_SLOT_ID_POS;
   return trb;


}
TRB TRB_NORMAL(void *dataBuffer, uint16_t bufferSize){
   TRB trb = {{{0,0,0,0}}};
   trb.dataBufferPointer = (uintptr_t)dataBuffer;
   trb.transferLength = bufferSize;
   trb.interruptOnCompletion = 1;
   trb.interruptOnShortPacket = 1;
   trb.type = TRB_TYPE_NORMAL; 

   return trb;
}
TRB TRB_SETUP_STAGE(SetupStageHeader header){
   SetupStageTRB setupTrb = {{{0,0,0,0}}};

   setupTrb.bmRequestType = header.bmRequestType;
   setupTrb.bRequest = header.bRequest;
   setupTrb.wValue = header.wValue;
   setupTrb.wIndex = header.wIndex;
   setupTrb.wLength = header.wLength;

   setupTrb.transferLength = 8;
   setupTrb.immediateData = 1;
   setupTrb.interruptOnCompletion = 0;
   setupTrb.type = TRB_TYPE_SETUP;
   setupTrb.transferType = InDataStage;
   TRB result;
   memcpy(&result, &setupTrb, sizeof(TRB));
   return result;
}
TRB TRB_DATA_STAGE(uint64_t dataBufferPointer, int bufferSize){
   DataStageTRB dataTrb = {{{0,0,0,0}}};

   dataTrb.dataBuffer = dataBufferPointer;
   dataTrb.transferLength = bufferSize;
   dataTrb.chainBit = 0;
   dataTrb.interruptOnCompletion = 0;
   dataTrb.immediateData = 0;
   dataTrb.type = TRB_TYPE_DATA;
   dataTrb.direction = DIRECTION_IN;
   TRB result;
   memcpy(&result, &dataTrb, sizeof(TRB));
   return result;
}
TRB TRB_STATUS_STAGE(){
   StatusStageTRB statusTrb = {{{0,0,0,0}}};
   statusTrb.interruptOnCompletion = 1;
   statusTrb.type = TRB_TYPE_STATUS;
   statusTrb.direction = DIRECTION_OUT; //Questionable 
   statusTrb.chainBit = 0;

   TRB result;
   memcpy(&result, &statusTrb, sizeof(TRB));
   return result;
}
TD TD_GET_DESCRIPTOR(void *dataBufferPointer, int descriptorLength){
   SetupStageHeader setupHeader;

   setupHeader.bmRequestType = 0x80;
   setupHeader.bRequest = REQUEST_GET_DESCRIPTOR;
   setupHeader.wValue = DESCRIPTOR_TYPE_DEVICE << 8;
   setupHeader.wIndex = 0;
   setupHeader.wLength = descriptorLength;

   TRB setupTrb = TRB_SETUP_STAGE(setupHeader);
   TRB dataTrb = TRB_DATA_STAGE((uintptr_t)dataBufferPointer, descriptorLength);
   TRB statusTrb = TRB_STATUS_STAGE();

   TD result = {{setupTrb, dataTrb, statusTrb}, 3};
   return result;
}
TD TD_GET_CONFIGURATION_DESCRIPTOR(void *dataBufferPointer, int descriptorLength, uint8_t descriptorIndex){
   SetupStageHeader header;
   header.bmRequestType = 0x80;
   header.bRequest = REQUEST_GET_DESCRIPTOR;
   header.wValue = DESCRIPTOR_TYPE_CONFIGURATION << 8 | descriptorIndex;
   header.wIndex = 0;
   header.wLength = descriptorLength;

   TRB setupTrb = TRB_SETUP_STAGE(header);
   TRB dataTrb = TRB_DATA_STAGE((uintptr_t)dataBufferPointer, descriptorLength);
   TRB statusTrb = TRB_STATUS_STAGE();

   TD result = {{setupTrb, dataTrb, statusTrb}, 3};
   printf("scount %d\n", result.trbCount);

   return result;
}
TD TD_SET_CONFIGURATION(int configuration){
   SetupStageHeader header;
   header.bmRequestType = 0;
   header.bRequest = REQUEST_SET_CONFIGURATION;
   header.wValue = configuration;
   header.wIndex = 0;
   header.wLength = 0;
   TRB setupTrb = TRB_SETUP_STAGE(header);
   TRB statusTrb = TRB_STATUS_STAGE();

   TD result = {{setupTrb, statusTrb}, 2};
   return result;
}
TD TD_SET_PROTOCOL(int protocol, int interface){
   if(protocol != 0 && protocol != 1){
      printf("[xhc] invalid protocol: %d\n", protocol);
      return (TD){{},0};
   }
   SetupStageHeader header;
   header.bmRequestType = 0x21;
   header.bRequest = REQUEST_SET_PROTOCOL;
   header.wValue = protocol;
   header.wIndex = interface;
   header.wLength = 0;
   TRB setupTrb = TRB_SETUP_STAGE(header);
   TRB statusTrb = TRB_STATUS_STAGE();

   TD result = {{setupTrb, statusTrb}, 2};
   return result;
}
TD TD_GET_PROTOCOL(int interface, uint8_t *resultPointer){
   SetupStageHeader header;
   header.bmRequestType = 0xA1;
   header.bRequest = REQUEST_GET_PROTOCOL;
   header.wValue = 0;
   header.wIndex = interface;
   header.wLength = 1;
   TRB setupTrb = TRB_SETUP_STAGE(header);
   TRB dataTrb = TRB_DATA_STAGE((uintptr_t)resultPointer, 1);
   TRB statusTrb = TRB_STATUS_STAGE();

   TD result = {{setupTrb, dataTrb, statusTrb}, 3};
   return result;
}
TD TD_GET_REPORT(void *dataBufferPointer, uint16_t bufferSize, int interface){
   SetupStageHeader header;
   header.bmRequestType = 0xA1;
   header.bRequest = 1;
   header.wValue = 0x0100;
   header.wIndex = interface;
   header.wLength = bufferSize;
   TRB setupTrb = TRB_SETUP_STAGE(header);
   TRB dataTrb = TRB_DATA_STAGE((uintptr_t)dataBufferPointer, bufferSize);
   TRB statusTrb = TRB_STATUS_STAGE();

   TD result = {{setupTrb, dataTrb, statusTrb}, 3};
   return result;
}
static void initSegment(Segment segment, Segment nextSegment, int isLast){
   memset((void*)segment.address, 0, segment.trbCount * sizeof(TRB));
   TRB *trbs = (TRB*)segment.address;
   if(!DEFAULT_PCS){
      for(int i = 0; i < segment.trbCount; i ++){
         trbs[i].r3 |= DEFAULT_PCS; 
      }
   }
   LinkTRB *link = (LinkTRB*)&trbs[segment.trbCount - 1];
   link->ringSegment = (uintptr_t)nextSegment.address;
   link->cycleBit = DEFAULT_PCS;
   link->toggleCycle = isLast;
   link->trbType = TRB_TYPE_LINK;
}
