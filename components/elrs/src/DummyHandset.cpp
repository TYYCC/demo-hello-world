#include "DummyHandset.h"

// 全局 DummyHandset
DummyHandset* dummyHandset = nullptr;

DummyHandset::DummyHandset() {
    for (int i = 0; i < NUM_CHANNELS; i++)
        internalCh[i] = 1500;
}

void DummyHandset::JustSentRFpacket() {
    for (int i = 0; i < NUM_CHANNELS; i++) {
        internalCh[i] += (i % 2 ? 1 : -1);
        if (internalCh[i] < 1000) internalCh[i] = 1000;
        if (internalCh[i] > 2000) internalCh[i] = 2000;

        // 同步到全局 ChannelData
        ChannelData[i] = internalCh[i];
    }
}

void DummyHandset::registerCallbacks(void (*connectCb)(), void (*disconnectCb)()) {
    (void)connectCb;
    (void)disconnectCb;
}

void DummyHandset::setChannelValue(uint32_t ch, uint16_t value) {
    clampChannel(ch);
    internalCh[ch] = value;
    ChannelData[ch] = value;
}

uint16_t DummyHandset::getChannelValue(uint32_t ch) const {
    if (ch >= NUM_CHANNELS) ch = NUM_CHANNELS - 1;
    return internalCh[ch];
}

void DummyHandset::clampChannel(uint32_t &ch) {
    if (ch >= NUM_CHANNELS) ch = NUM_CHANNELS - 1;
}

void initVirtualHandset() {
    if (!dummyHandset) dummyHandset = new DummyHandset();

    // 使用 ELRS 原有的指针，不改变类型
    if (!ExpressLRS_currAirRate_Modparams) return;
    ExpressLRS_currAirRate_Modparams->numOfSends = 1;  // 初始化 numOfSends
    handset = dummyHandset;  // 替换全局 handset 指针
}
