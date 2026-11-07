#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace mw34::texture_staging {
// Preserve the renderer's exact integer expansion, including floor division.
// Convert in CPU-cached memory, then copy contiguous blocks to write-combined
// Vulkan staging memory. No reads of mapped upload memory are required.
inline std::uint32_t expand(std::uint16_t p,unsigned format) {
    unsigned r,g,b,a;
    if(format==0){r=((p>>11)&31)*255/31;g=((p>>5)&63)*255/63;b=(p&31)*255/31;a=255;}
    else if(format==1){r=((p>>10)&31)*255/31;g=((p>>5)&31)*255/31;b=(p&31)*255/31;a=(p&0x8000)?255:0;}
    else {r=((p>>8)&15)*17;g=((p>>4)&15)*17;b=(p&15)*17;a=((p>>12)&15)*17;}
    return r|(g<<8)|(b<<16)|(a<<24);
}
inline const std::array<std::uint32_t,65536>& table(unsigned format) {
    static const auto tables=[] {
        std::array<std::array<std::uint32_t,65536>,3> values{};
        for(unsigned f=0;f<3;++f)for(unsigned p=0;p<65536;++p)values[f][p]=expand(std::uint16_t(p),f);
        return values;
    }();
    return tables.at(format);
}
inline void copy(unsigned char* destination,const std::uint16_t* source,std::size_t count,unsigned format) {
    const auto& lookup=table(format);
    std::array<std::uint32_t,4096> chunk;
    while(count){
        const auto n=std::min(count,chunk.size());
        for(std::size_t i=0;i<n;++i)chunk[i]=lookup[source[i]];
        std::memcpy(destination,chunk.data(),n*sizeof(chunk[0]));
        source+=n;destination+=n*sizeof(chunk[0]);count-=n;
    }
}
}
