#include "ollee_protocol.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
int main(){
    assert(ollee::crc(reinterpret_cast<const uint8_t*>("123456789"),9)==0x29b1);
    uint8_t f[257];size_t n=ollee::frame(0x2a,nullptr,0,f,sizeof(f));
    assert(f[4]==0xfe&&f[5]==0x45);
    assert(n==8&&f[0]==0&&f[1]==6&&f[2]==0xaa&&f[3]==0x55&&f[6]==2&&f[7]==0x2a);
    uint8_t payload[20];assert(ollee::timePayload(1794168000,-25200,39739,-104990,123,0,payload));
    assert(payload[4]==0x90&&payload[5]==0x9d&&payload[6]==0xff&&payload[7]==0xff);
    assert(payload[16]==123&&payload[17]==0);
    n=ollee::frame(0x23,payload,20,f,sizeof(f));assert(n==28&&f[1]==26);
    // Independent reference: lengths, little-endian time and CRC checked by Python fixture below.
    assert(f[8]==0xc0&&f[9]==0xd4&&f[10]==0xf0&&f[11]==0x6a);
    ollee::Receiver r;unsigned frames=0;for(size_t i=0;i<n;++i)if(r.byte(f[i]))++frames;assert(frames==1&&r.used==0&&r.buffer[7]==0x23);
    for(size_t corrupt=0;corrupt<n;++corrupt){ollee::Receiver broken;uint8_t copy[28];memcpy(copy,f,n);copy[corrupt]^=1;bool ok=false;for(size_t i=0;i<n;++i)ok|=broken.byte(copy[i]);assert(!ok);}
    // Arbitrary notification boundaries and concatenated messages.
    r={};frames=0;for(int repeat=0;repeat<3;++repeat)for(size_t i=0;i<n;++i)frames+=r.byte(f[i]);assert(frames==3);
    uint8_t version[36]={};memcpy(version+8,"01.05.00",8);memcpy(version+16,"00.01.11",8);assert(ollee::supportedVersion(version,36));assert(!ollee::supportedVersion(version,35));version[23]='0';assert(!ollee::supportedVersion(version,36));
    assert(!ollee::timePayload(0,0,0,0,0,0,payload));assert(!ollee::timePayload(1794168000,0,90001,0,0,0,payload));assert(!ollee::timePayload(1794168000,0,0,0,1000,0,payload));
    assert(!ollee::frame(0x23,payload,250,f,sizeof(f)));assert(!ollee::frame(0x23,payload,20,f,27));
    std::cout<<"Ollee CRC, framing, fragmentation, signed clock fields, version gate and invalid input tests passed\n";
}
