//=====================  C++  =====================
#include <string>
//=====================  PRJ  =====================
#include "capturer.h"

Capturer::Capturer(int chnId, SrcCfg_t config) :
    mpDecChannel(NULL)
{
    mpDecChannel = new DecChannel(chnId, config.loaction, config.videoEncType);
}
Capturer::~Capturer()
{
    if(mpDecChannel)
        delete mpDecChannel;
    mpDecChannel = NULL;
}

